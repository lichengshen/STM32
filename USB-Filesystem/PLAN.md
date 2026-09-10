# USB FAT32 File Browser and Write Test

## CubeMX configuration

- Start with **Board Selector → STM32F407G-DISC1** and answer **No** to “Initialize all peripherals with their default mode.” Set CMake as the toolchain, copy used libraries into the project, and enable **Keep User Code**.
- Configure the board’s 8 MHz HSE crystal with PLLM=8, PLLN=336, PLLP=2, PLLQ=7, yielding 168 MHz CPU and a 48 MHz USB clock. Set `SYS → Debug: Serial Wire` only, freeing PB3 for the LCD.
- Configure the LCD:
  - `SPI1 → Transmit Only Master`, PB3=SCK and PB5=MOSI, Mode 0, 8-bit/MSB-first, software NSS, prescaler 16.
  - PD0/PD1/PD2 as low-speed GPIO outputs, initially high, labelled `TFT_CS`, `TFT_DC`, and `TFT_RES`.
- Configure the encoder:
  - `TIM1 → Encoder Mode TI12`, PE9/PE11, **No Pull**, prescaler 0, period 65535, rising edges, no timer interrupt.
  - PE13 as GPIO input with internal pull-up for the active-low button; debounce in software.
- Configure USB:
  - `USB_OTG_FS → Host Only`, enable VBUS activation and the global interrupt.
  - `USB_HOST → Mass Storage Host Class` for FS (`MSC_FS`).
  - In USB Host platform settings, set `Drive_VBUS_FS` to GPIO output **PC0**.
  - `FATFS → USB Disk`; enable long filenames with a static 255-character buffer and leave writes enabled.
  - Set heap to 4 KiB and stack to 8 KiB.
- After generation, verify the USB VBUS hook drives PC0 **low** for VBUS on—the Discovery board’s CN5 load switch is active-low.

## Implementation

- Use Cube HAL plus generated USB Host MSC and FatFs middleware only: no LVGL, FreeRTOS, USBX, or external drivers.
- Add a small HAL-based ILI9341 drawing driver, printable-ASCII font, encoder input module, and filesystem browser module.
- Service the generated USB host process continuously in the main loop; mount only after MSC is ready, and unmount/reset browser state on removal.
- Display a scrollable browser with directory traversal, `.txt` file preview, and clear folder/file/error status. Rotation changes selection or preview page; a debounced press selects or returns.
- Add `[WRITE + VERIFY]`, which exclusively creates/overwrites `/STM32_TEST.TXT` with a readable, deterministic 1 KiB pattern; syncs, closes, reopens, and byte-compares it.

## Test plan

- Verify USB insertion/removal, FAT32 mount/unmount, root and nested-directory navigation, and ASCII text preview.
- Run write/verify and inspect `STM32_TEST.TXT` on a computer.
- Show recoverable errors for full, write-protected, unsupported, or disconnected media.
- Confirm SWD remains usable and the LCD works on SPI1.

## Assumptions

- The USB drive is a single FAT32 mass-storage device on CN5 and the board is powered independently through CN1.
- Only the dedicated `STM32_TEST.TXT` is modified; supplied test files remain untouched.
