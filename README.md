# STM32 Learning Projects
My STM32 learning projects, based on the STM32F407G-DISC1 discovery kit.
Warning: This is for my own learning purposes. The code may not be optimal, and also includes a lot of vibe coding.

## Hardware
This project uses the STM32F407G-DISC1 discovery kit. It is the MB997E model, with the STM32F407 MCU.

The discovery kit is also connected with a 2.4-inch TFT LCD 240x320 ILI9341 display via SPI. The pins are connected as:
| ILI9341 | Discovery Pin | STM32 Pin |
| ------- | ------------- | --------- |
| GND     | P2-1          | GND       |
| VCC     | P2-5          | 3V        |
| SCL     | P2-28         | PB3       |
| SDA     | P2-26         | PB5       |
| RES     | P2-34         | PD2       |
| DC      | P2-33         | PD1       |
| CS      | P2-36         | PD0       |
| BLK     | P2-6          | 3V        |

An HW-040 rotary encoder is also connected. The pins are connected as:
| HW-040 | Discovery Pin | STM32 Pin       |
| ------ | ------------- | --------------- |
| CLK    | P1-27         | PE9 / TIM1_CH1  |
| DT     | P1-29         | PE11 / TIM1_CH2 |
| SW     | P1-31         | PE13            |
| GND    | P2-2          | GND             |
| +      | P2-4          | 5V              |

A USB flash drive is connected to the discovery kit, plugged into the CN5 connector via a USB A to micro USB adapter.

## Current Projects
- USB-Filesystem: Read/write text files on USB flash drive using FatFs.
- USB-Audio-Player: Play WAV files from USB flash drive.