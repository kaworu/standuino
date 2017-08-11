# Arduino Make file. Refer to https://github.com/sudar/Arduino-Makefile

BOARD_TAG=pro5v328
USER_LIB_PATH=$(realpath .)
ARDUINO_LIBS=	Wire \
		Adafruit-GFX-Library \
		Adafruit_LED_Backpack \
		Arduino-IRremote \
		arduino-softpwm
include /usr/share/arduino/Arduino.mk
