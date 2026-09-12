# USB WAV Audio Player for STM32F407G-DISC1

## Summary

Build a bare-metal USB audio player that reuses the working USB/FatFs, ILI9341, and rotary-encoder approach from `../USB-Filesystem`. It will play PCM WAV files through the Discovery board’s CS43L22 headphone output using I2S3 TX with circular DMA.

Do not use LVGL or FreeRTOS for v1. Follow the ST example’s USB → RAM ping-pong buffer → I2S DMA architecture, but implement a clean streaming player rather than copying its fixed-file and fixed-header logic. MP3 and other compressed formats are deferred behind a future decoder interface.

## CubeMX Template Setup

- Start by saving a copy of `../USB-Filesystem/USB-Filesystem.ioc` as this project’s `.ioc`, rename the project to `USB-Audio-Player`, then generate a CMake project from CubeMX.
- Preserve the known-good configuration:
  - HSE bypass, 168 MHz core clock, SWD debugging.
  - USB OTG FS host with MSC middleware and FatFs USB disk driver; enable long filenames.
  - SPI1 TX-only on PB3/PB5 plus PD0/PD1/PD2 for the ILI9341.
  - TIM1 encoder mode on PE9/PE11, filter 15; PE13 pull-up switch.
  - PC0 USB VBUS switch, with the active-low `MX_DriverVbusFS()` correction retained in its CubeMX user-code section.
- Add audio output only:
  - I2C1 on PB6/PB9 at 100 kHz for CS43L22 control.
  - PD4 as `CODEC_RESET` GPIO output.
  - SPI3 configured as I2S3 master transmit: Philips I2S, 16-bit data, low clock polarity, MCLK enabled; PA4/PC7/PC10/PC12.
  - I2S3 TX DMA as circular, memory-to-peripheral, half-word aligned, with half/full-complete interrupts; prioritize OTG FS above audio DMA.
- Keep microphone/I2S2, LVGL, and FreeRTOS disabled.
- Keep all application code outside generated files; preserve generated-file adjustments only in CubeMX user-code regions, including the USB host-handle alias needed by the FatFs disk driver.

## Implementation Changes

- Reuse and adapt the TFT and encoder modules; extend encoder input to emit short-press and one-shot 700 ms long-press events.
- Replace the text-file UI with a cooperative state machine: waiting for USB, mounting, directory browser, now-playing, and recoverable error status.
  - Browse directories first, then files sorted case-insensitively; retain the existing 64-entry bound and long-filename support.
  - Mark playable `.wav` files; selecting another file reports that its format is unsupported.
  - Short press enters a folder or starts the selected WAV; parent navigation is explicit.
- Add a RIFF/WAVE parser API returning stream metadata (`data` offset/size, sample rate, channels, frame count).
  - Scan chunks and honor word padding rather than assuming a 44-byte header.
  - Accept only little-endian PCM format, 16 bits per sample, mono or stereo, at 8, 11.025, 16, 22.05, 32, 44.1, or 48 kHz.
  - Reject malformed, truncated, unsupported-rate, compressed, float, multichannel, or non-16-bit files before enabling DMA.
- Add a CS43L22 output module using CubeMX-generated I2C/I2S handles. Adapt the proven codec reset, initialization, pause/resume, mute, and volume-register behavior from ST’s board driver, without importing the legacy BSP’s separate clock/MSP ownership.
  - Reconfigure PLLI2S and I2S only between tracks using the ST-proven per-rate settings.
  - Default volume is 70%; encoder rotation adjusts it in 5% steps.
- Add a WAV streaming player with two 16 KiB DMA halves in normal SRAM (never CCM RAM).
  - Pre-fill both halves, start circular I2S DMA, and refill only the half reported free by DMA.
  - Copy stereo PCM directly; expand mono PCM backward in its destination half; zero-pad the final half.
  - DMA callbacks only latch half-complete/error state. `app_poll()` performs FatFs reads, conversion, UI updates, and cleanup.
  - Detect a missed refill, FatFs read failure, codec failure, or USB disconnect; stop DMA safely, close/unmount as applicable, and show an error instead of continuing corrupted playback.
- Implement the now-playing screen without full-screen redraws while DMA is active: filename, pause/play state, volume, elapsed/total time, progress bar, and an incrementally drawn rolling waveform refreshed at most 20 Hz.
  - Press toggles pause/resume; long-press stops playback and returns to the selected browser entry.
  - End of track stops cleanly and returns to the browser; no autoplay, playlist, or seeking in v1.

## Test Plan

- Add host-side parser tests for valid mono/stereo WAVs with extra chunks, odd chunk padding, unsupported encodings, invalid headers, truncated data, and each supported sample rate.
- Build the CMake firmware and inspect the map file; keep DMA-accessible RAM usage below 112 KiB, leaving at least 16 KiB headroom.
- On hardware, verify:
  - USB insertion, mount, nested-directory navigation, long names, unsupported-file feedback, removal, and reinsertion.
  - Clean headphone playback for mono and stereo WAVs at 16, 44.1, and 48 kHz.
  - Volume, pause/resume, stop, progress/time display, and waveform updates during sustained playback.
  - Graceful recovery from a removed drive during playback and deliberately invalid WAV files.

## Assumptions

- The test drive is FAT32 with an MBR, as used by the working USB project.
- Output is through headphones on CN4; the player is read-only.
- MP3, AAC, FLAC, WAV ADPCM, playlists, seeking, recording, and persistent settings are explicitly future work. A later decoder can feed the same PCM streaming interface.
