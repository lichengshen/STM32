#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  const char *name;
  bool directory;
  bool playable;
} UiBrowserItem;

typedef enum {
  UI_ACTION_NONE = 0,
  UI_ACTION_BROWSER_ITEM,
  UI_ACTION_RESULT_RETURN,
  UI_ACTION_PLAYBACK_TOGGLE,
  UI_ACTION_PLAYBACK_STOP,
} UiAction;

void ui_init(void);
void ui_show_waiting(void);
void ui_show_mount_error(const char *detail);
void ui_show_browser(const char *path, const UiBrowserItem *items, uint16_t item_count,
                     bool has_parent, bool truncated);
void ui_show_result(const char *title, const char *detail);
void ui_show_playback(const char *name);
void ui_update_playback(const char *time_text, uint8_t progress, bool paused, uint8_t volume);
UiAction ui_take_action(uint16_t *value);

#endif
