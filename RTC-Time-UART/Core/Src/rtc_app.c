#include "rtc_app.h"
#include "main.h"
#include "tft.h"

#include <stdio.h>
#include <string.h>

#define TIME_MARKER 0x54494d45u
#define LINE_SIZE 48u

static RTC_HandleTypeDef *clock_rtc;
static UART_HandleTypeDef *serial_uart;
static uint8_t rx_byte;
static char rx_line[LINE_SIZE];
static unsigned rx_length;
static bool rx_overflow;
static volatile bool line_ready;
static volatile bool rx_error;
static char previous_date[16];
static char previous_time[16];
static uint32_t last_display;

bool rtc_app_has_time(RTC_HandleTypeDef *rtc)
{
  return HAL_RTCEx_BKUPRead(rtc, RTC_BKP_DR0) == TIME_MARKER;
}

static void receive_byte(void)
{
  if (HAL_UART_Receive_IT(serial_uart, &rx_byte, 1u) != HAL_OK) {
    rx_error = true;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
  if (uart != serial_uart) return;

  if (!line_ready) {
    if (rx_byte == '\n') {
      /* An oversized command becomes an empty, rejected line. */
      rx_line[rx_overflow ? 0u : rx_length] = '\0';
      rx_length = 0u;
      rx_overflow = false;
      line_ready = true;
    } else if (rx_byte != '\r' && !rx_overflow) {
      if (rx_length < LINE_SIZE - 1u) {
        rx_line[rx_length++] = (char)rx_byte;
      } else {
        rx_overflow = true;
      }
    }
  }
  receive_byte();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  if (uart == serial_uart) rx_error = true;
}

static bool set_time(const char *line)
{
  unsigned year, month, day, hour, minute, second, weekday;
  if (sscanf(line, "SET %4u-%2u-%2u %2u:%2u:%2u %1u",
             &year, &month, &day, &hour, &minute, &second, &weekday) != 7) {
    return false;
  }

  /* The PC supplies valid local time; no calendar/range validation here. */
  RTC_TimeTypeDef time = {0};
  RTC_DateTypeDef date = {0};
  time.Hours = (uint8_t)hour;
  time.Minutes = (uint8_t)minute;
  time.Seconds = (uint8_t)second;
  time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  time.StoreOperation = RTC_STOREOPERATION_RESET;
  date.Year = (uint8_t)(year - 2000u);
  date.Month = (uint8_t)month;
  date.Date = (uint8_t)day;
  date.WeekDay = (uint8_t)weekday; /* PC supplies Monday=1 through Sunday=7. */

  HAL_RTCEx_BKUPWrite(clock_rtc, RTC_BKP_DR0, 0u);
  if (HAL_RTC_SetTime(clock_rtc, &time, RTC_FORMAT_BIN) != HAL_OK ||
      HAL_RTC_SetDate(clock_rtc, &date, RTC_FORMAT_BIN) != HAL_OK) {
    tft_clear(TFT_BLACK);
    tft_draw_text(12u, 20u, "WAITING FOR PC TIME", TFT_WHITE, 2u);
    return false;
  }
  HAL_RTCEx_BKUPWrite(clock_rtc, RTC_BKP_DR0, TIME_MARKER);
  tft_clear(TFT_BLACK);
  previous_date[0] = '\0';
  previous_time[0] = '\0';
  return true;
}

static void update_display(void)
{
  RTC_TimeTypeDef time;
  RTC_DateTypeDef date;
  char date_text[16], time_text[16];
  /* GetDate releases the RTC shadow-register lock acquired by GetTime. */
  if (HAL_RTC_GetTime(clock_rtc, &time, RTC_FORMAT_BIN) != HAL_OK ||
      HAL_RTC_GetDate(clock_rtc, &date, RTC_FORMAT_BIN) != HAL_OK) return;

  snprintf(date_text, sizeof(date_text), "%04u-%02u-%02u",
           2000u + date.Year, (unsigned)date.Month, (unsigned)date.Date);
  snprintf(time_text, sizeof(time_text), "%02u:%02u:%02u",
           (unsigned)time.Hours, (unsigned)time.Minutes, (unsigned)time.Seconds);
  if (strcmp(date_text, previous_date) != 0) {
    tft_draw_text_opaque(12u, 20u, 216u, 20u, date_text, TFT_WHITE, TFT_BLACK, 2u);
    strcpy(previous_date, date_text);
  }
  if (strcmp(time_text, previous_time) != 0) {
    tft_draw_text_opaque(12u, 50u, 216u, 20u, time_text, TFT_WHITE, TFT_BLACK, 2u);
    strcpy(previous_time, time_text);
  }
}

void rtc_app_init(RTC_HandleTypeDef *rtc, UART_HandleTypeDef *uart,
                  SPI_HandleTypeDef *spi)
{
  clock_rtc = rtc;
  serial_uart = uart;
  if (!tft_init(spi)) Error_Handler();
  tft_clear(TFT_BLACK);
  if (rtc_app_has_time(rtc)) {
    update_display();
  } else {
    tft_draw_text(12u, 20u, "WAITING FOR PC TIME", TFT_WHITE, 2u);
  }
  receive_byte();
}

void rtc_app_process(void)
{
  if (rx_error) {
    HAL_UART_AbortReceive(serial_uart);
    rx_error = false;
    rx_length = 0u;
    rx_overflow = true; /* Discard the damaged command through its newline. */
    receive_byte();
  }
  if (line_ready) {
    char line[LINE_SIZE];
    /* The ISR leaves the completed line untouched until this copy finishes. */
    memcpy(line, rx_line, sizeof(line));
    line_ready = false;
    const char *reply = set_time(line) ? "OK\r\n" : "ERROR\r\n";
    HAL_UART_Transmit(serial_uart, (const uint8_t *)reply,
                      (uint16_t)strlen(reply), 100u);
  }
  uint32_t now = HAL_GetTick();
  if (now - last_display >= 100u) {
    last_display = now;
    if (rtc_app_has_time(clock_rtc)) update_display();
  }
}
