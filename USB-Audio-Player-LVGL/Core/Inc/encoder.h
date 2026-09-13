#ifndef ENCODER_H
#define ENCODER_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

void encoder_init(TIM_HandleTypeDef *timer);
void encoder_poll(void);
int32_t encoder_take_steps(void);
bool encoder_is_pressed(void);

#endif
