#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*LvglPortEncoderTurnCb)(int32_t steps);

bool lvgl_port_init(void);
lv_group_t *lvgl_port_encoder_group(void);
void lvgl_port_set_encoder_turn_cb(LvglPortEncoderTurnCb callback);

#endif
