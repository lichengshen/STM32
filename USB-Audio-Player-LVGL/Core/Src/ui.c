#include "ui.h"

#include "lvgl_port.h"

#include <stddef.h>

#define UI_WIDTH 240
#define UI_HEIGHT 320
#define UI_MAX_TEXT 48u

static lv_obj_t *current_screen;
static lv_obj_t *playback_time_label;
static lv_obj_t *playback_state_label;
static lv_obj_t *playback_progress;
static UiAction pending_action;
static uint16_t pending_action_value;
static bool ignore_playback_click;

static lv_style_t style_screen;
static lv_style_t style_title;
static lv_style_t style_text;
static lv_style_t style_muted;
static lv_style_t style_warning;
static lv_style_t style_error;
static lv_style_t style_row;
static lv_style_t style_row_focused;
static lv_style_t style_directory;
static lv_style_t style_file;
static lv_style_t style_progress_bg;
static lv_style_t style_progress_indicator;

static char playback_time_text[UI_MAX_TEXT];
static char playback_state_text[UI_MAX_TEXT];

static void copy_string(char *destination, uint16_t capacity, const char *source)
{
  if (capacity == 0u) return;
  uint16_t index = 0u;
  while (source != NULL && source[index] != '\0' && index + 1u < capacity) {
    destination[index] = source[index];
    ++index;
  }
  destination[index] = '\0';
}

static void append_u8(char *destination, uint16_t capacity, uint8_t value)
{
  uint16_t index = 0u;
  while (index < capacity && destination[index] != '\0') ++index;
  if (index + 1u >= capacity) return;
  if (value >= 100u && index + 1u < capacity) destination[index++] = '1';
  if (value >= 10u && index + 1u < capacity) destination[index++] = (char)('0' + (value / 10u) % 10u);
  if (index + 1u < capacity) destination[index++] = (char)('0' + value % 10u);
  destination[index] = '\0';
}

static void queue_action(UiAction action, uint16_t value)
{
  if (pending_action == UI_ACTION_NONE) {
    pending_action = action;
    pending_action_value = value;
  }
}

static void init_styles(void)
{
  lv_style_init(&style_screen);
  lv_style_set_bg_color(&style_screen, lv_color_black());
  lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
  lv_style_set_border_width(&style_screen, 0);
  lv_style_set_pad_all(&style_screen, 0);

  lv_style_init(&style_title);
  lv_style_set_text_color(&style_title, lv_color_hex(0x00d8ff));
  lv_style_set_text_font(&style_title, &lv_font_montserrat_20);

  lv_style_init(&style_text);
  lv_style_set_text_color(&style_text, lv_color_white());
  lv_style_set_text_font(&style_text, &lv_font_montserrat_14);

  lv_style_init(&style_muted);
  lv_style_set_text_color(&style_muted, lv_color_hex(0x9aa0a6));
  lv_style_set_text_font(&style_muted, &lv_font_montserrat_14);

  lv_style_init(&style_warning);
  lv_style_set_text_color(&style_warning, lv_color_hex(0xffd740));
  lv_style_set_text_font(&style_warning, &lv_font_montserrat_14);

  lv_style_init(&style_error);
  lv_style_set_text_color(&style_error, lv_color_hex(0xff5252));
  lv_style_set_text_font(&style_error, &lv_font_montserrat_14);

  lv_style_init(&style_row);
  lv_style_set_bg_color(&style_row, lv_color_black());
  lv_style_set_bg_opa(&style_row, LV_OPA_COVER);
  lv_style_set_border_color(&style_row, lv_color_hex(0x30343a));
  lv_style_set_border_width(&style_row, 1);
  lv_style_set_radius(&style_row, 0);
  lv_style_set_pad_all(&style_row, 0);

  lv_style_init(&style_row_focused);
  lv_style_set_bg_color(&style_row_focused, lv_color_hex(0x155c9c));
  lv_style_set_bg_opa(&style_row_focused, LV_OPA_COVER);
  lv_style_set_border_color(&style_row_focused, lv_color_hex(0x00d8ff));
  lv_style_set_border_width(&style_row_focused, 1);

  lv_style_init(&style_directory);
  lv_style_set_text_color(&style_directory, lv_color_hex(0x00d8ff));
  lv_style_set_text_font(&style_directory, &lv_font_montserrat_14);

  lv_style_init(&style_file);
  lv_style_set_text_color(&style_file, lv_color_hex(0xaeb4bb));
  lv_style_set_text_font(&style_file, &lv_font_montserrat_14);

  lv_style_init(&style_progress_bg);
  lv_style_set_bg_color(&style_progress_bg, lv_color_hex(0x42474d));
  lv_style_set_bg_opa(&style_progress_bg, LV_OPA_COVER);
  lv_style_set_radius(&style_progress_bg, 2);

  lv_style_init(&style_progress_indicator);
  lv_style_set_bg_color(&style_progress_indicator, lv_color_hex(0x00c853));
  lv_style_set_bg_opa(&style_progress_indicator, LV_OPA_COVER);
  lv_style_set_radius(&style_progress_indicator, 2);
}

static lv_obj_t *new_screen(void)
{
  lv_group_t *group = lvgl_port_encoder_group();
  if (group != NULL) lv_group_remove_all_objs(group);

  lv_obj_t *old_screen = current_screen;
  current_screen = lv_obj_create(NULL);
  lv_obj_add_style(current_screen, &style_screen, LV_PART_MAIN);
  lv_obj_set_size(current_screen, UI_WIDTH, UI_HEIGHT);
  lv_screen_load(current_screen);
  if (old_screen != NULL) lv_obj_delete(old_screen);

  playback_time_label = NULL;
  playback_state_label = NULL;
  playback_progress = NULL;
  pending_action = UI_ACTION_NONE;
  ignore_playback_click = false;
  return current_screen;
}

static lv_obj_t *add_label(lv_obj_t *parent, const char *text, lv_style_t *style,
                           int16_t x, int16_t y, int16_t width)
{
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_add_style(label, style, LV_PART_MAIN);
  lv_label_set_text_static(label, text == NULL ? "" : text);
  lv_obj_set_width(label, width);
  lv_obj_set_pos(label, x, y);
  return label;
}

static lv_obj_t *add_action_button(lv_obj_t *parent, const char *text, int16_t x, int16_t y,
                                   int16_t width, int16_t height, lv_event_cb_t event_cb,
                                   void *event_data, lv_style_t *text_style)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_add_style(button, &style_row, LV_PART_MAIN);
  lv_obj_add_style(button, &style_row_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, event_data);
  lv_group_t *group = lvgl_port_encoder_group();
  if (group != NULL) lv_group_add_obj(group, button);

  lv_obj_t *label = add_label(button, text, text_style, 4, 2, width - 8);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
  return button;
}

static void browser_item_event(lv_event_t *event)
{
  queue_action(UI_ACTION_BROWSER_ITEM,
               (uint16_t)(uintptr_t)lv_event_get_user_data(event));
}

static void result_event(lv_event_t *event)
{
  LV_UNUSED(event);
  queue_action(UI_ACTION_RESULT_RETURN, 0u);
}

static void playback_event(lv_event_t *event)
{
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_LONG_PRESSED) {
    ignore_playback_click = true;
    queue_action(UI_ACTION_PLAYBACK_STOP, 0u);
  } else if (code == LV_EVENT_CLICKED) {
    if (ignore_playback_click) {
      ignore_playback_click = false;
      return;
    }
    queue_action(UI_ACTION_PLAYBACK_TOGGLE, 0u);
  }
}

void ui_init(void)
{
  init_styles();
  current_screen = NULL;
  pending_action = UI_ACTION_NONE;
}

void ui_show_waiting(void)
{
  lv_obj_t *screen = new_screen();
  add_label(screen, "USB AUDIO PLAYER", &style_title, 8, 10, 224);
  add_label(screen, "WAITING FOR USB DRIVE", &style_warning, 10, 62, 220);
  add_label(screen, "CN5 HOST / MSC / FAT32", &style_text, 10, 88, 220);
  add_label(screen, "INSERT A WAV DRIVE", &style_muted, 10, 116, 220);
}

void ui_show_mount_error(const char *detail)
{
  lv_obj_t *screen = new_screen();
  add_label(screen, "USB MOUNT ERROR", &style_title, 8, 10, 224);
  add_label(screen, detail, &style_warning, 10, 62, 220);
  add_label(screen, "CHECK FAT32 DRIVE", &style_text, 10, 96, 220);
  add_label(screen, "RETRYING...", &style_muted, 10, 122, 220);
}

void ui_show_browser(const char *path, const UiBrowserItem *items, uint16_t item_count,
                     bool has_parent, bool truncated)
{
  lv_obj_t *screen = new_screen();
  add_label(screen, "USB WAV BROWSER", &style_title, 4, 3, 232);
  lv_obj_t *path_label = add_label(screen, path, &style_text, 4, 28, 232);
  lv_label_set_long_mode(path_label, LV_LABEL_LONG_MODE_DOTS);

  lv_obj_t *list = lv_obj_create(screen);
  lv_obj_add_style(list, &style_screen, LV_PART_MAIN);
  lv_obj_set_pos(list, 3, 51);
  lv_obj_set_size(list, 234, 220);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

  uint16_t item = 0u;
  lv_obj_t *first = add_action_button(list, "REFRESH DIRECTORY", 0, 0, 228, 22,
                                      browser_item_event, (void *)(uintptr_t)item, &style_text);
  ++item;
  if (has_parent) {
    (void)add_action_button(list, "..  PARENT DIRECTORY", 0, (int16_t)(item * 24u), 228, 22,
                            browser_item_event, (void *)(uintptr_t)item, &style_directory);
    ++item;
  }
  for (uint16_t index = 0u; index < item_count; ++index) {
    lv_style_t *text_style = items[index].directory ? &style_directory :
                             (items[index].playable ? &style_text : &style_file);
    (void)add_action_button(list, items[index].name, 0, (int16_t)(item * 24u), 228, 22,
                            browser_item_event, (void *)(uintptr_t)item, text_style);
    ++item;
  }
  lv_group_focus_obj(first);

  if (truncated) add_label(screen, "FIRST 64 ENTRIES", &style_warning, 4, 279, 232);
  else add_label(screen, "TURN=SELECT   PRESS=OPEN", &style_muted, 4, 279, 232);
  add_label(screen, "PRESS REFRESH TO RELOAD", &style_muted, 4, 300, 232);
}

void ui_show_result(const char *title, const char *detail)
{
  lv_obj_t *screen = new_screen();
  add_label(screen, title, &style_title, 8, 14, 224);
  add_label(screen, detail, &style_text, 8, 62, 224);
  lv_obj_t *button = add_action_button(screen, "PRESS TO RETURN", 8, 110, 224, 28,
                                        result_event, NULL, &style_muted);
  lv_group_focus_obj(button);
}

void ui_show_playback(const char *name)
{
  lv_obj_t *screen = new_screen();
  add_label(screen, "NOW PLAYING", &style_title, 4, 4, 232);
  lv_obj_t *name_label = add_label(screen, name, &style_text, 4, 33, 232);
  lv_label_set_long_mode(name_label, LV_LABEL_LONG_MODE_DOTS);
  add_label(screen, "PCM WAV / I2S DMA", &style_muted, 4, 58, 232);

  copy_string(playback_time_text, sizeof(playback_time_text), "0:00 / 0:00");
  playback_time_label = add_label(screen, playback_time_text, &style_text, 8, 94, 224);

  playback_progress = lv_bar_create(screen);
  lv_obj_add_style(playback_progress, &style_progress_bg, LV_PART_MAIN);
  lv_obj_add_style(playback_progress, &style_progress_indicator, LV_PART_INDICATOR);
  lv_obj_set_pos(playback_progress, 10, 122);
  lv_obj_set_size(playback_progress, 220, 10);
  lv_bar_set_range(playback_progress, 0, 100);
  lv_bar_set_value(playback_progress, 0, LV_ANIM_OFF);

  copy_string(playback_state_text, sizeof(playback_state_text), "PLAYING VOL 70");
  playback_state_label = add_label(screen, playback_state_text, &style_text, 8, 254, 224);
  lv_obj_t *button = add_action_button(screen, "PRESS=PAUSE   HOLD=STOP", 8, 282, 224, 28,
                                        playback_event, NULL, &style_muted);
  lv_obj_add_event_cb(button, playback_event, LV_EVENT_LONG_PRESSED, NULL);
  lv_group_focus_obj(button);
}

void ui_update_playback(const char *time_text, uint8_t progress, bool paused, uint8_t volume)
{
  if (playback_time_label == NULL || playback_state_label == NULL || playback_progress == NULL)
    return;
  copy_string(playback_time_text, sizeof(playback_time_text), time_text);
  lv_label_set_text_static(playback_time_label, playback_time_text);

  copy_string(playback_state_text, sizeof(playback_state_text), paused ? "PAUSED VOL " : "PLAYING VOL ");
  append_u8(playback_state_text, sizeof(playback_state_text), volume);
  lv_label_set_text_static(playback_state_label, playback_state_text);
  lv_obj_remove_style(playback_state_label, &style_text, LV_PART_MAIN);
  lv_obj_remove_style(playback_state_label, &style_warning, LV_PART_MAIN);
  lv_obj_add_style(playback_state_label, paused ? &style_warning : &style_text, LV_PART_MAIN);
  if (progress > 100u) progress = 100u;
  lv_bar_set_value(playback_progress, progress, LV_ANIM_OFF);
}

UiAction ui_take_action(uint16_t *value)
{
  const UiAction action = pending_action;
  if (value != NULL) *value = pending_action_value;
  pending_action = UI_ACTION_NONE;
  pending_action_value = 0u;
  return action;
}
