#include "app.h"

#include "audio_player.h"
#include "encoder.h"
#include "fatfs.h"
#include "lvgl_port.h"
#include "main.h"
#include "ui.h"
#include "usb_host.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define APP_PATH_CAPACITY     256u
#define APP_MAX_ENTRIES       64u
#define APP_MOUNT_RETRY_MS    1000u
#define APP_PLAYBACK_UI_MS    50u
#define FATFS_ATTR_VOLUME_ID  0x08u

typedef enum {
  APP_UI_WAITING,
  APP_UI_BROWSER,
  APP_UI_PLAYING,
  APP_UI_RESULT,
  APP_UI_MOUNT_ERROR,
} AppUiMode;

typedef struct {
  char name[_MAX_LFN + 1u];
  FSIZE_t size;
  BYTE attributes;
} BrowserEntry;

extern ApplicationTypeDef Appli_state;
extern TIM_HandleTypeDef htim1;

static BrowserEntry entries[APP_MAX_ENTRIES];
static UiBrowserItem ui_entries[APP_MAX_ENTRIES];
static uint16_t entry_count;
static bool directory_truncated;
static bool filesystem_mounted;
static bool lvgl_ready;
static AppUiMode ui_mode;
static uint32_t next_mount_attempt;
static uint32_t last_playback_ui;
static int32_t pending_volume_steps;

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

static void show_waiting(void)
{
  lvgl_port_set_encoder_turn_cb(NULL);
  ui_mode = APP_UI_WAITING;
  ui_show_waiting();
}

static void show_result(const char *title, const char *detail)
{
  copy_string(result_title, sizeof(result_title), title);
  copy_string(result_detail, sizeof(result_detail), detail);
  lvgl_port_set_encoder_turn_cb(NULL);
  ui_mode = APP_UI_RESULT;
  ui_show_result(result_title, result_detail);
}

static void show_fresult(const char *title, FRESULT result)
{
  show_result(title, fresult_text(result));
}

static void show_browser(void)
{
  for (uint16_t index = 0u; index < entry_count; ++index) {
    ui_entries[index].name = entries[index].name;
    ui_entries[index].directory = (entries[index].attributes & AM_DIR) != 0u;
    ui_entries[index].playable = is_wav_file(entries[index].name);
  }
  lvgl_port_set_encoder_turn_cb(NULL);
  ui_mode = APP_UI_BROWSER;
  ui_show_browser(current_path, ui_entries, entry_count, !path_is_root(), directory_truncated);
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

  show_browser();
  return true;
}

static void append_time(char *buffer, uint16_t capacity, uint32_t frames, uint32_t rate)
{
  const uint32_t seconds = rate == 0u ? 0u : frames / rate;
  append_u32(buffer, capacity, seconds / 60u);
  append_string(buffer, capacity, ":");
  if (seconds % 60u < 10u) append_string(buffer, capacity, "0");
  append_u32(buffer, capacity, seconds % 60u);
}

static void update_playback_ui(bool force)
{
  const uint32_t now = HAL_GetTick();
  if (!force && (uint32_t)(now - last_playback_ui) < APP_PLAYBACK_UI_MS) return;
  last_playback_ui = now;

  const WavStreamInfo *stream = audio_player_stream();
  char elapsed[12] = "";
  char total[12] = "";
  char time_text[30] = "";
  append_time(elapsed, sizeof(elapsed), audio_player_elapsed_frames(), stream->sample_rate);
  append_time(total, sizeof(total), stream->total_frames, stream->sample_rate);
  append_string(time_text, sizeof(time_text), elapsed);
  append_string(time_text, sizeof(time_text), " / ");
  append_string(time_text, sizeof(time_text), total);

  uint32_t progress = 0u;
  if (stream->total_frames != 0u) {
    progress = (uint32_t)(((uint64_t)audio_player_elapsed_frames() * 100u) /
                          stream->total_frames);
  }
  if (progress > 100u) progress = 100u;
  ui_update_playback(time_text, (uint8_t)progress, audio_player_is_paused(),
                     audio_player_volume());
}

static void collect_playback_volume_steps(int32_t steps)
{
  if (steps > 0 && pending_volume_steps > INT32_MAX - steps) {
    pending_volume_steps = INT32_MAX;
  } else if (steps < 0 && pending_volume_steps < INT32_MIN - steps) {
    pending_volume_steps = INT32_MIN;
  } else {
    pending_volume_steps += steps;
  }
}

static void start_selected_wav(const char *path, const char *name)
{
  if (!audio_player_open(path)) {
    show_result("WAV ERROR", audio_player_error_text());
    return;
  }
  copy_string(playing_name, sizeof(playing_name), name);
  ui_mode = APP_UI_PLAYING;
  ui_show_playback(playing_name);
  lvgl_port_set_encoder_turn_cb(collect_playback_volume_steps);
  pending_volume_steps = 0;
  last_playback_ui = 0u;
  if (!audio_player_start()) {
    show_result("PLAYBACK ERROR", audio_player_error_text());
    return;
  }
  update_playback_ui(true);
}

static void activate_browser_item(uint16_t selected)
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

static void stop_playback_to_browser(void)
{
  lvgl_port_set_encoder_turn_cb(NULL);
  pending_volume_steps = 0;
  audio_player_stop();
  (void)load_directory();
}

static void service_playback(void)
{
  audio_player_service();
  const AudioPlayerEvent event = audio_player_take_event();
  if (event == AUDIO_PLAYER_EVENT_ERROR) {
    show_result("PLAYBACK ERROR", audio_player_error_text());
    return;
  }
  if (event == AUDIO_PLAYER_EVENT_FINISHED) {
    lvgl_port_set_encoder_turn_cb(NULL);
    pending_volume_steps = 0;
    (void)load_directory();
    return;
  }

  if (pending_volume_steps != 0) {
    const int32_t steps = pending_volume_steps;
    pending_volume_steps = 0;
    if (!audio_player_change_volume(steps)) {
      show_result("PLAYBACK ERROR", audio_player_error_text());
      return;
    }
    update_playback_ui(true);
  }
  update_playback_ui(false);
}

static void disconnect_filesystem(void)
{
  lvgl_port_set_encoder_turn_cb(NULL);
  pending_volume_steps = 0;
  audio_player_stop();
  if (filesystem_mounted) (void)f_mount(NULL, USBHPath, 0u);
  filesystem_mounted = false;
  show_waiting();
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
  lvgl_port_set_encoder_turn_cb(NULL);
  ui_mode = APP_UI_MOUNT_ERROR;
  ui_show_mount_error(result_detail);
}

static void handle_ui_action(void)
{
  uint16_t selected = 0u;
  const UiAction action = ui_take_action(&selected);
  if (action == UI_ACTION_NONE) return;

  if (ui_mode == APP_UI_BROWSER && action == UI_ACTION_BROWSER_ITEM) {
    activate_browser_item(selected);
  } else if (ui_mode == APP_UI_RESULT && action == UI_ACTION_RESULT_RETURN) {
    (void)load_directory();
  } else if (ui_mode == APP_UI_PLAYING && action == UI_ACTION_PLAYBACK_TOGGLE) {
    if (!audio_player_toggle_pause()) show_result("PLAYBACK ERROR", audio_player_error_text());
    else update_playback_ui(true);
  } else if (ui_mode == APP_UI_PLAYING && action == UI_ACTION_PLAYBACK_STOP) {
    stop_playback_to_browser();
  }
}

void app_init(void)
{
  /* PB3 is also an optional SWO pin. The LCD owns it in this application. */
  DBGMCU->CR &= ~DBGMCU_CR_TRACE_IOEN;
  encoder_init(&htim1);
  audio_player_init();
  lv_init();
  lvgl_ready = lvgl_port_init();
  ui_init();
  show_waiting();
  next_mount_attempt = HAL_GetTick();
}

void app_poll(void)
{
  encoder_poll();

  if (Appli_state != APPLICATION_READY) {
    if (filesystem_mounted || ui_mode != APP_UI_WAITING) disconnect_filesystem();
  } else if (!filesystem_mounted) {
    try_mount();
  } else {
    handle_ui_action();
    if (ui_mode == APP_UI_PLAYING) service_playback();
  }

  if (lvgl_ready) (void)lv_timer_handler();
}
