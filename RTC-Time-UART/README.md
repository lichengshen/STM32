# RTC time over UART

## Overview

This project tests the STM32's real-time clock (RTC), while  the local date and time are synchronized with the PC through UART.

A Python script sends the PC's current local time through a CH340 USB-to-UART adapter. The firmware sets the RTC and displays the date and time as plain text on the ILI9341 LCD. After synchronization, the RTC advances independently of the PC. The running calendar is preserved across resets while the board remains powered, using a marker in an RTC backup register. ~~We use the LSI clock source since the STM32F407G-DISC1 does not have the LSE crystal mounted.~~
We now use the 8 MHz HSE crystal to reduce the drift observed with LSI. To do: Measure the LSI frequency with TIM5 channel 4, see page 160 of the RM0090 manual.

## Usage

Install the sender dependency:
```sh
python3 -m venv .venv
.venv/bin/python -m pip install pyserial
```

Send the PC's current local time (replace the port as needed):
```sh
.venv/bin/python scripts/set_time.py /dev/ttyUSB0
```

## UART protocol

The serial protocol is 115200 baud, 8N1, no flow control:
```text
SET 2026-10-07 12:34:56 3
```

The last field is the weekday: Monday=1 through Sunday=7. Terminate the command with LF or CRLF. After successfully setting the time and date, the STM32 sends back `OK\r\n`. The Python script waits up to three seconds for the acknowledgment.

## Code organization

The custom ILI9341 driver (`tft.c` and `tft.h`) was copied from `../USB-Audio-Player`. The UART processing, RTC setting, and LCD updates are implemented in `rtc_app.c` and `rtc_app.h`. In `main.c`, default calendar assignment is skipped in `Check_RTC_BKUP` when the marker is present.
