#include "wav.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void put_u16(uint8_t *at, uint16_t value)
{
  at[0] = (uint8_t)value;
  at[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *at, uint32_t value)
{
  at[0] = (uint8_t)value;
  at[1] = (uint8_t)(value >> 8);
  at[2] = (uint8_t)(value >> 16);
  at[3] = (uint8_t)(value >> 24);
}

static uint32_t build_pcm_wav(uint8_t *data, uint16_t format, uint16_t channels,
                              uint32_t rate, int with_odd_junk)
{
  memset(data, 0, 128u);
  memcpy(data, "RIFF", 4u);
  memcpy(&data[8], "WAVE", 4u);
  memcpy(&data[12], "fmt ", 4u);
  put_u32(&data[16], 16u);
  put_u16(&data[20], format);
  put_u16(&data[22], channels);
  put_u32(&data[24], rate);
  put_u32(&data[28], rate * channels * 2u);
  put_u16(&data[32], channels * 2u);
  put_u16(&data[34], 16u);
  uint32_t at = 36u;
  if (with_odd_junk) {
    memcpy(&data[at], "JUNK", 4u);
    put_u32(&data[at + 4u], 3u);
    data[at + 8u] = 0x11u;
    data[at + 9u] = 0x22u;
    data[at + 10u] = 0x33u;
    at += 12u;
  }
  memcpy(&data[at], "data", 4u);
  put_u32(&data[at + 4u], channels * 2u * 2u);
  for (uint32_t i = 0u; i < channels * 2u * 2u; ++i) data[at + 8u + i] = (uint8_t)i;
  const uint32_t size = at + 8u + channels * 2u * 2u;
  put_u32(&data[4], size - 8u);
  return size;
}

static WavResult parse(const uint8_t *data, uint32_t size, WavStreamInfo *stream)
{
  FIL file = {.data = data, .size = size, .position = 0u};
  return wav_open(&file, stream);
}

int main(void)
{
  uint8_t data[128];
  WavStreamInfo stream;
  const uint32_t supported_rates[] = {
      8000u, 11025u, 16000u, 22050u, 32000u, 44100u, 48000u,
  };

  uint32_t size = build_pcm_wav(data, 1u, 2u, 44100u, 1);
  assert(parse(data, size, &stream) == WAV_OK);
  assert(stream.sample_rate == 44100u && stream.channels == 2u);
  assert(stream.data_offset == 56u && stream.data_size == 8u && stream.total_frames == 2u);

  for (uint32_t index = 0u; index < sizeof(supported_rates) / sizeof(supported_rates[0]);
       ++index) {
    size = build_pcm_wav(data, 1u, 1u, supported_rates[index], 0);
    assert(parse(data, size, &stream) == WAV_OK);
    assert(stream.sample_rate == supported_rates[index]);
    assert(stream.channels == 1u && stream.total_frames == 2u);
  }

  size = build_pcm_wav(data, 3u, 2u, 48000u, 0);
  assert(parse(data, size, &stream) == WAV_ERR_FORMAT);

  size = build_pcm_wav(data, 1u, 2u, 96000u, 0);
  assert(parse(data, size, &stream) == WAV_ERR_RATE);

  size = build_pcm_wav(data, 1u, 2u, 48000u, 1);
  assert(parse(data, size - 1u, &stream) == WAV_ERR_TRUNCATED);

  size = build_pcm_wav(data, 1u, 2u, 48000u, 0);
  put_u32(&data[4], size);
  assert(parse(data, size, &stream) == WAV_ERR_TRUNCATED);

  memcpy(data, "NOPE", 4u);
  assert(parse(data, 16u, &stream) == WAV_ERR_RIFF);

  puts("wav parser tests passed");
  return 0;
}
