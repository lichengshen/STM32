#ifndef TFT_H
#define TFT_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

bool Tft_Init(SPI_HandleTypeDef *spi);
bool Tft_FillScreen(uint16_t rgb565);

#endif /* TFT_H */
