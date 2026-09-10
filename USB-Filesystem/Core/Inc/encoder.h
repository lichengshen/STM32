#ifndef ENCODER_H
#define ENCODER_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

void encoder_init(TIM_HandleTypeDef *timer);
int32_t encoder_take_steps(void);
bool encoder_take_press(void);

#endif
