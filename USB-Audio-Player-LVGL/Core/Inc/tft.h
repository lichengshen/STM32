#ifndef TFT_H
#define TFT_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#define TFT_WIDTH  240u
#define TFT_HEIGHT 320u

#define TFT_BLACK   0x0000u
#define TFT_WHITE   0xffffu
#define TFT_BLUE    0x001fu
#define TFT_GREEN   0x07e0u
#define TFT_RED     0xf800u
#define TFT_YELLOW  0xffe0u
#define TFT_CYAN    0x07ffu
#define TFT_GRAY    0x7befu

bool tft_init(SPI_HandleTypeDef *spi);
void tft_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                   uint16_t color);
void tft_clear(uint16_t color);
void tft_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t color,
                   uint8_t scale);

#endif
