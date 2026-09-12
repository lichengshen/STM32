#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include "wav.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  AUDIO_PLAYER_EVENT_NONE = 0,
  AUDIO_PLAYER_EVENT_FINISHED,
  AUDIO_PLAYER_EVENT_ERROR,
} AudioPlayerEvent;

void audio_player_init(void);
bool audio_player_open(const char *path);
bool audio_player_start(void);
void audio_player_service(void);
void audio_player_stop(void);
bool audio_player_toggle_pause(void);
bool audio_player_is_playing(void);
bool audio_player_is_paused(void);
uint8_t audio_player_volume(void);
bool audio_player_change_volume(int32_t steps);
const WavStreamInfo *audio_player_stream(void);
uint32_t audio_player_elapsed_frames(void);
AudioPlayerEvent audio_player_take_event(void);
const char *audio_player_error_text(void);

#endif
