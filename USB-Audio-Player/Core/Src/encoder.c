#include "encoder.h"

#include "main.h"

#define ENCODER_COUNTS_PER_STEP 2
#define BUTTON_DEBOUNCE_MS      20u
#define BUTTON_LONG_PRESS_MS    700u

static TIM_HandleTypeDef *encoder_timer;
static uint16_t last_count;
static int32_t count_remainder;
static bool candidate_pressed;
static bool stable_pressed;
static bool long_reported;
static uint32_t candidate_since;
static uint32_t pressed_since;
static EncoderButtonEvent button_event;

void encoder_init(TIM_HandleTypeDef *timer)
{
  encoder_timer = timer;
  (void)HAL_TIM_Encoder_Start(timer, TIM_CHANNEL_ALL);
  last_count = (uint16_t)__HAL_TIM_GET_COUNTER(timer);
  count_remainder = 0;

  candidate_pressed = HAL_GPIO_ReadPin(ENC_SW_GPIO_Port, ENC_SW_Pin) == GPIO_PIN_RESET;
  stable_pressed = candidate_pressed;
  long_reported = false;
  candidate_since = HAL_GetTick();
  pressed_since = candidate_since;
  button_event = ENCODER_BUTTON_NONE;
}

int32_t encoder_take_steps(void)
{
  if (encoder_timer == NULL) return 0;
  const uint16_t count = (uint16_t)__HAL_TIM_GET_COUNTER(encoder_timer);
  const int16_t delta = (int16_t)(count - last_count);
  last_count = count;
  count_remainder += delta;

  int32_t steps = 0;
  while (count_remainder >= ENCODER_COUNTS_PER_STEP) {
    count_remainder -= ENCODER_COUNTS_PER_STEP;
    ++steps;
  }
  while (count_remainder <= -ENCODER_COUNTS_PER_STEP) {
    count_remainder += ENCODER_COUNTS_PER_STEP;
    --steps;
  }
  return steps;
}

EncoderButtonEvent encoder_take_button_event(void)
{
  const bool raw_pressed = HAL_GPIO_ReadPin(ENC_SW_GPIO_Port, ENC_SW_Pin) == GPIO_PIN_RESET;
  const uint32_t now = HAL_GetTick();
  if (raw_pressed != candidate_pressed) {
    candidate_pressed = raw_pressed;
    candidate_since = now;
  }
  if (candidate_pressed != stable_pressed &&
      (uint32_t)(now - candidate_since) >= BUTTON_DEBOUNCE_MS) {
    stable_pressed = candidate_pressed;
    if (stable_pressed) {
      pressed_since = now;
      long_reported = false;
    } else if (!long_reported) {
      button_event = ENCODER_BUTTON_SHORT;
    }
  }

  if (stable_pressed && !long_reported &&
      (uint32_t)(now - pressed_since) >= BUTTON_LONG_PRESS_MS) {
    long_reported = true;
    button_event = ENCODER_BUTTON_LONG;
  }

  const EncoderButtonEvent event = button_event;
  button_event = ENCODER_BUTTON_NONE;
  return event;
}
