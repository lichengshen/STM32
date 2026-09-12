# USB WAV Audio Player

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

USB/FatFs file reads and TFT rendering run cooperatively in `app_poll()`.
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
- Clock tree set to `PLLI2SN = 258` and `PLLI2SR = 3` for the 48 kHz configuration. However, the values of `PLLI2SN` and `PLLI2SR` are reapplied at every `audio_player_start()` to match the WAV file's sample rate.

The USB VBUS active-low fix and the FatFs `hUSB_Host` alias are kept inside
CubeMX user-code sections.

## Main source files

| File | Role |
| --- | --- |
| `Core/Src/app.c` | USB/mount state machine, directory browser, controls, and TFT UI. |
| `Core/Src/wav.c` | RIFF/WAVE validation and stream metadata parser. |
| `Core/Src/audio_player.c` | Mono-to-stereo PCM preparation, circular-DMA streaming, track state, and DMA callbacks. |
| `Core/Src/codec_cs43l22.c` | CS43L22 reset, I2C setup, mute/pause/resume, and volume control. |
| `Core/Src/encoder.c` | Encoder steps plus debounced short and long button events. |
| `tests/test_wav.c` | Host-side WAV parser tests. |
