/**************************************************************************//**
 * @file cdc.c
 * @brief USB Communication Device Class (CDC) driver.
 * @version 5.0.0
 ******************************************************************************
 * @section License
 * <b>Copyright 2015 Silicon Labs, Inc. http://www.silabs.com</b>
 *******************************************************************************
 *
 * This file is licensed under the Silabs License Agreement. See the file
 * "Silabs_License_Agreement.txt" for details. Before using this software for
 * any purpose, you must agree to the terms of that agreement.
 *
 ******************************************************************************/
#include "cdc.h"
#include <descriptors.h>
#include <SEGGER/SEGGER_RTT.h>
#include <string.h>

#define CDC_BULK_EP_SIZE  (USB_FS_BULK_EP_MAXSIZE) /* This is the max. ep size.    */
#define CDC_USB_RX_BUF_SIZ  CDC_BULK_EP_SIZE /* Packet size when receiving on USB. */
#define CDC_USB_TX_BUF_SIZ  127    /* Packet size when transmitting on USB.  */

/* The serial port LINE CODING data structure, used to carry information  */
/* about serial port baudrate, parity etc. between host and device.       */
SL_PACK_START(1)
typedef struct
{
	uint32_t dwDTERate;               /** Baudrate                            */
	uint8_t  bCharFormat;             /** Stop bits, 0=1 1=1.5 2=2            */
	uint8_t  bParityType;             /** 0=None 1=Odd 2=Even 3=Mark 4=Space  */
	uint8_t  bDataBits;               /** 5, 6, 7, 8 or 16                    */
	uint8_t  dummy;                   /** To ensure size is a multiple of 4 bytes */
} SL_ATTRIBUTE_PACKED cdcLineCoding_TypeDef;
SL_PACK_END()

/*** Function prototypes. ***/

static int  UsbDataReceived_from_ISR(USB_Status_TypeDef status, uint32_t rx_count, uint32_t remaining);
static int  LineCodingReceived_from_ISR(USB_Status_TypeDef status, uint32_t xferred,	uint32_t remaining);

/*** Variables ***/

/*
 * The LineCoding variable must be 4-byte aligned as it is used as USB
 * transmit and receive buffer.
 */
SL_ALIGN(4)
SL_PACK_START(1)
static cdcLineCoding_TypeDef SL_ATTRIBUTE_ALIGN(4) cdcLineCoding =
{
		115200, 0, 0, 8, 0
};
SL_PACK_END()

static uint8_t _rx_buffer[CDC_USB_RX_BUF_SIZ];
static const uint8_t *_tx_buffer_ptr;
static uint32_t _bytes_to_send;

/** @endcond */

/**************************************************************************//**
 * @brief CDC device initialization.
 *****************************************************************************/
void CDC_Init( void )
{
	static const USBD_Callbacks_TypeDef callbacks =
	{
			.usbReset        = NULL,
			.usbStateChange  = CDC_StateChangeEvent,
			.setupCmd        = CDC_SetupCmd,
			.isSelfPowered   = NULL,
			.sofInt          = NULL
	};

	static const USBD_Init_TypeDef usbInitStruct =
	{
			.deviceDescriptor    = &USBDESC_deviceDesc,
			.configDescriptor    = USBDESC_configDesc,
			.stringDescriptors   = USBDESC_strings,
			.numberOfStrings     = sizeof(USBDESC_strings)/sizeof(void*),
			.callbacks           = &callbacks,
			.bufferingMultiplier = USBDESC_bufferingMultiplier,
			.reserved            = 0
	};

	USBD_Init(&usbInitStruct);
}

/**************************************************************************//**
 * @brief
 *   Handle USB setup commands. Implements CDC class specific commands.
 *
 * @param[in] setup Pointer to the setup packet received.
 *
 * @return USB_STATUS_OK if command accepted.
 *         USB_STATUS_REQ_UNHANDLED when command is unknown, the USB device
 *         stack will handle the request.
 *****************************************************************************/
int CDC_SetupCmd(const USB_Setup_TypeDef *setup)
{

	int retVal = USB_STATUS_REQ_UNHANDLED;

	if ( ( setup->Type      == USB_SETUP_TYPE_CLASS          ) &&
			( setup->Recipient == USB_SETUP_RECIPIENT_INTERFACE )    )
	{
		switch (setup->bRequest)
		{
		case USB_CDC_GETLINECODING:
			/********************/
			if ( ( setup->wValue    == 0                     ) &&
					( setup->wIndex    == CDC_CTRL_INTERFACE_NO ) && /* Interface no. */
					( setup->wLength   == 7                     ) && /* Length of cdcLineCoding. */
					( setup->Direction == USB_SETUP_DIR_IN      )    )
			{
				/* Send current settings to USB host. */
				USBD_Write(0, (void*) &cdcLineCoding, 7, NULL);
				retVal = USB_STATUS_OK;
			}
			break;

		case USB_CDC_SETLINECODING:
			/********************/
			if ( ( setup->wValue    == 0                     ) &&
					( setup->wIndex    == CDC_CTRL_INTERFACE_NO ) && /* Interface no. */
					( setup->wLength   == 7                     ) && /* Length of cdcLineCoding. */
					( setup->Direction != USB_SETUP_DIR_IN      )    )
			{
				/* Get new settings from USB host. */
				USBD_Read(0, (void*) &cdcLineCoding, 7, LineCodingReceived_from_ISR);
				retVal = USB_STATUS_OK;
			}
			break;

		case USB_CDC_SETCTRLLINESTATE:
			/********************/
			if ( ( setup->wIndex  == CDC_CTRL_INTERFACE_NO ) &&   /* Interface no.  */
					( setup->wLength == 0                     )    ) /* No data.       */
			{
				/* Do nothing ( Non compliant behaviour !! ) */
				retVal = USB_STATUS_OK;
			}
			break;
		}
	}

	return retVal;
}

/**************************************************************************//**
 * @brief
 *   Callback function called each time the USB device state is changed.
 *   Starts CDC operation when device has been configured by USB host.
 *
 * @param[in] oldState The device state the device has just left.
 * @param[in] newState The new device state.
 *****************************************************************************/
void CDC_StateChangeEvent( USBD_State_TypeDef oldState,
		USBD_State_TypeDef newState)
{
	SEGGER_RTT_printf(0, "old %d new %d\n", oldState, newState);
	if (newState == USBD_STATE_CONFIGURED)
	{
		/* We have been configured, start CDC functionality ! */

		if (oldState == USBD_STATE_SUSPENDED)   /* Resume ?   */
		{
		}

		/* Start receiving data from USB host. */
		USBD_Read(CDC_EP_DATA_OUT, (void*)_rx_buffer, sizeof(_rx_buffer), UsbDataReceived_from_ISR);
	}

	else if ((oldState == USBD_STATE_CONFIGURED) &&
			(newState != USBD_STATE_SUSPENDED))
	{
		/* We have been de-configured, stop CDC functionality. */
	}

	else if (newState == USBD_STATE_SUSPENDED)
	{
		/* We have been suspended, stop CDC functionality. */
		/* Reduce current consumption to below 2.5 mA.     */
	}
}

/** @cond DO_NOT_INCLUDE_WITH_DOXYGEN */

/**************************************************************************//**
 * @brief Callback function called whenever a new packet with data is received
 *        on USB.
 *
 * @param[in] status    Transfer status code.
 * @param[in] xferred   Number of bytes transferred.
 * @param[in] remaining Number of bytes not transferred.
 *
 * @return USB_STATUS_OK.
 *****************************************************************************/
static int UsbDataReceived_from_ISR(USB_Status_TypeDef status, uint32_t rx_count, uint32_t remaining_count __attribute__((unused)))
{
	if ((status == USB_STATUS_OK) && (rx_count > 0))
	{
		SEGGER_RTT_Write(0, _rx_buffer, rx_count);

		/* Start a new USB receive transfer. */
		USBD_Read(CDC_EP_DATA_OUT, (void*)_rx_buffer, sizeof(_rx_buffer), UsbDataReceived_from_ISR);

	}
	return USB_STATUS_OK;
}

/**************************************************************************//**
 * @brief Callback function called whenever a packet with data has been
 *        transmitted on USB
 *
 * @param[in] status    Transfer status code.
 * @param[in] xferred   Number of bytes transferred.
 * @param[in] remaining Number of bytes not transferred.
 *
 * @return USB_STATUS_OK.
 *****************************************************************************/
static int UsbDataTransmitted_from_ISR(USB_Status_TypeDef status, uint32_t xferred, uint32_t remaining __attribute__((unused)))
{
	if (status == USB_STATUS_OK)
	{
		_bytes_to_send -= xferred;
		_tx_buffer_ptr += xferred;

		USBD_Write(CDC_EP_DATA_IN, (void*)_tx_buffer_ptr, _bytes_to_send, UsbDataTransmitted_from_ISR);
	}
	return USB_STATUS_OK;
}

/**************************************************************************//**
 * @brief
 *   Callback function called when the data stage of a CDC_SET_LINECODING
 *   setup command has completed.
 *
 * @param[in] status    Transfer status code.
 * @param[in] xferred   Number of bytes transferred.
 * @param[in] remaining Number of bytes not transferred.
 *
 * @return USB_STATUS_OK if data accepted.
 *         USB_STATUS_REQ_ERR if data calls for modes we can not support.
 *****************************************************************************/
static int LineCodingReceived_from_ISR(USB_Status_TypeDef status,
		uint32_t xferred,
		uint32_t remaining)
{

	uint32_t frame = 0;
	(void) remaining;

	/* We have received new serial port communication settings from USB host. */
	if ((status == USB_STATUS_OK) && (xferred == 7))
	{
		/* Check bDataBits, valid values are: 5, 6, 7, 8 or 16 bits. */
		if (cdcLineCoding.bDataBits == 5)
			frame |= USART_FRAME_DATABITS_FIVE;

		else if (cdcLineCoding.bDataBits == 6)
			frame |= USART_FRAME_DATABITS_SIX;

		else if (cdcLineCoding.bDataBits == 7)
			frame |= USART_FRAME_DATABITS_SEVEN;

		else if (cdcLineCoding.bDataBits == 8)
			frame |= USART_FRAME_DATABITS_EIGHT;

		else if (cdcLineCoding.bDataBits == 16)
			frame |= USART_FRAME_DATABITS_SIXTEEN;

		else
			return USB_STATUS_REQ_ERR;

		/* Check bParityType, valid values are: 0=None 1=Odd 2=Even 3=Mark 4=Space  */
		if (cdcLineCoding.bParityType == 0)
			frame |= USART_FRAME_PARITY_NONE;

		else if (cdcLineCoding.bParityType == 1)
			frame |= USART_FRAME_PARITY_ODD;

		else if (cdcLineCoding.bParityType == 2)
			frame |= USART_FRAME_PARITY_EVEN;

		else if (cdcLineCoding.bParityType == 3)
			return USB_STATUS_REQ_ERR;

		else if (cdcLineCoding.bParityType == 4)
			return USB_STATUS_REQ_ERR;

		else
			return USB_STATUS_REQ_ERR;

		/* Check bCharFormat, valid values are: 0=1 1=1.5 2=2 stop bits */
		if (cdcLineCoding.bCharFormat == 0)
			frame |= USART_FRAME_STOPBITS_ONE;

		else if (cdcLineCoding.bCharFormat == 1)
			frame |= USART_FRAME_STOPBITS_ONEANDAHALF;

		else if (cdcLineCoding.bCharFormat == 2)
			frame |= USART_FRAME_STOPBITS_TWO;

		else
			return USB_STATUS_REQ_ERR;

		/* Program new UART baudrate etc. */
		//    CDC_UART->FRAME = frame;
		//    USART_BaudrateAsyncSet(CDC_UART, 0, cdcLineCoding.dwDTERate, usartOVS16);

		return USB_STATUS_OK;
	}
	return USB_STATUS_REQ_ERR;
}

void CDC_puts(const char *s){
	_bytes_to_send = strlen(s);
	_tx_buffer_ptr = (const uint8_t*)s;

	USBD_Write(CDC_EP_DATA_IN, (void*)_tx_buffer_ptr, _bytes_to_send, UsbDataTransmitted_from_ISR);
}
