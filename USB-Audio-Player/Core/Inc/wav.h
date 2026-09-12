#ifndef WAV_H
#define WAV_H

#include "ff.h"

#include <stdint.h>

typedef enum {
  WAV_OK = 0,
  WAV_ERR_IO,
  WAV_ERR_RIFF,
  WAV_ERR_TRUNCATED,
  WAV_ERR_FORMAT,
  WAV_ERR_RATE,
  WAV_ERR_EMPTY,
} WavResult;

typedef struct {
  FSIZE_t data_offset;
  uint32_t data_size;
  uint32_t sample_rate;
  uint32_t total_frames;
  uint16_t channels;
} WavStreamInfo;

WavResult wav_open(FIL *file, WavStreamInfo *stream);
const char *wav_result_text(WavResult result);

#endif
