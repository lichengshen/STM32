#ifndef ENCODER_H
#define ENCODER_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  ENCODER_BUTTON_NONE = 0,
  ENCODER_BUTTON_SHORT,
  ENCODER_BUTTON_LONG,
} EncoderButtonEvent;

void encoder_init(TIM_HandleTypeDef *timer);
int32_t encoder_take_steps(void);
EncoderButtonEvent encoder_take_button_event(void);

#endif
