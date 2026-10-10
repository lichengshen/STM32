#include "encoder.h"
#include "main.h"

#define ENCODER_BUTTON_DEBOUNCE_MS 20U

static TIM_HandleTypeDef *encoder_timer;
static uint16_t previous_count;
static bool button_candidate;
static bool button_pressed;
static uint32_t button_change_tick;

bool Encoder_Init(TIM_HandleTypeDef *timer)
{
  if (HAL_TIM_Encoder_Start(timer, TIM_CHANNEL_ALL) != HAL_OK)
  {
    return false;
  }
  encoder_timer = timer;
  previous_count = (uint16_t)__HAL_TIM_GET_COUNTER(timer);
  button_candidate = (HAL_GPIO_ReadPin(ENCODER_SW_GPIO_Port, ENCODER_SW_Pin) == GPIO_PIN_RESET);
  button_pressed = false;
  button_change_tick = HAL_GetTick();
  return true;
}

int16_t Encoder_ReadDelta(void)
{
  uint16_t count = (uint16_t)__HAL_TIM_GET_COUNTER(encoder_timer);
  /* Signed 16-bit subtraction handles either direction across 0/65535. */
  int16_t delta = (int16_t)(count - previous_count);
  previous_count = count;
  return delta;
}

void Encoder_PollButton(void)
{
  bool pressed = (HAL_GPIO_ReadPin(ENCODER_SW_GPIO_Port, ENCODER_SW_Pin) == GPIO_PIN_RESET);
  uint32_t now = HAL_GetTick();

  if (pressed != button_candidate)
  {
    button_candidate = pressed;
    button_change_tick = now;
  }
  else if (now - button_change_tick >= ENCODER_BUTTON_DEBOUNCE_MS)
  {
    /* Accept either edge only after the sampled level stays unchanged. */
    button_pressed = button_candidate;
  }
}

bool Encoder_IsPressed(void)
{
  return button_pressed;
}
