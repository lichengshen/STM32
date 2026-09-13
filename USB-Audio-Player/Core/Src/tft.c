#include "tft.h"

#include "main.h"

#include <stddef.h>

static SPI_HandleTypeDef *tft_spi;
static uint8_t line_buffer[TFT_WIDTH * 2u];

/* The display-facing glyph set deliberately remains small. Unsupported
 * printable characters become '?', while lower-case text is legible as upper
 * case. This covers FAT names and the ASCII test files without a font library. */
static const uint8_t font5x7[37][5] = {
    {0, 0, 0, 0, 0},
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31},
    {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e},
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c},
    {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0x00, 0x41, 0x7f, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3f, 0x01},
    {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06},
    {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f},
    {0x7f, 0x20, 0x18, 0x20, 0x7f}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x03, 0x04, 0x78, 0x04, 0x03}, {0x61, 0x51, 0x49, 0x45, 0x43},
};

static const uint8_t glyph_dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
static const uint8_t glyph_slash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
static const uint8_t glyph_dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
static const uint8_t glyph_under[5] = {0x40, 0x40, 0x40, 0x40, 0x40};
static const uint8_t glyph_colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
static const uint8_t glyph_plus[5] = {0x08, 0x08, 0x3e, 0x08, 0x08};
static const uint8_t glyph_equal[5] = {0x14, 0x14, 0x14, 0x14, 0x14};
static const uint8_t glyph_lbracket[5] = {0x00, 0x7f, 0x41, 0x41, 0x00};
static const uint8_t glyph_rbracket[5] = {0x00, 0x41, 0x41, 0x7f, 0x00};
static const uint8_t glyph_question[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
static const uint8_t glyph_bang[5] = {0x00, 0x00, 0x5f, 0x00, 0x00};
static const uint8_t glyph_comma[5] = {0x00, 0x80, 0x60, 0x00, 0x00};
static const uint8_t glyph_lparen[5] = {0x00, 0x1c, 0x22, 0x41, 0x00};
static const uint8_t glyph_rparen[5] = {0x00, 0x41, 0x22, 0x1c, 0x00};

static bool spi_send(const uint8_t *data, uint16_t length)
{
  return HAL_SPI_Transmit(tft_spi, data, length, 100u) == HAL_OK;
}

static bool command(uint8_t value, const uint8_t *data, uint16_t length)
{
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
  bool ok = spi_send(&value, 1u);
  if (ok && length != 0u) {
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    ok = spi_send(data, length);
  }
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
  return ok;
}

static bool begin_memory_write(uint16_t x, uint16_t y, uint16_t width,
                               uint16_t height)
{
  const uint16_t x2 = (uint16_t)(x + width - 1u);
  const uint16_t y2 = (uint16_t)(y + height - 1u);
  const uint8_t columns[] = {(uint8_t)(x >> 8), (uint8_t)x,
                             (uint8_t)(x2 >> 8), (uint8_t)x2};
  const uint8_t rows[] = {(uint8_t)(y >> 8), (uint8_t)y,
                          (uint8_t)(y2 >> 8), (uint8_t)y2};
  const uint8_t memory_write = 0x2cu;

  if (!command(0x2au, columns, sizeof(columns)) ||
      !command(0x2bu, rows, sizeof(rows))) {
    return false;
  }

  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
  if (!spi_send(&memory_write, 1u)) {
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
    return false;
  }
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
  return true;
}

static const uint8_t *glyph_for(char c)
{
  if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
  if (c >= '0' && c <= '9') return font5x7[(uint8_t)(c - '0') + 1u];
  if (c >= 'A' && c <= 'Z') return font5x7[(uint8_t)(c - 'A') + 11u];

  switch (c) {
  case ' ': return font5x7[0];
  case '.': return glyph_dot;
  case '/': return glyph_slash;
  case '-': return glyph_dash;
  case '_': return glyph_under;
  case ':': return glyph_colon;
  case '+': return glyph_plus;
  case '=': return glyph_equal;
  case '[': return glyph_lbracket;
  case ']': return glyph_rbracket;
  case '!': return glyph_bang;
  case ',': return glyph_comma;
  case '(': return glyph_lparen;
  case ')': return glyph_rparen;
  default: return glyph_question;
  }
}

bool tft_init(SPI_HandleTypeDef *spi)
{
  tft_spi = spi;
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(TFT_RES_GPIO_Port, TFT_RES_Pin, GPIO_PIN_SET);
  HAL_Delay(5u);
  HAL_GPIO_WritePin(TFT_RES_GPIO_Port, TFT_RES_Pin, GPIO_PIN_RESET);
  HAL_Delay(20u);
  HAL_GPIO_WritePin(TFT_RES_GPIO_Port, TFT_RES_Pin, GPIO_PIN_SET);
  HAL_Delay(150u);

  static const uint8_t p_cf[] = {0x00, 0xc1, 0x30};
  static const uint8_t p_ed[] = {0x64, 0x03, 0x12, 0x81};
  static const uint8_t p_e8[] = {0x85, 0x00, 0x78};
  static const uint8_t p_cb[] = {0x39, 0x2c, 0x00, 0x34, 0x02};
  static const uint8_t p_f7[] = {0x20};
  static const uint8_t p_ea[] = {0x00, 0x00};
  static const uint8_t p_c0[] = {0x23};
  static const uint8_t p_c1[] = {0x10};
  static const uint8_t p_c5[] = {0x3e, 0x28};
  static const uint8_t p_c7[] = {0x86};
  static const uint8_t p_3a[] = {0x55};
  static const uint8_t p_b1[] = {0x00, 0x18};
  static const uint8_t p_b6[] = {0x08, 0x82, 0x27};
  static const uint8_t p_f2[] = {0x00};
  static const uint8_t p_26[] = {0x01};
  static const uint8_t p_36[] = {0x48};
  static const uint8_t p_e0[] = {0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e,
                                 0xf1, 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0x00};
  static const uint8_t p_e1[] = {0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31,
                                 0xc1, 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f};

  bool ok = command(0x01u, NULL, 0u);
  HAL_Delay(150u);
  ok = ok && command(0x28u, NULL, 0u);
  ok = ok && command(0xefu, (const uint8_t[]){0x03, 0x80, 0x02}, 3u);
  ok = ok && command(0xcfu, p_cf, sizeof(p_cf));
  ok = ok && command(0xedu, p_ed, sizeof(p_ed));
  ok = ok && command(0xe8u, p_e8, sizeof(p_e8));
  ok = ok && command(0xcbu, p_cb, sizeof(p_cb));
  ok = ok && command(0xf7u, p_f7, sizeof(p_f7));
  ok = ok && command(0xeau, p_ea, sizeof(p_ea));
  ok = ok && command(0xc0u, p_c0, sizeof(p_c0));
  ok = ok && command(0xc1u, p_c1, sizeof(p_c1));
  ok = ok && command(0xc5u, p_c5, sizeof(p_c5));
  ok = ok && command(0xc7u, p_c7, sizeof(p_c7));
  ok = ok && command(0x3au, p_3a, sizeof(p_3a));
  ok = ok && command(0xb1u, p_b1, sizeof(p_b1));
  ok = ok && command(0xb6u, p_b6, sizeof(p_b6));
  ok = ok && command(0xf2u, p_f2, sizeof(p_f2));
  ok = ok && command(0x26u, p_26, sizeof(p_26));
  ok = ok && command(0x36u, p_36, sizeof(p_36));
  ok = ok && command(0xe0u, p_e0, sizeof(p_e0));
  ok = ok && command(0xe1u, p_e1, sizeof(p_e1));
  ok = ok && command(0x11u, NULL, 0u);
  HAL_Delay(120u);
  ok = ok && command(0x29u, NULL, 0u);
  HAL_Delay(20u);
  return ok;
}

void tft_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                   uint16_t color)
{
  if (tft_spi == NULL || width == 0u || height == 0u || x >= TFT_WIDTH ||
      y >= TFT_HEIGHT) return;
  if ((uint32_t)x + width > TFT_WIDTH) width = (uint16_t)(TFT_WIDTH - x);
  if ((uint32_t)y + height > TFT_HEIGHT) height = (uint16_t)(TFT_HEIGHT - y);

  for (uint16_t pixel = 0u; pixel < width; ++pixel) {
    line_buffer[pixel * 2u] = (uint8_t)(color >> 8);
    line_buffer[pixel * 2u + 1u] = (uint8_t)color;
  }
  if (!begin_memory_write(x, y, width, height)) return;
  for (uint16_t row = 0u; row < height; ++row) {
    if (!spi_send(line_buffer, (uint16_t)(width * 2u))) break;
  }
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

void tft_clear(uint16_t color)
{
  tft_fill_rect(0u, 0u, TFT_WIDTH, TFT_HEIGHT, color);
}

static void draw_char(uint16_t x, uint16_t y, char c, uint16_t color,
                      uint8_t scale)
{
  const uint8_t *glyph = glyph_for(c);
  for (uint8_t row = 0u; row < 7u; ++row) {
    uint8_t col = 0u;
    while (col < 5u) {
      if ((glyph[col] & (1u << row)) == 0u) {
        ++col;
        continue;
      }
      const uint8_t start = col;
      while (col < 5u && (glyph[col] & (1u << row)) != 0u) ++col;
      tft_fill_rect((uint16_t)(x + start * scale), (uint16_t)(y + row * scale),
                    (uint16_t)((col - start) * scale), scale, color);
    }
  }
}

void tft_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t color,
                   uint8_t scale)
{
  if (text == NULL) return;
  if (scale == 0u) scale = 1u;
  const uint16_t start_x = x;
  while (*text != '\0' && y + 7u * scale < TFT_HEIGHT) {
    if (*text == '\n') {
      x = start_x;
      y = (uint16_t)(y + 8u * scale);
      ++text;
      continue;
    }
    if (x + 5u * scale >= TFT_WIDTH) {
      x = start_x;
      y = (uint16_t)(y + 8u * scale);
    }
    draw_char(x, y, *text++, color, scale);
    x = (uint16_t)(x + 6u * scale);
  }
}

void tft_draw_text_opaque(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                          const char *text, uint16_t color, uint16_t background,
                          uint8_t scale)
{
  if (tft_spi == NULL || width == 0u || height == 0u || x >= TFT_WIDTH ||
      y >= TFT_HEIGHT) return;
  if ((uint32_t)x + width > TFT_WIDTH) width = (uint16_t)(TFT_WIDTH - x);
  if ((uint32_t)y + height > TFT_HEIGHT) height = (uint16_t)(TFT_HEIGHT - y);
  if (scale == 0u) scale = 1u;

  if (!begin_memory_write(x, y, width, height)) return;
  for (uint16_t row = 0u; row < height; ++row) {
    for (uint16_t column = 0u; column < width; ++column) {
      line_buffer[column * 2u] = (uint8_t)(background >> 8);
      line_buffer[column * 2u + 1u] = (uint8_t)background;
    }

    if (text != NULL && row < 7u * scale) {
      uint16_t text_x = 0u;
      const uint8_t glyph_row = (uint8_t)(row / scale);
      for (const char *character = text; *character != '\0'; ++character) {
        if (*character == '\n' || text_x + 5u * scale > width) break;
        const uint8_t *glyph = glyph_for(*character);
        for (uint8_t glyph_column = 0u; glyph_column < 5u; ++glyph_column) {
          if ((glyph[glyph_column] & (1u << glyph_row)) == 0u) continue;
          const uint16_t first = (uint16_t)(text_x + glyph_column * scale);
          for (uint8_t pixel = 0u; pixel < scale && first + pixel < width; ++pixel) {
            line_buffer[(first + pixel) * 2u] = (uint8_t)(color >> 8);
            line_buffer[(first + pixel) * 2u + 1u] = (uint8_t)color;
          }
        }
        text_x = (uint16_t)(text_x + 6u * scale);
        if (text_x >= width) break;
      }
    }

    if (!spi_send(line_buffer, (uint16_t)(width * 2u))) break;
  }
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}
