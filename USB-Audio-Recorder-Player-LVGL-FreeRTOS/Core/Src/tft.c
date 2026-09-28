#include "tft.h"
#include "main.h"
#include "cmsis_os.h"

#define TFT_WIDTH  240U
#define TFT_HEIGHT 320U

enum {
  TFT_CMD_SLEEP_OUT = 0x11U,
  TFT_CMD_DISPLAY_ON = 0x29U,
  TFT_CMD_COLUMN_ADDRESS = 0x2AU,
  TFT_CMD_PAGE_ADDRESS = 0x2BU,
  TFT_CMD_MEMORY_WRITE = 0x2CU,
  TFT_CMD_MEMORY_ACCESS = 0x36U,
  TFT_CMD_PIXEL_FORMAT = 0x3AU
};

static SPI_HandleTypeDef *tft_spi;
static uint8_t pixel_row[TFT_WIDTH * 2U];

static bool Tft_Command(uint8_t command, const uint8_t *data, uint16_t length)
{
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef status = HAL_SPI_Transmit(tft_spi, &command, 1U, 100U);
  if (status == HAL_OK && length != 0U)
  {
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    status = HAL_SPI_Transmit(tft_spi, data, length, 100U);
  }
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
  return status == HAL_OK;
}

bool Tft_Init(SPI_HandleTypeDef *spi)
{
  tft_spi = spi;
  HAL_GPIO_WritePin(TFT_RES_GPIO_Port, TFT_RES_Pin, GPIO_PIN_RESET);
  osDelay(10U);
  HAL_GPIO_WritePin(TFT_RES_GPIO_Port, TFT_RES_Pin, GPIO_PIN_SET);
  osDelay(120U);

  const uint8_t pixel_format = 0x55U; /* 16-bit RGB565 */
  const uint8_t orientation = 0x48U;  /* Portrait, BGR order */
  if (!Tft_Command(TFT_CMD_PIXEL_FORMAT, &pixel_format, 1U) ||
      !Tft_Command(TFT_CMD_MEMORY_ACCESS, &orientation, 1U) ||
      !Tft_Command(TFT_CMD_SLEEP_OUT, NULL, 0U))
  {
    return false;
  }
  osDelay(120U);
  return Tft_Command(TFT_CMD_DISPLAY_ON, NULL, 0U);
}

bool Tft_FillScreen(uint16_t rgb565)
{
  const uint8_t columns[] = {0U, 0U, 0U, TFT_WIDTH - 1U};
  const uint8_t pages[] = {0U, 0U, (TFT_HEIGHT - 1U) >> 8, (TFT_HEIGHT - 1U) & 0xFFU};
  if (tft_spi == NULL || !Tft_Command(TFT_CMD_COLUMN_ADDRESS, columns, sizeof(columns)) ||
      !Tft_Command(TFT_CMD_PAGE_ADDRESS, pages, sizeof(pages)))
  {
    return false;
  }

  for (uint16_t x = 0; x < TFT_WIDTH; x++)
  {
    pixel_row[2U * x] = rgb565 >> 8;
    pixel_row[2U * x + 1U] = rgb565 & 0xFFU;
  }

  uint8_t command = TFT_CMD_MEMORY_WRITE;
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef status = HAL_SPI_Transmit(tft_spi, &command, 1U, 100U);
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
  for (uint16_t y = 0; y < TFT_HEIGHT && status == HAL_OK; y++)
  {
    status = HAL_SPI_Transmit(tft_spi, pixel_row, sizeof(pixel_row), 100U);
  }
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
  return status == HAL_OK;
}
