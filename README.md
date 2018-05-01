# EFM32GG Makefile project
This is a demo project for EFM32 Giant Gecko. The chip enumerates as USB CDC (virtual serial port). Data sent over the USB serial port is sent to RTT debug console and data from the RTT debug console is sent back over USB. This project is fully standalone, can be compiled without Simplicity Studio, just type `make`.

Features and components:
 - Build system based on boilermake
 - Linker script
 - Startup code
 - Header files
 - CMSIS headers
 - emlib (peripherals library)
 - emdrv (peripherals library)
 - usb_gecko (peripherals library)
 - SEGGER RTT

# More information

 - [Tutorial how to assemble a similar firmware project from scratch](https://lb9mg.no/2018/04/30/efm32-cortex-m-firmware-project-from-scratch-step-by-step/)
