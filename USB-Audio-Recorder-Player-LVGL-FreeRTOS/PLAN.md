# Small, reviewable implementation checkpoints

## Approach

Keep each checkpoint to roughly **50–100 lines of handwritten code**, including any test additions. CubeMX-generated code and third-party libraries are reviewed separately. If an integrated change grows beyond that size, split it before implementation. After every checkpoint, show the diff and a focused check; run a board smoke test even when the new logic is best checked on the host.

Use your proposed `UiTask`, `AudioTask`, and `StorageTask`. Each task owns one subsystem: LVGL and encoder; codec, I2S, DMA, and microphone conversion; USB host, FatFs, and WAV files. Fixed-size queues connect them.

## Checkpoints

**Foundation and interface**

| # | Change | CubeMX before this checkpoint | Focused check |
| --- | --- | --- | --- |
| 1 | Generate the scaffold and start the three application tasks with a heartbeat. | Board selector, CMake, start with CubeMX's HSE crystal/ceramic setting, 168 MHz/USB 48 MHz clocks, SWD, TIM6 HAL timebase, FreeRTOS CMSIS v2. The scaffold also enables USB Host MSC, which creates a fourth, library-owned task. Confirm the selected HSE path on the actual board; bypass is for the ST-LINK MCO path. | Three application LEDs blink; HAL and RTOS ticks advance. |
| 2a | Create the first bounded UI event queue and pass startup events from audio and storage. | Add `UiEventQueue`, eight `AppEvent` items, dynamic allocation in FreeRTOS. | Blue LED turns on after both events; existing heartbeats continue. |
| 2b | Measure queue, heap, and task stack use, including the USB helper task, and set the RAM budget. | Set the USB Host process stack to 1024 bytes after measuring only 76 bytes spare at 512. | Inspect measured headroom with and without a USB drive. |
| 3 | Initialize the LCD and send basic SPI commands. | SPI1 PB3/PB5; PD0/PD1/PD2 outputs. | Show solid colors on the panel. |
| 4 | Add LVGL display buffers and SPI TX DMA flushing. | DMA2 Stream3 Channel 3, normal mode. | Repeated redraws finish without a stuck flush. |
| 5 | Read encoder turns and debounced presses. | TIM1 encoder PE9/PE11 with filters; PE13 pull-up input. | Show turn and press counts on the LCD. |
| 6 | Build the waiting, status, and error screen primitives. | None. | Check text, focus, and long-press timing. |

**USB browser and WAV identification**

| # | Change | CubeMX before this checkpoint | Focused check |
| --- | --- | --- | --- |
| 7 | Process USB Host connection changes and validate the USB helper task's stack under load. | Verify USB OTG FS host MSC and PC0 VBUS output. | Insertion and removal change the displayed connection state; stack headroom remains adequate. |
| 8 | Mount and unmount a USB drive in `StorageTask`. | FatFs USB disk driver and long filenames. | Mount a FAT32 drive; remove and reinsert it. |
| 9 | Scan one directory into a bounded, sorted entry list. | None. | Show entry count and order from a prepared drive. |
| 10 | Add browser rows, refresh, parent navigation, and encoder selection. | None. | Browse nested folders and return to the root. |
| 11 | Parse RIFF chunks, including padding and chunk boundaries. | None. | Host tests with valid and malformed chunk layouts. |
| 12 | Validate PCM format, channels, sample rate, and data length. | None. | Accept the reference player’s formats and explain rejected files on the board. |

**Playback**

| # | Change | CubeMX before this checkpoint | Focused check |
| --- | --- | --- | --- |
| 13 | Initialize the codec and set a safe volume. | I2C1 PB6/PB9; PD4 codec reset. | Confirm codec communication and quiet startup. |
| 14 | Configure I2S3 and play a short in-memory test tone. | I2S3 master TX, PA4/PC7/PC10/PC12. | Hear the tone at CN4; verify its rate. |
| 15 | Move the test tone to circular I2S DMA. | DMA1 Stream7 Channel 0, circular half-word transfers. | Continuous tone with half/full DMA events. |
| 16 | Add fixed-size block ownership between storage and audio tasks. | None. | Stream the in-memory tone through the block queue. |
| 17 | Open a WAV and prefetch its PCM blocks from USB. | None. | Inspect ordered blocks from a known WAV. |
| 18 | Refill DMA from blocks and stop at the exact end of the file. | None. | Play short and long WAVs without repetition or clipped endings. |
| 19 | Add volume, pause/resume, and stop actions. | None. | Match the reference encoder controls. |
| 20 | Add time/progress updates and playback error handling. | None. | Check progress, unsupported media, underrun, and removal during playback. |

**Playback visualization**

| # | Change | CubeMX before this checkpoint | Focused check |
| --- | --- | --- | --- |
| 21 | Publish a bounded snapshot of played PCM for the UI. | None. | Check sample values without affecting DMA deadlines. |
| 22 | Use CMSIS-DSP to convert snapshots into 12 frequency-band levels. | None. | Host tests with silence and known-frequency tones. |
| 23 | Draw and update frequency bars in the playback screen. | None. | Bars respond to music, settle on pause, and remain smooth during playback. |

**Recording**

| # | Change | CubeMX before this checkpoint | Focused check |
| --- | --- | --- | --- |
| 24 | Capture raw microphone PDM through circular DMA. | I2S2 master RX PB10/PC3 at nominal 64 kHz I2S frame rate; DMA1 Stream3 Channel 0. | Verify the roughly 2 MHz mic clock and changing PDM data. |
| 25 | Convert PDM to 16 kHz mono PCM with ST’s library. | Enable CRC, then PDM2PCM middleware with 128:1 decimation. | Inspect speech samples and conversion time; check for overruns. The E-series microphone’s normal clock minimum is 1.2 MHz. [Board manual](https://www.st.com/resource/en/user_manual/um1472-discovery-kit-with-stm32f407vg-mcu-stmicroelectronics.pdf), [microphone datasheet](https://www.st.com/resource/en/datasheet/imp34dt05.pdf), [PDM2PCM manual](https://www.st.com/resource/en/user_manual/um2372-stm32cube-pdm2pcm-software-library-for-the-stm32f4f7h7-series-stmicroelectronics.pdf). |
| 26 | Pass captured PCM blocks to `StorageTask` with explicit full-buffer handling. | None. | Confirm block order and a visible overrun error under an induced stall. |
| 27 | Choose a free `REC0001`-style name in the current folder and write a temporary WAV header. | Verify FatFs write support. | Check that existing files are never replaced. |
| 28 | Drain PCM blocks into the temporary file. | None. | Inspect recorded data and sustained write rate on a PC. |
| 29 | Stop capture, drain remaining blocks, fix the header, close, and rename to `.WAV`. | None. | Open the result in a PC player and this device’s browser. |
| 30 | Add the browser **Record WAV** action and recording screen; press starts and press stops. | None. | Perform the complete recording flow with the encoder. |
| 31 | Handle full drive, USB removal, and repeated play/record cycles. | None. | Confirm useful errors, no overwritten WAV, and no task or buffer failures. |

## Checkpoint 2b RAM baseline

Debug build with a USB drive enumerated (`APPLICATION_READY`, `HOST_CLASS`):

| Resource | Allocated | Measured |
| --- | ---: | ---: |
| FreeRTOS heap | 24,576 B | 8,456 B minimum free |
| C heap | Grows above linker `_end` | 268 B peak growth |
| Storage task stack | 6,144 B | 5,872 B minimum unused |
| Audio task stack | 4,096 B | 3,924 B minimum unused |
| UI task stack | 4,096 B | 3,896 B minimum unused |
| USB host task stack | 1,024 B | 588 B minimum unused |
| UI event queue payload | 8 × 8 B | 64 B |

The linker uses 32,432 B of the 128 KiB main RAM region, including the fixed FreeRTOS heap and linker heap/stack reservations. The C heap measurement tracks runtime growth; do not add it directly to the linker figure. Treat the 8,456 B of unallocated FreeRTOS heap as the current dynamic allocation budget. Recheck stack and heap headroom as LCD, file I/O, playback, and recording workloads are added.

## Defaults and gates

Record 16-bit mono PCM at 16 kHz into the current USB folder. Playback and recording are mutually exclusive. Frequency bars appear only during playback. An interrupted recording retains a temporary file rather than appearing as a valid WAV.

Before coding each checkpoint, inspect the actual generated `.ioc`, provide the precise CubeMX changes for that checkpoint, and wait for your regenerated files. Advance only after its diff review and focused hardware check.
