#include "lvgl_port.h"

#include "encoder.h"
#include "main.h"

#include <stddef.h>

#define LCD_WIDTH 240u
#define LCD_HEIGHT 320u
#define LCD_DRAW_LINES 10u
#define LCD_DRAW_BUFFER_BYTES (LCD_WIDTH * LCD_DRAW_LINES * sizeof(uint16_t))
#define LCD_SPI_TIMEOUT_MS 100u
#define ENCODER_LONG_PRESS_MS 700u

extern SPI_HandleTypeDef hspi1;

static uint16_t draw_buffer_1[LCD_WIDTH * LCD_DRAW_LINES] __attribute__((aligned(4)));
static uint16_t draw_buffer_2[LCD_WIDTH * LCD_DRAW_LINES] __attribute__((aligned(4)));
static lv_display_t *active_display;
static lv_group_t *encoder_group;
static volatile bool color_transfer_active;
static LvglPortEncoderTurnCb encoder_turn_callback;

static uint32_t lvgl_tick_get(void)
{
  return HAL_GetTick();
}

static void lcd_deselect(void)
{
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

static void lcd_select(void)
{
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
}

static bool spi_set_data_size(uint32_t data_size)
{
  if (hspi1.Init.DataSize == data_size) return true;
  if (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) return false;
  hspi1.Init.DataSize = data_size;
  return HAL_SPI_Init(&hspi1) == HAL_OK;
}

static void wait_for_color_transfer(void)
{
  while (color_transfer_active) {
  }
}

static void finish_color_transfer(void)
{
  lv_display_t *display = active_display;
  active_display = NULL;
  color_transfer_active = false;
  lcd_deselect();
  if (display != NULL) lv_display_flush_ready(display);
}

static bool transmit_blocking(const uint8_t *data, size_t size)
{
  if (size == 0u) return true;
  return HAL_SPI_Transmit(&hspi1, (uint8_t *)data, (uint16_t)size,
                          LCD_SPI_TIMEOUT_MS) == HAL_OK;
}

static void lcd_send_cmd(lv_display_t *display, const uint8_t *cmd, size_t cmd_size,
                         const uint8_t *param, size_t param_size)
{
  LV_UNUSED(display);
  wait_for_color_transfer();
  if (!spi_set_data_size(SPI_DATASIZE_8BIT)) return;

  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
  lcd_select();
  bool sent = transmit_blocking(cmd, cmd_size);
  if (sent && param_size != 0u) {
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    sent = transmit_blocking(param, param_size);
  }
  (void)sent;
  lcd_deselect();
}

static void lcd_send_color(lv_display_t *display, const uint8_t *cmd, size_t cmd_size,
                           uint8_t *param, size_t param_size)
{
  wait_for_color_transfer();
  if ((param_size & 1u) != 0u || !spi_set_data_size(SPI_DATASIZE_8BIT)) {
    active_display = display;
    finish_color_transfer();
    return;
  }

  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
  lcd_select();
  if (!transmit_blocking(cmd, cmd_size) || !spi_set_data_size(SPI_DATASIZE_16BIT)) {
    active_display = display;
    finish_color_transfer();
    return;
  }

  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
  active_display = display;
  color_transfer_active = true;
  if (HAL_SPI_Transmit_DMA(&hspi1, param, (uint16_t)(param_size / 2u)) != HAL_OK)
    finish_color_transfer();
}

static void encoder_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  LV_UNUSED(indev);
  const int32_t steps = encoder_take_steps();
  if (encoder_turn_callback != NULL) {
    encoder_turn_callback(steps);
    data->enc_diff = 0;
  } else if (steps > INT16_MAX) {
    data->enc_diff = INT16_MAX;
  } else if (steps < INT16_MIN) {
    data->enc_diff = INT16_MIN;
  } else {
    data->enc_diff = (int16_t)steps;
  }
  data->state = encoder_is_pressed() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool lvgl_port_init(void)
{
  lv_tick_set_cb(lvgl_tick_get);

  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(TFT_RES_GPIO_Port, TFT_RES_Pin, GPIO_PIN_SET);
  HAL_Delay(5u);
  HAL_GPIO_WritePin(TFT_RES_GPIO_Port, TFT_RES_Pin, GPIO_PIN_RESET);
  HAL_Delay(20u);
  HAL_GPIO_WritePin(TFT_RES_GPIO_Port, TFT_RES_Pin, GPIO_PIN_SET);
  HAL_Delay(150u);

  /* The panel's native portrait scan direction and RGB color order match the
   * LVGL framebuffer.  BGR made cyan title text appear yellow, and MIRROR_X
   * flipped the browser horizontally on the fitted ILI9341 module. */
  lv_display_t *display = lv_ili9341_create(LCD_WIDTH, LCD_HEIGHT, 0u,
                                             lcd_send_cmd, lcd_send_color);
  if (display == NULL) return false;
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, draw_buffer_1, draw_buffer_2, LCD_DRAW_BUFFER_BYTES,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  encoder_group = lv_group_create();
  lv_indev_t *encoder_indev = lv_indev_create();
  if (encoder_group == NULL || encoder_indev == NULL) return false;
  lv_indev_set_type(encoder_indev, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_read_cb(encoder_indev, encoder_read);
  lv_indev_set_group(encoder_indev, encoder_group);
  lv_indev_set_long_press_time(encoder_indev, ENCODER_LONG_PRESS_MS);
  return true;
}

lv_group_t *lvgl_port_encoder_group(void)
{
  return encoder_group;
}

void lvgl_port_set_encoder_turn_cb(LvglPortEncoderTurnCb callback)
{
  encoder_turn_callback = callback;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi == &hspi1 && color_transfer_active) finish_color_transfer();
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi == &hspi1 && color_transfer_active) finish_color_transfer();
}
