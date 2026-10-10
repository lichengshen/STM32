#ifndef ENCODER_H
#define ENCODER_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

bool Encoder_Init(TIM_HandleTypeDef *timer);
/* Raw signed timer counts since the previous read, including wraparound. */
int16_t Encoder_ReadDelta(void);

/* Poll in UiTask; the getter returns the debounced button state. */
void Encoder_PollButton(void);
bool Encoder_IsPressed(void);

#endif /* ENCODER_H */
