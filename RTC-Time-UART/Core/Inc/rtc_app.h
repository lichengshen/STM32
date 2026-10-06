#ifndef RTC_APP_H
#define RTC_APP_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>

bool rtc_app_has_time(RTC_HandleTypeDef *rtc);
void rtc_app_init(RTC_HandleTypeDef *rtc, UART_HandleTypeDef *uart,
                  SPI_HandleTypeDef *spi);
void rtc_app_process(void);

#endif
