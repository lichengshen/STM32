Below provides the development environment and information common to all projects in this repository. Each project may have additional information specific to that project.

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

## Code Template
Unless otherwise specified, the project code template was generated with STM32CubeMX using the Board Selector and choosing STM32F407G-DISC1, with all peripherals initialized with default mode. Toolchain/IDE is set to CMake.

Note that we may need to regenerate code if we need to change the configuration of the peripherals. Make sure the code will not get overwritten if we regenerate code later. Before implementing a feature, first check if any configuration changes are needed. If any changes are needed, do not manually modify the .ioc file. STM32CubeMX should be used to reconfigure and regenerate the code.

When some functionality has a well-established library available, prefer using the libraries instead of implementing the functionality from scratch.

## Datasheets
Some manuals/datasheets that may be useful are included in `Datasheets/`. These include:
- RM0090: STM32F4 MCU reference manual
- PM0214: STM32 Cortex-M4 MCU and MPU programming manual
- UM1472: Discovery kit with STM32F407VG MCU user manual
- UM1725: STM32F4 HAL and LL user manual
- dm00037051: STM32F405xx / STM32F407xx datasheet
If additional information or manuals are needed, search for them online.

## Building and Flashing
In VSCode, the project can be built and flashed to the discovery kit by running Ctrl+Shift+P → Tasks: Run Task → STM32: Build, Flash and Run. This is set in `~/.config/Code/User/tasks.json`. A copy of the file is also included as `tasks.json` here. If necessary, consider adding new script files to build and flash the project from the command line.