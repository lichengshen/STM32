#ifndef CODEC_CS43L22_H
#define CODEC_CS43L22_H

#include <stdbool.h>
#include <stdint.h>

bool codec_cs43l22_init(uint8_t volume);
bool codec_cs43l22_start(void);
bool codec_cs43l22_pause(void);
bool codec_cs43l22_set_volume(uint8_t volume);
void codec_cs43l22_stop(void);

#endif
