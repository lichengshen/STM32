# USB WAV Audio Player (LVGL)

This is a migration of the `USB-Audio-Player` project to use LVGL v9.3.0. We migrated the UI from the custom TFT renderer to LVGL v9.3.0, and we use LVGL's ILI9341 driver. The rotary encoder is also adapted to LVGL as an input device. The behavior of the application is currently unchanged.

A WAV audio player test application. The USB mass storage and file browser interface follows from the `USB-Filesystem` project, while adding audio playback through the on-board CS43L22 codec and headphone jack (CN4). Currently supporting only 16-bit WAV files.

## Supported audio

- RIFF/WAVE, little-endian PCM (`format = 1`), 16-bit mono or stereo.
- 8, 11.025, 16, 22.05, 32, 44.1, and 48 kHz sample rates.
- WAV `fmt ` and `data` chunks may be separated by other chunks; odd-sized
  chunks are handled with RIFF word padding.

## Controls

- Turn encoder in the browser: select a directory or file.
- Press in the browser: enter a directory, go to parent, refresh, or play WAV.
- Turn encoder while playing: adjust volume in 5% steps.
- Press while playing: pause/resume. Pause holds the codec in hardware reset,
  so the headphone amplifier is silent even though I2S remains clock master.
- Hold the button (for 700 ms) while playing: stop and return to the browser.

The playback view incrementally updates elapsed/total time, a progress bar, and
volume/state without full-screen redraws while the DMA stream is active.

## Architecture

USB/FatFs file reads and LVGL run cooperatively in `app_poll()`. LVGL v9.3.0
uses its ILI9341 driver with a small STM32 HAL transport: commands are sent
blocking in 8-bit SPI mode and RGB565 pixels are sent in 16-bit SPI TX DMA
mode. Two 240 x 10-line RGB565 draw buffers, the 32 KiB LVGL allocation, and
the audio DMA buffer all reside in ordinary SRAM, which is accessible by DMA.
`lv_display_flush_ready()` is called only by the SPI completion/error callback.

I2S3 TX uses circular DMA on DMA1 Stream 7, with a 32 KiB ordinary-SRAM buffer
split into two 16 KiB halves. DMA callbacks only record which half became free;
the foreground player refills that half from FatFs. This is the same two-buffer
streaming principle as ST's example, without restarting normal-mode DMA for
each buffer. The DMA buffer resides in normal SRAM, not CCM RAM, because DMA1
cannot access the STM32F407's CCM region.

Note: The Audio_playback_and_record example in STM32Cube uses the ping-pong buffer approach, but uses normal-mode DMA. It uses the STM32F4-Discovery BSP provided in STM32Cube, which in turn uses the CS43L22 component driver. Here we implemented our own CS43L22 driver.

## Hardware

| Part | STM32F407G-DISC1 connection |
| --- | --- |
| ILI9341 SCL | PB3 / SPI1_SCK |
| ILI9341 SDA | PB5 / SPI1_MOSI |
| ILI9341 CS, DC, RESET | PD0, PD1, PD2 |
| HW-040 CLK, DT | PE9 / TIM1_CH1, PE11 / TIM1_CH2 |
| HW-040 switch | PE13, active-low |
| USB flash drive | CN5 through a USB-A-to-micro-USB OTG adapter |
| Audio | headphones on CN4 |

The display and encoder wiring details are also recorded in the repository-wide
`AGENTS.md`.

## CubeMX configuration

We start from the configuration in `USB-Filesystem.ioc`, which already includes the HSE-bypass, SPI1 display, TIM1 encoder, USB OTG FS, and FatFs configurations. We then add:

- I2C1 at 100 kHz on PB6/PB9 for the CS43L22. PD4 output for codec reset.
- I2S3 master transmit on PA4/PC7/PC10/PC12, Philips 16-bit, MCLK enabled.
- DMA1 Stream 7 / Channel 0 for I2S3 TX: circular, half-word transfers, high priority, with half/full-complete interrupts.
- DMA1 NVIC interrupt preemption priority set to 6, giving it lower priority than USB interrupts.
- SPI1 TX on DMA2 Stream 3 / Channel 3: normal, half-word memory/peripheral
  transfers, low priority, with its interrupt priority set below USB and the
  audio DMA interrupt. SPI1 uses the `/4` prescaler (21 MHz).
- Clock tree set to `PLLI2SN = 258` and `PLLI2SR = 3` for the 48 kHz configuration. However, the values of `PLLI2SN` and `PLLI2SR` are reapplied at every `audio_player_start()` to match the WAV file's sample rate.

The USB VBUS active-low fix and the FatFs `hUSB_Host` alias are kept inside
CubeMX user-code sections.

## Main source files

| File | Role |
| --- | --- |
| `Core/Src/app.c` | USB/mount and playback coordinator between the existing audio path and LVGL UI. |
| `Core/Src/lvgl_port.c` | ILI9341 SPI/DMA transport, LVGL tick source, and encoder input device. |
| `Core/Src/ui.c` | LVGL waiting, mount-error, browser, result, and playback screens. |
| `Core/Src/wav.c` | RIFF/WAVE validation and stream metadata parser. |
| `Core/Src/audio_player.c` | Mono-to-stereo PCM preparation, circular-DMA streaming, track state, and DMA callbacks. |
| `Core/Src/codec_cs43l22.c` | CS43L22 reset, I2C setup, mute/pause/resume, and volume control. |
| `Core/Src/encoder.c` | TIM1 encoder steps plus debounced pressed state. |
| `tests/test_wav.c` | Host-side WAV parser tests. |
