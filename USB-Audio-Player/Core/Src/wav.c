#include "wav.h"

#include <stdbool.h>
#include <string.h>

static uint16_t read_u16le(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32le(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool is_supported_rate(uint32_t rate)
{
  switch (rate) {
  case 8000u:
  case 11025u:
  case 16000u:
  case 22050u:
  case 32000u:
  case 44100u:
  case 48000u:
    return true;
  default:
    return false;
  }
}

static WavResult read_at(FIL *file, FSIZE_t offset, void *buffer, UINT length)
{
  UINT read = 0u;
  if (f_lseek(file, offset) != FR_OK) return WAV_ERR_IO;
  if (f_read(file, buffer, length, &read) != FR_OK) return WAV_ERR_IO;
  return read == length ? WAV_OK : WAV_ERR_TRUNCATED;
}

WavResult wav_open(FIL *file, WavStreamInfo *stream)
{
  uint8_t header[12];
  uint8_t chunk_header[8];
  uint8_t fmt[16];
  const FSIZE_t file_size = f_size(file);
  FSIZE_t riff_end;
  FSIZE_t offset = 12u;
  bool fmt_found = false;
  bool data_found = false;
  uint16_t format = 0u;
  uint16_t channels = 0u;
  uint16_t block_align = 0u;
  uint16_t bits_per_sample = 0u;
  uint32_t sample_rate = 0u;
  uint32_t byte_rate = 0u;
  uint32_t data_size = 0u;
  FSIZE_t data_offset = 0u;

  if (file == NULL || stream == NULL) return WAV_ERR_FORMAT;
  memset(stream, 0, sizeof(*stream));
  if (file_size < sizeof(header)) return WAV_ERR_TRUNCATED;
  WavResult result = read_at(file, 0u, header, sizeof(header));
  if (result != WAV_OK) return result;
  if (memcmp(header, "RIFF", 4u) != 0 || memcmp(&header[8], "WAVE", 4u) != 0)
    return WAV_ERR_RIFF;
  const uint32_t riff_size = read_u32le(&header[4]);
  if (riff_size < 4u) return WAV_ERR_FORMAT;
  if ((FSIZE_t)riff_size > file_size - 8u) return WAV_ERR_TRUNCATED;
  riff_end = 8u + (FSIZE_t)riff_size;

  while (offset + sizeof(chunk_header) <= riff_end) {
    result = read_at(file, offset, chunk_header, sizeof(chunk_header));
    if (result != WAV_OK) return result;
    const uint32_t chunk_size = read_u32le(&chunk_header[4]);
    const FSIZE_t payload = offset + sizeof(chunk_header);
    if (payload > riff_end || (FSIZE_t)chunk_size > riff_end - payload)
      return WAV_ERR_TRUNCATED;

    if (memcmp(chunk_header, "fmt ", 4u) == 0 && !fmt_found) {
      if (chunk_size < sizeof(fmt)) return WAV_ERR_FORMAT;
      result = read_at(file, payload, fmt, sizeof(fmt));
      if (result != WAV_OK) return result;
      format = read_u16le(&fmt[0]);
      channels = read_u16le(&fmt[2]);
      sample_rate = read_u32le(&fmt[4]);
      byte_rate = read_u32le(&fmt[8]);
      block_align = read_u16le(&fmt[12]);
      bits_per_sample = read_u16le(&fmt[14]);
      fmt_found = true;
    } else if (memcmp(chunk_header, "data", 4u) == 0 && !data_found) {
      data_offset = payload;
      data_size = chunk_size;
      data_found = true;
    }

    offset = payload + (FSIZE_t)chunk_size;
    if ((chunk_size & 1u) != 0u) {
      if (offset == riff_end) return WAV_ERR_TRUNCATED;
      ++offset;
    }
  }

  if (offset != riff_end) return WAV_ERR_TRUNCATED;
  if (!fmt_found || !data_found) return WAV_ERR_FORMAT;
  if (!is_supported_rate(sample_rate)) return WAV_ERR_RATE;
  if (format != 1u || (channels != 1u && channels != 2u) ||
      bits_per_sample != 16u || block_align != (uint16_t)(channels * 2u) ||
      byte_rate != sample_rate * block_align)
    return WAV_ERR_FORMAT;
  if (data_size == 0u) return WAV_ERR_EMPTY;
  if (data_size % block_align != 0u) return WAV_ERR_TRUNCATED;

  stream->data_offset = data_offset;
  stream->data_size = data_size;
  stream->sample_rate = sample_rate;
  stream->channels = channels;
  stream->total_frames = data_size / block_align;
  return WAV_OK;
}

const char *wav_result_text(WavResult result)
{
  switch (result) {
  case WAV_OK: return "OK";
  case WAV_ERR_IO: return "FILE I/O ERROR";
  case WAV_ERR_RIFF: return "NOT RIFF/WAVE";
  case WAV_ERR_TRUNCATED: return "TRUNCATED WAV";
  case WAV_ERR_FORMAT: return "UNSUPPORTED WAV";
  case WAV_ERR_RATE: return "UNSUPPORTED RATE";
  case WAV_ERR_EMPTY: return "EMPTY WAV";
  default: return "WAV ERROR";
  }
}
