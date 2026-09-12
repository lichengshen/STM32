#include "app.h"

#include "audio_player.h"
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
#define APP_MOUNT_RETRY_MS    1000u
#define FATFS_ATTR_VOLUME_ID  0x08u

typedef enum {
  UI_WAITING,
  UI_BROWSER,
  UI_PLAYING,
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
static uint16_t displayed_selected;
static uint16_t displayed_scroll;
static bool directory_truncated;
static bool filesystem_mounted;
static bool redraw_needed;
static bool tft_ready;
static bool browser_full_redraw;
static UiMode ui_mode;
static uint32_t next_mount_attempt;
static uint32_t last_playback_ui;
static uint32_t displayed_elapsed_seconds;
static uint32_t displayed_total_seconds;
static uint16_t displayed_progress;
static uint8_t displayed_volume;
static bool displayed_paused;
static bool playback_dynamic_valid;

static char root_path[APP_PATH_CAPACITY];
static char current_path[APP_PATH_CAPACITY];
static char playing_name[_MAX_LFN + 1u];
static char result_title[25];
static char result_detail[45];

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
  while (*source != '\0' && at + 1u < capacity) destination[at++] = *source++;
  if (at < capacity) destination[at] = '\0';
}

static void append_u32(char *destination, uint16_t capacity, uint32_t value)
{
  char digits[10];
  uint8_t count = 0u;
  do {
    digits[count++] = (char)('0' + value % 10u);
    value /= 10u;
  } while (value != 0u && count < sizeof(digits));

  uint16_t at = 0u;
  while (at < capacity && destination[at] != '\0') ++at;
  while (count != 0u && at + 1u < capacity) destination[at++] = digits[--count];
  if (at < capacity) destination[at] = '\0';
}

static char ascii_upper(char value)
{
  return value >= 'a' && value <= 'z' ? (char)(value - ('a' - 'A')) : value;
}

static int compare_names(const char *left, const char *right)
{
  while (*left != '\0' && *right != '\0') {
    const char a = ascii_upper(*left++);
    const char b = ascii_upper(*right++);
    if (a < b) return -1;
    if (a > b) return 1;
  }
  return *left == *right ? 0 : (*left == '\0' ? -1 : 1);
}

static int compare_entries(const BrowserEntry *left, const BrowserEntry *right)
{
  const bool left_directory = (left->attributes & AM_DIR) != 0u;
  const bool right_directory = (right->attributes & AM_DIR) != 0u;
  if (left_directory != right_directory) return left_directory ? -1 : 1;
  return compare_names(left->name, right->name);
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
  case FR_INVALID_OBJECT: return "INVALID OBJECT";
  case FR_NOT_ENABLED: return "NOT ENABLED";
  case FR_NO_FILESYSTEM: return "NO FAT FILESYSTEM";
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
  if (length == 0u || (uint32_t)length + strlen(name) + 2u >= APP_PATH_CAPACITY)
    return false;
  if (destination[length - 1u] != '/') append_string(destination, APP_PATH_CAPACITY, "/");
  append_string(destination, APP_PATH_CAPACITY, name);
  return true;
}

static void move_to_parent(void)
{
  if (path_is_root()) return;
  char *last_slash = NULL;
  for (char *cursor = current_path; *cursor != '\0'; ++cursor)
    if (*cursor == '/') last_slash = cursor;
  if (last_slash == NULL) copy_string(current_path, sizeof(current_path), root_path);
  else *last_slash = '\0';
}

static bool is_wav_file(const char *name)
{
  const char *extension = NULL;
  for (const char *cursor = name; *cursor != '\0'; ++cursor)
    if (*cursor == '.') extension = cursor;
  return extension != NULL && extension[1] != '\0' && extension[2] != '\0' &&
         extension[3] != '\0' && ascii_upper(extension[1]) == 'W' &&
         ascii_upper(extension[2]) == 'A' && ascii_upper(extension[3]) == 'V' &&
         extension[4] == '\0';
}

static uint16_t browser_fixed_items(void)
{
  return (uint16_t)(1u + (path_is_root() ? 0u : 1u));
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
        strcmp(info.fname, "..") == 0)
      continue;
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

  for (uint16_t index = 1u; index < entry_count; ++index) {
    BrowserEntry item = entries[index];
    uint16_t at = index;
    while (at != 0u && compare_entries(&item, &entries[at - 1u]) < 0) {
      entries[at] = entries[at - 1u];
      --at;
    }
    entries[at] = item;
  }

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
  tft_draw_text(10u, 14u, "USB AUDIO PLAYER", TFT_CYAN, 2u);
  tft_draw_text(10u, 58u, "WAITING FOR USB DRIVE", TFT_YELLOW, 1u);
  tft_draw_text(10u, 78u, "CN5 HOST / MSC / FAT32", TFT_WHITE, 1u);
  tft_draw_text(10u, 104u, "INSERT A WAV DRIVE", TFT_GRAY, 1u);
}

static void render_mount_error(void)
{
  tft_clear(TFT_BLACK);
  tft_draw_text(10u, 14u, "USB MOUNT ERROR", TFT_RED, 2u);
  tft_draw_text(10u, 58u, result_detail, TFT_YELLOW, 2u);
  tft_draw_text(10u, 96u, "CHECK FAT32 DRIVE", TFT_WHITE, 1u);
  tft_draw_text(10u, 114u, "RETRYING...", TFT_GRAY, 1u);
}

static void item_label(uint16_t item, char *buffer, uint16_t capacity)
{
  buffer[0] = '\0';
  if (item == 0u) {
    copy_string(buffer, capacity, "[REFRESH DIRECTORY]");
    return;
  }
  if (!path_is_root() && item == 1u) {
    copy_string(buffer, capacity, "[..] PARENT DIRECTORY");
    return;
  }
  const uint16_t index = (uint16_t)(item - browser_fixed_items());
  if (index >= entry_count) return;
  if ((entries[index].attributes & AM_DIR) != 0u) copy_string(buffer, capacity, "[D] ");
  else if (is_wav_file(entries[index].name)) copy_string(buffer, capacity, "[W] ");
  else copy_string(buffer, capacity, "[F] ");
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
    tft_draw_text(4u, 4u, "USB WAV BROWSER", TFT_CYAN, 2u);
    tft_draw_text(4u, 24u, current_path, TFT_WHITE, 1u);
    for (uint16_t row = 0u; row < APP_VISIBLE_ROWS; ++row) {
      const uint16_t item = (uint16_t)(scroll_row + row);
      if (item >= count) break;
      draw_browser_row(item, row, item == selected);
    }
    if (directory_truncated) tft_draw_text(4u, 300u, "FIRST 64 ENTRIES", TFT_YELLOW, 1u);
    else {
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

static void render_result(void)
{
  tft_clear(TFT_BLACK);
  tft_draw_text(8u, 14u, result_title, TFT_GREEN, 2u);
  tft_draw_text(8u, 58u, result_detail, TFT_WHITE, 2u);
  tft_draw_text(8u, 104u, "PRESS TO RETURN", TFT_GRAY, 1u);
}

static void append_time(char *buffer, uint16_t capacity, uint32_t frames, uint32_t rate)
{
  const uint32_t seconds = rate == 0u ? 0u : frames / rate;
  append_u32(buffer, capacity, seconds / 60u);
  append_string(buffer, capacity, ":");
  if (seconds % 60u < 10u) append_string(buffer, capacity, "0");
  append_u32(buffer, capacity, seconds % 60u);
}

static void draw_playback_dynamic(bool force)
{
  const uint32_t now = HAL_GetTick();
  if (!force && (uint32_t)(now - last_playback_ui) < 50u) return;
  last_playback_ui = now;

  const WavStreamInfo *stream = audio_player_stream();
  const uint32_t elapsed_frames = audio_player_elapsed_frames();
  const uint32_t elapsed_seconds = stream->sample_rate == 0u
                                       ? 0u
                                       : elapsed_frames / stream->sample_rate;
  const uint32_t total_seconds = stream->sample_rate == 0u
                                     ? 0u
                                     : stream->total_frames / stream->sample_rate;
  char elapsed[12] = "";
  char total[12] = "";
  append_time(elapsed, sizeof(elapsed), elapsed_frames, stream->sample_rate);
  append_time(total, sizeof(total), stream->total_frames, stream->sample_rate);
  if (force || !playback_dynamic_valid || elapsed_seconds != displayed_elapsed_seconds ||
      total_seconds != displayed_total_seconds) {
    char time_text[30] = "";
    append_string(time_text, sizeof(time_text), elapsed);
    append_string(time_text, sizeof(time_text), " / ");
    append_string(time_text, sizeof(time_text), total);
    tft_fill_rect(4u, 73u, 232u, 13u, TFT_BLACK);
    tft_draw_text(4u, 74u, time_text, TFT_WHITE, 1u);
  }

  uint32_t progress = 0u;
  if (stream->total_frames != 0u)
    progress = (elapsed_frames * 220u) / stream->total_frames;
  if (progress > 220u) progress = 220u;
  if (force || !playback_dynamic_valid || progress < displayed_progress) {
    tft_fill_rect(10u, 98u, 220u, 8u, TFT_GRAY);
    if (progress != 0u) tft_fill_rect(10u, 98u, (uint16_t)progress, 8u, TFT_GREEN);
  } else if (progress > displayed_progress) {
    tft_fill_rect((uint16_t)(10u + displayed_progress), 98u,
                  (uint16_t)(progress - displayed_progress), 8u, TFT_GREEN);
  }

  const bool paused = audio_player_is_paused();
  const uint8_t volume = audio_player_volume();
  if (force || !playback_dynamic_valid || paused != displayed_paused ||
      volume != displayed_volume) {
    char state_text[32] = "";
    append_string(state_text, sizeof(state_text), paused ? "PAUSED VOL " : "PLAYING VOL ");
    append_u32(state_text, sizeof(state_text), volume);
    tft_fill_rect(4u, 268u, 232u, 13u, TFT_BLACK);
    tft_draw_text(4u, 269u, state_text, paused ? TFT_YELLOW : TFT_CYAN, 1u);
  }

  displayed_elapsed_seconds = elapsed_seconds;
  displayed_total_seconds = total_seconds;
  displayed_progress = (uint16_t)progress;
  displayed_volume = volume;
  displayed_paused = paused;
  playback_dynamic_valid = true;
}

static void render_playback_static(void)
{
  tft_clear(TFT_BLACK);
  tft_draw_text(4u, 4u, "NOW PLAYING", TFT_CYAN, 2u);
  tft_draw_text(4u, 29u, playing_name, TFT_WHITE, 1u);
  tft_draw_text(4u, 51u, "PCM WAV / I2S DMA", TFT_GRAY, 1u);
  tft_draw_text(4u, 294u, "PRESS=PAUSE  HOLD=STOP", TFT_GRAY, 1u);
  last_playback_ui = 0u;
  playback_dynamic_valid = false;
  draw_playback_dynamic(true);
}

static void render(void)
{
  if (!tft_ready || !redraw_needed) return;
  redraw_needed = false;
  switch (ui_mode) {
  case UI_WAITING: render_waiting(); break;
  case UI_BROWSER: render_browser(); break;
  case UI_RESULT: render_result(); break;
  case UI_MOUNT_ERROR: render_mount_error(); break;
  default: break;
  }
}

static void start_selected_wav(const char *path, const char *name)
{
  if (!audio_player_open(path)) {
    show_result("WAV ERROR", audio_player_error_text());
    return;
  }
  copy_string(playing_name, sizeof(playing_name), name);
  ui_mode = UI_PLAYING;
  redraw_needed = false;
  if (tft_ready) render_playback_static();
  if (!audio_player_start()) {
    show_result("PLAYBACK ERROR", audio_player_error_text());
    return;
  }
}

static void activate_browser_item(void)
{
  if (selected == 0u) {
    (void)load_directory();
    return;
  }
  if (!path_is_root() && selected == 1u) {
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
  if (!is_wav_file(entries[index].name)) {
    show_result("NOT A WAV FILE", entries[index].name);
    return;
  }
  char path[APP_PATH_CAPACITY];
  if (!make_child_path(path, entries[index].name)) {
    show_result("PATH TOO LONG", "CANNOT OPEN WAV");
    return;
  }
  start_selected_wav(path, entries[index].name);
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

static void service_playback(int32_t steps, EncoderButtonEvent button)
{
  audio_player_service();
  AudioPlayerEvent event = audio_player_take_event();
  if (event == AUDIO_PLAYER_EVENT_ERROR) {
    show_result("PLAYBACK ERROR", audio_player_error_text());
    return;
  }
  if (event == AUDIO_PLAYER_EVENT_FINISHED) {
    ui_mode = UI_BROWSER;
    browser_full_redraw = true;
    redraw_needed = true;
    return;
  }

  if (steps != 0 && !audio_player_change_volume(steps)) {
    show_result("PLAYBACK ERROR", audio_player_error_text());
    return;
  }
  if (button == ENCODER_BUTTON_LONG) {
    audio_player_stop();
    ui_mode = UI_BROWSER;
    browser_full_redraw = true;
    redraw_needed = true;
    return;
  }
  if (button == ENCODER_BUTTON_SHORT && !audio_player_toggle_pause()) {
    show_result("PLAYBACK ERROR", audio_player_error_text());
    return;
  }
  draw_playback_dynamic(false);
}

static void disconnect_filesystem(void)
{
  audio_player_stop();
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

  const FRESULT result = f_mount(&USBHFatFS, USBHPath, 1u);
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
  audio_player_init();
  tft_ready = tft_init(&hspi1);
  ui_mode = UI_WAITING;
  redraw_needed = true;
  next_mount_attempt = HAL_GetTick();
}

void app_poll(void)
{
  const int32_t steps = encoder_take_steps();
  const EncoderButtonEvent button = encoder_take_button_event();

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

  if (ui_mode == UI_PLAYING) service_playback(steps, button);
  else if (ui_mode == UI_BROWSER) {
    move_browser_selection(steps);
    if (button == ENCODER_BUTTON_SHORT) activate_browser_item();
  } else if (ui_mode == UI_RESULT && button == ENCODER_BUTTON_SHORT) {
    (void)load_directory();
  }
  render();
}
