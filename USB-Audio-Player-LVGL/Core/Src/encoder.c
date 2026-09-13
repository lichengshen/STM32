#include "encoder.h"

#include "main.h"

#define ENCODER_COUNTS_PER_STEP 2
#define BUTTON_DEBOUNCE_MS      20u

static TIM_HandleTypeDef *encoder_timer;
static uint16_t last_count;
static int32_t count_remainder;
static bool candidate_pressed;
static bool stable_pressed;
static uint32_t candidate_since;

void encoder_init(TIM_HandleTypeDef *timer)
{
  encoder_timer = timer;
  (void)HAL_TIM_Encoder_Start(timer, TIM_CHANNEL_ALL);
  last_count = (uint16_t)__HAL_TIM_GET_COUNTER(timer);
  count_remainder = 0;

  candidate_pressed = HAL_GPIO_ReadPin(ENC_SW_GPIO_Port, ENC_SW_Pin) == GPIO_PIN_RESET;
  stable_pressed = candidate_pressed;
  candidate_since = HAL_GetTick();
}

void encoder_poll(void)
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
  }
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

bool encoder_is_pressed(void)
{
  return stable_pressed;
}
