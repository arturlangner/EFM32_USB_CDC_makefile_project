#include <cdc.h>
#include <em_cmu.h> //clock library
#include <em_gpio.h>
#include <em_usbhal.h>
#include <em_usbd.h>
#include <SEGGER/SEGGER_RTT.h>

int main(void){
	SEGGER_RTT_WriteString(0, "Hello from EFM32!");

	CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFXO);

	CMU_ClockEnable(cmuClock_HFPER, true);
	CMU_ClockEnable(cmuClock_GPIO, true);

	CDC_Init();

	//A clean disconnect is recommended when the device
	//is connected all the time and controlled by the debugger.
	USBD_Disconnect();
	USBTIMER_DelayMs(1000);
	USBD_Connect();

	while (1){
		uint8_t buffer[32];
		unsigned int bytes_read = SEGGER_RTT_ReadNoLock(0, buffer, sizeof(buffer)-1);
		buffer[sizeof(buffer)-1] = '\0'; //make sure that the buffer is terminated

		if (bytes_read > 0){
			CDC_puts((char*)buffer);
		}

		__WFE();
	}
	return 0;
}
