#include "app.h"

#include "encoder.h"
#include "fatfs.h"
#include "main.h"
#include "tft.h"
#include "usb_host.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define APP_PATH_CAPACITY     256u
#define APP_MAX_ENTRIES       64u
#define APP_VISIBLE_ROWS      21u
#define APP_PREVIEW_BYTES     256u
#define APP_PREVIEW_COLUMNS   38u
#define APP_PREVIEW_ROWS      24u
#define APP_MOUNT_RETRY_MS    1000u
#define FATFS_ATTR_VOLUME_ID  0x08u

typedef enum {
  UI_WAITING,
  UI_BROWSER,
  UI_PREVIEW,
  UI_RESULT,
  UI_MOUNT_ERROR,
} UiMode;

typedef struct {
  char name[_MAX_LFN + 1u];
  FSIZE_t size;
  BYTE attributes;
} BrowserEntry;

extern ApplicationTypeDef Appli_state;
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim1;

static BrowserEntry entries[APP_MAX_ENTRIES];
static uint16_t entry_count;
static uint16_t selected;
static uint16_t scroll_row;
static bool directory_truncated;
static bool filesystem_mounted;
static bool redraw_needed;
static bool tft_ready;
static bool browser_full_redraw;
static UiMode ui_mode;
static uint32_t next_mount_attempt;
static uint16_t displayed_selected;
static uint16_t displayed_scroll;

static char root_path[APP_PATH_CAPACITY];
static char current_path[APP_PATH_CAPACITY];
static char preview_path[APP_PATH_CAPACITY];
static FSIZE_t preview_offset;
static char result_title[25];
static char result_detail[45];

static uint8_t write_pattern[1024];
static uint8_t verify_pattern[sizeof(write_pattern)];
static uint32_t write_run;

static void copy_string(char *destination, uint16_t capacity, const char *source)
{
  if (capacity == 0u) return;
  uint16_t at = 0u;
  while (source[at] != '\0' && at + 1u < capacity) {
    destination[at] = source[at];
    ++at;
  }
  destination[at] = '\0';
}

static void append_string(char *destination, uint16_t capacity, const char *source)
{
  uint16_t at = 0u;
  while (at < capacity && destination[at] != '\0') ++at;
  while (source[0] != '\0' && at + 1u < capacity) {
    destination[at++] = *source++;
  }
  if (at < capacity) destination[at] = '\0';
}

static void append_u32(char *destination, uint16_t capacity, uint32_t value)
{
  char digits[11];
  uint8_t count = 0u;
  do {
    digits[count++] = (char)('0' + (value % 10u));
    value /= 10u;
  } while (value != 0u && count < sizeof(digits));

  uint16_t at = 0u;
  while (at < capacity && destination[at] != '\0') ++at;
  while (count != 0u && at + 1u < capacity) destination[at++] = digits[--count];
  if (at < capacity) destination[at] = '\0';
}

static const char *fresult_text(FRESULT result)
{
  switch (result) {
  case FR_OK: return "OK";
  case FR_DISK_ERR: return "DISK ERROR";
  case FR_INT_ERR: return "INTERNAL ERROR";
  case FR_NOT_READY: return "NOT READY";
  case FR_NO_FILE: return "NO FILE";
  case FR_NO_PATH: return "NO PATH";
  case FR_INVALID_NAME: return "INVALID NAME";
  case FR_DENIED: return "ACCESS DENIED";
  case FR_EXIST: return "ALREADY EXISTS";
  case FR_INVALID_OBJECT: return "INVALID OBJECT";
  case FR_WRITE_PROTECTED: return "WRITE PROTECTED";
  case FR_INVALID_DRIVE: return "INVALID DRIVE";
  case FR_NOT_ENABLED: return "NOT ENABLED";
  case FR_NO_FILESYSTEM: return "NO FAT FILESYSTEM";
  case FR_MKFS_ABORTED: return "FORMAT ABORTED";
  case FR_TIMEOUT: return "TIMEOUT";
  case FR_LOCKED: return "LOCKED";
  case FR_NOT_ENOUGH_CORE: return "NO MEMORY";
  default: return "FATFS ERROR";
  }
}

static bool path_is_root(void)
{
  return strcmp(current_path, root_path) == 0;
}

static bool make_child_path(char *destination, const char *name)
{
  copy_string(destination, APP_PATH_CAPACITY, current_path);
  const uint16_t length = (uint16_t)strlen(destination);
  if (length == 0u || (uint32_t)length + strlen(name) + 2u >= APP_PATH_CAPACITY) return false;
  if (destination[length - 1u] != '/') append_string(destination, APP_PATH_CAPACITY, "/");
  append_string(destination, APP_PATH_CAPACITY, name);
  return strlen(destination) < APP_PATH_CAPACITY - 1u;
}

static void move_to_parent(void)
{
  if (path_is_root()) return;
  char *last_slash = NULL;
  for (char *cursor = current_path; *cursor != '\0'; ++cursor) {
    if (*cursor == '/') last_slash = cursor;
  }
  if (last_slash == NULL) {
    copy_string(current_path, APP_PATH_CAPACITY, root_path);
  } else {
    *last_slash = '\0';
  }
}

static bool is_text_file(const char *name)
{
  const char *extension = NULL;
  for (const char *cursor = name; *cursor != '\0'; ++cursor) {
    if (*cursor == '.') extension = cursor;
  }
  return extension != NULL &&
         (extension[1] == 't' || extension[1] == 'T') &&
         (extension[2] == 'x' || extension[2] == 'X') &&
         (extension[3] == 't' || extension[3] == 'T') && extension[4] == '\0';
}

static uint16_t browser_fixed_items(void)
{
  return (uint16_t)(2u + (path_is_root() ? 0u : 1u));
}

static uint16_t browser_item_count(void)
{
  return (uint16_t)(browser_fixed_items() + entry_count);
}

static void show_result(const char *title, const char *detail)
{
  copy_string(result_title, sizeof(result_title), title);
  copy_string(result_detail, sizeof(result_detail), detail);
  ui_mode = UI_RESULT;
  redraw_needed = true;
}

static void show_fresult(const char *title, FRESULT result)
{
  show_result(title, fresult_text(result));
}

static bool load_directory(void)
{
  DIR directory;
  FILINFO info;
  FRESULT result = f_opendir(&directory, current_path);
  if (result != FR_OK) {
    show_fresult("DIRECTORY ERROR", result);
    return false;
  }

  entry_count = 0u;
  directory_truncated = false;
  while (true) {
    result = f_readdir(&directory, &info);
    if (result != FR_OK) {
      (void)f_closedir(&directory);
      show_fresult("READ DIRECTORY", result);
      return false;
    }
    if (info.fname[0] == '\0') break;
    if ((info.fattrib & FATFS_ATTR_VOLUME_ID) != 0u || strcmp(info.fname, ".") == 0 ||
        strcmp(info.fname, "..") == 0) continue;
    if (entry_count == APP_MAX_ENTRIES) {
      directory_truncated = true;
      break;
    }
    copy_string(entries[entry_count].name, sizeof(entries[entry_count].name), info.fname);
    entries[entry_count].size = info.fsize;
    entries[entry_count].attributes = info.fattrib;
    ++entry_count;
  }
  (void)f_closedir(&directory);

  selected = 0u;
  scroll_row = 0u;
  browser_full_redraw = true;
  ui_mode = UI_BROWSER;
  redraw_needed = true;
  return true;
}

static void render_waiting(void)
{
  tft_clear(TFT_BLACK);
  tft_draw_text(10u, 14u, "USB FAT32 TEST", TFT_CYAN, 2u);
  tft_draw_text(10u, 56u, "WAITING FOR USB DRIVE", TFT_YELLOW, 1u);
  tft_draw_text(10u, 74u, "CN5 HOST / MSC / FAT32", TFT_WHITE, 1u);
  tft_draw_text(10u, 100u, "INSERT A FLASH DRIVE", TFT_GRAY, 1u);
}

static void render_mount_error(void)
{
  tft_clear(TFT_BLACK);
  tft_draw_text(10u, 14u, "USB MOUNT ERROR", TFT_RED, 2u);
  tft_draw_text(10u, 58u, result_detail, TFT_YELLOW, 2u);
  tft_draw_text(10u, 96u, "CHECK FAT32 DRIVE", TFT_WHITE, 1u);
  tft_draw_text(10u, 112u, "RETRYING...", TFT_GRAY, 1u);
}

static void item_label(uint16_t item, char *buffer, uint16_t capacity)
{
  buffer[0] = '\0';
  if (item == 0u) {
    copy_string(buffer, capacity, "[WRITE + VERIFY]");
    return;
  }
  if (item == 1u) {
    copy_string(buffer, capacity, "[REFRESH DIRECTORY]");
    return;
  }
  const uint16_t fixed = browser_fixed_items();
  if (!path_is_root() && item == 2u) {
    copy_string(buffer, capacity, "[..] PARENT DIRECTORY");
    return;
  }
  const uint16_t index = (uint16_t)(item - fixed);
  if (index >= entry_count) return;
  copy_string(buffer, capacity,
              (entries[index].attributes & AM_DIR) != 0u ? "[D] " : "[F] ");
  append_string(buffer, capacity, entries[index].name);
}

static void draw_browser_row(uint16_t item, uint16_t row, bool highlighted)
{
  const uint16_t y = (uint16_t)(43u + row * 12u);
  tft_fill_rect(0u, (uint16_t)(y - 1u), TFT_WIDTH, 11u,
                highlighted ? TFT_BLUE : TFT_BLACK);
  char label[40];
  item_label(item, label, sizeof(label));
  tft_draw_text(3u, y, label, highlighted ? TFT_WHITE : TFT_GRAY, 1u);
}

static void render_browser(void)
{
  const uint16_t count = browser_item_count();
  const bool full_redraw = browser_full_redraw || displayed_scroll != scroll_row;
  if (full_redraw) {
    tft_clear(TFT_BLACK);
    tft_draw_text(4u, 4u, "USB FILE BROWSER", TFT_CYAN, 2u);
    tft_draw_text(4u, 24u, current_path, TFT_WHITE, 1u);
    for (uint16_t row = 0u; row < APP_VISIBLE_ROWS; ++row) {
      const uint16_t item = (uint16_t)(scroll_row + row);
      if (item >= count) break;
      draw_browser_row(item, row, item == selected);
    }
    if (directory_truncated) {
      tft_draw_text(4u, 300u, "FIRST 64 ENTRIES", TFT_YELLOW, 1u);
    } else {
      char footer[30] = "ENTRIES ";
      append_u32(footer, sizeof(footer), entry_count);
      tft_draw_text(4u, 300u, footer, TFT_GRAY, 1u);
    }
    tft_draw_text(4u, 311u, "TURN=SELECT  PRESS=OPEN", TFT_GRAY, 1u);
    browser_full_redraw = false;
  } else if (displayed_selected != selected) {
    draw_browser_row(displayed_selected,
                     (uint16_t)(displayed_selected - scroll_row), false);
    draw_browser_row(selected, (uint16_t)(selected - scroll_row), true);
  }
  displayed_selected = selected;
  displayed_scroll = scroll_row;
}

static void render_preview(void)
{
  FIL file;
  UINT bytes_read = 0u;
  static uint8_t data[APP_PREVIEW_BYTES];
  FRESULT result = f_open(&file, preview_path, FA_READ);
  if (result != FR_OK) {
    show_fresult("OPEN TEXT FILE", result);
    return;
  }
  result = f_lseek(&file, preview_offset);
  if (result == FR_OK) result = f_read(&file, data, sizeof(data), &bytes_read);
  (void)f_close(&file);
  if (result != FR_OK) {
    show_fresult("READ TEXT FILE", result);
    return;
  }

  tft_clear(TFT_BLACK);
  tft_draw_text(4u, 4u, "TEXT PREVIEW", TFT_CYAN, 2u);
  tft_draw_text(4u, 23u, preview_path, TFT_WHITE, 1u);
  char page[20] = "PAGE ";
  append_u32(page, sizeof(page), (uint32_t)(preview_offset / APP_PREVIEW_BYTES) + 1u);
  tft_draw_text(180u, 23u, page, TFT_GRAY, 1u);

  char line[APP_PREVIEW_COLUMNS + 1u];
  uint8_t line_length = 0u;
  uint8_t line_number = 0u;
  for (UINT index = 0u; index < bytes_read && line_number < APP_PREVIEW_ROWS; ++index) {
    char character = (char)data[index];
    if (character == '\r') continue;
    if (character == '\n' || line_length == APP_PREVIEW_COLUMNS) {
      line[line_length] = '\0';
      tft_draw_text(4u, (uint16_t)(38u + line_number * 11u), line, TFT_WHITE, 1u);
      ++line_number;
      line_length = 0u;
      if (character == '\n') continue;
    }
    if ((uint8_t)character < 0x20u || (uint8_t)character > 0x7eu) character = '?';
    line[line_length++] = character;
  }
  if (line_length != 0u && line_number < APP_PREVIEW_ROWS) {
    line[line_length] = '\0';
    tft_draw_text(4u, (uint16_t)(38u + line_number * 11u), line, TFT_WHITE, 1u);
  }
  if (bytes_read == 0u) tft_draw_text(4u, 45u, "EMPTY FILE", TFT_GRAY, 1u);
  tft_draw_text(4u, 311u, "TURN=PAGE  PRESS=BACK", TFT_GRAY, 1u);
}

static void render_result(void)
{
  tft_clear(TFT_BLACK);
  tft_draw_text(8u, 14u, result_title, TFT_GREEN, 2u);
  tft_draw_text(8u, 58u, result_detail, TFT_WHITE, 2u);
  tft_draw_text(8u, 104u, "PRESS TO RETURN", TFT_GRAY, 1u);
}

static void render(void)
{
  if (!tft_ready || !redraw_needed) return;
  redraw_needed = false;
  switch (ui_mode) {
  case UI_WAITING: render_waiting(); break;
  case UI_BROWSER: render_browser(); break;
  case UI_PREVIEW: render_preview(); break;
  case UI_RESULT: render_result(); break;
  case UI_MOUNT_ERROR: render_mount_error(); break;
  default: break;
  }
}

static void build_test_pattern(void)
{
  static const char heading[] = "STM32F407 USB FAT32 WRITE VERIFY\r\nRUN ";
  uint16_t at = 0u;
  for (uint16_t index = 0u; index < sizeof(heading) - 1u; ++index) {
    write_pattern[at++] = (uint8_t)heading[index];
  }
  char run_text[12] = "";
  append_u32(run_text, sizeof(run_text), ++write_run);
  for (uint16_t index = 0u; run_text[index] != '\0' && at < sizeof(write_pattern); ++index) {
    write_pattern[at++] = (uint8_t)run_text[index];
  }
  static const char explanation[] = "\r\nTHIS FILE IS SAFE TO OVERWRITE.\r\n";
  for (uint16_t index = 0u; index < sizeof(explanation) - 1u && at < sizeof(write_pattern); ++index) {
    write_pattern[at++] = (uint8_t)explanation[index];
  }
  while (at < sizeof(write_pattern)) {
    write_pattern[at] = (at % 64u == 63u) ? '\n' :
                        (uint8_t)('A' + ((at / 3u) % 26u));
    ++at;
  }
}

static void write_and_verify(void)
{
  char test_path[APP_PATH_CAPACITY];
  copy_string(test_path, sizeof(test_path), root_path);
  append_string(test_path, sizeof(test_path), "/STM32_TEST.TXT");
  build_test_pattern();

  FIL file;
  UINT transferred = 0u;
  FRESULT result = f_open(&file, test_path, FA_CREATE_ALWAYS | FA_WRITE);
  if (result == FR_OK) {
    result = f_write(&file, write_pattern, sizeof(write_pattern), &transferred);
    if (result == FR_OK && transferred != sizeof(write_pattern)) result = FR_DISK_ERR;
    if (result == FR_OK) result = f_sync(&file);
    const FRESULT close_result = f_close(&file);
    if (result == FR_OK) result = close_result;
  }
  if (result != FR_OK) {
    show_fresult("WRITE FAILED", result);
    return;
  }

  transferred = 0u;
  result = f_open(&file, test_path, FA_READ);
  if (result == FR_OK) {
    if (f_size(&file) != sizeof(verify_pattern)) result = FR_DISK_ERR;
    if (result == FR_OK) result = f_read(&file, verify_pattern, sizeof(verify_pattern), &transferred);
    const FRESULT close_result = f_close(&file);
    if (result == FR_OK) result = close_result;
  }
  if (result != FR_OK || transferred != sizeof(verify_pattern) ||
      memcmp(write_pattern, verify_pattern, sizeof(write_pattern)) != 0) {
    if (result == FR_OK) result = FR_DISK_ERR;
    show_fresult("VERIFY FAILED", result);
    return;
  }
  show_result("WRITE VERIFIED", "1024 BYTES MATCH");
}

static void activate_browser_item(void)
{
  if (selected == 0u) {
    write_and_verify();
    return;
  }
  if (selected == 1u) {
    (void)load_directory();
    return;
  }
  if (!path_is_root() && selected == 2u) {
    move_to_parent();
    (void)load_directory();
    return;
  }

  const uint16_t index = (uint16_t)(selected - browser_fixed_items());
  if (index >= entry_count) return;
  if ((entries[index].attributes & AM_DIR) != 0u) {
    char next_path[APP_PATH_CAPACITY];
    if (!make_child_path(next_path, entries[index].name)) {
      show_result("PATH TOO LONG", "CANNOT OPEN FOLDER");
      return;
    }
    copy_string(current_path, sizeof(current_path), next_path);
    (void)load_directory();
    return;
  }
  if (!is_text_file(entries[index].name)) {
    show_result("NOT A TEXT FILE", entries[index].name);
    return;
  }
  if (!make_child_path(preview_path, entries[index].name)) {
    show_result("PATH TOO LONG", "CANNOT OPEN FILE");
    return;
  }
  preview_offset = 0u;
  ui_mode = UI_PREVIEW;
  redraw_needed = true;
}

static void move_browser_selection(int32_t steps)
{
  const uint16_t count = browser_item_count();
  if (count == 0u || steps == 0) return;
  int32_t next = (int32_t)selected + steps;
  if (next < 0) next = 0;
  if (next >= count) next = (int32_t)count - 1;
  selected = (uint16_t)next;
  if (selected < scroll_row) scroll_row = selected;
  if (selected >= scroll_row + APP_VISIBLE_ROWS)
    scroll_row = (uint16_t)(selected - APP_VISIBLE_ROWS + 1u);
  redraw_needed = true;
}

static void service_mounted_ui(int32_t steps, bool pressed)
{
  switch (ui_mode) {
  case UI_BROWSER:
    move_browser_selection(steps);
    if (pressed) activate_browser_item();
    break;
  case UI_PREVIEW:
    if (steps > 0) {
      preview_offset += (FSIZE_t)((uint32_t)steps * APP_PREVIEW_BYTES);
      redraw_needed = true;
    } else if (steps < 0) {
      const FSIZE_t amount = (FSIZE_t)((uint32_t)(-steps) * APP_PREVIEW_BYTES);
      preview_offset = preview_offset > amount ? preview_offset - amount : 0u;
      redraw_needed = true;
    }
    if (pressed) {
      ui_mode = UI_BROWSER;
      browser_full_redraw = true;
      redraw_needed = true;
    }
    break;
  case UI_RESULT:
    if (pressed) (void)load_directory();
    break;
  default:
    break;
  }
}

static void disconnect_filesystem(void)
{
  if (filesystem_mounted) (void)f_mount(NULL, USBHPath, 0u);
  filesystem_mounted = false;
  browser_full_redraw = true;
  ui_mode = UI_WAITING;
  redraw_needed = true;
}

static void try_mount(void)
{
  const uint32_t now = HAL_GetTick();
  if ((int32_t)(now - next_mount_attempt) < 0) return;
  next_mount_attempt = now + APP_MOUNT_RETRY_MS;

  FRESULT result = f_mount(&USBHFatFS, USBHPath, 1u);
  if (result == FR_OK) {
    filesystem_mounted = true;
    copy_string(root_path, sizeof(root_path), USBHPath);
    copy_string(current_path, sizeof(current_path), root_path);
    (void)load_directory();
    return;
  }
  copy_string(result_detail, sizeof(result_detail), fresult_text(result));
  ui_mode = UI_MOUNT_ERROR;
  redraw_needed = true;
}

void app_init(void)
{
  /* PB3 is also an optional SWO pin. The LCD owns it in this application. */
  DBGMCU->CR &= ~DBGMCU_CR_TRACE_IOEN;
  encoder_init(&htim1);
  tft_ready = tft_init(&hspi1);
  ui_mode = UI_WAITING;
  redraw_needed = true;
  next_mount_attempt = HAL_GetTick();
}

void app_poll(void)
{
  const int32_t steps = encoder_take_steps();
  const bool pressed = encoder_take_press();

  if (Appli_state != APPLICATION_READY) {
    if (filesystem_mounted || ui_mode != UI_WAITING) disconnect_filesystem();
    render();
    return;
  }

  if (!filesystem_mounted) {
    try_mount();
    render();
    return;
  }

  service_mounted_ui(steps, pressed);
  render();
}
