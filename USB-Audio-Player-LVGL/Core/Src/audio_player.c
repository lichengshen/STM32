#include "audio_player.h"

#include "codec_cs43l22.h"
#include "main.h"

#include <string.h>

#define AUDIO_HALF_BYTES       16384u
#define AUDIO_HALF_WORDS       (AUDIO_HALF_BYTES / sizeof(uint16_t))
#define AUDIO_TOTAL_WORDS      (AUDIO_HALF_WORDS * 2u)
#define AUDIO_HALF_0           0x01u
#define AUDIO_HALF_1           0x02u
#define AUDIO_NO_FINAL_HALF    0xffu

typedef enum {
  PLAYER_STOPPED,
  PLAYER_PREPARED,
  PLAYER_PLAYING,
  PLAYER_PAUSED,
} PlayerState;

typedef struct {
  uint32_t rate;
  uint16_t plli2sn;
  uint8_t plli2sr;
} I2sClock;

static const I2sClock i2s_clocks[] = {
    {8000u, 256u, 5u}, {11025u, 429u, 4u}, {16000u, 213u, 4u},
    {22050u, 429u, 4u}, {32000u, 426u, 4u}, {44100u, 271u, 6u},
    {48000u, 258u, 3u},
};

/* This ordinary .bss allocation is in SRAM at 0x20000000, which DMA1 can use.
 * Do not move it to CCM RAM; DMA1 cannot access the F407 CCM region. */
static uint16_t audio_dma[AUDIO_TOTAL_WORDS] __attribute__((aligned(4)));
static FIL audio_file;
static WavStreamInfo stream;
static PlayerState state;
static AudioPlayerEvent pending_event;
static const char *last_error;
static bool file_open;
static uint8_t volume = 70u;
static uint8_t final_half;
static uint32_t source_bytes_read;
static uint32_t played_frames;
static uint32_t half_frames[2];
static volatile uint8_t completed_halves;
static volatile bool dma_fault;

extern I2S_HandleTypeDef hi2s3;

static const I2sClock *clock_for_rate(uint32_t rate)
{
  for (uint32_t index = 0u; index < sizeof(i2s_clocks) / sizeof(i2s_clocks[0]); ++index) {
    if (i2s_clocks[index].rate == rate) return &i2s_clocks[index];
  }
  return NULL;
}

static bool configure_i2s(uint32_t rate)
{
  const I2sClock *clock = clock_for_rate(rate);
  if (clock == NULL) return false;

  RCC_PeriphCLKInitTypeDef clock_config = {0};
  HAL_RCCEx_GetPeriphCLKConfig(&clock_config);
  clock_config.PeriphClockSelection = RCC_PERIPHCLK_I2S;
  clock_config.PLLI2S.PLLI2SN = clock->plli2sn;
  clock_config.PLLI2S.PLLI2SR = clock->plli2sr;

  __HAL_I2S_DISABLE(&hi2s3);
  if (HAL_RCCEx_PeriphCLKConfig(&clock_config) != HAL_OK) return false;
  hi2s3.Init.AudioFreq = rate;
  return HAL_I2S_Init(&hi2s3) == HAL_OK;
}

static void close_file(void)
{
  if (file_open) {
    (void)f_close(&audio_file);
    file_open = false;
  }
}

static void stop_output(void)
{
  if (state != PLAYER_STOPPED) {
    (void)HAL_I2S_DMAStop(&hi2s3);
    codec_cs43l22_stop();
  }
}

static void reset_transfer_state(void)
{
  source_bytes_read = 0u;
  played_frames = 0u;
  half_frames[0] = 0u;
  half_frames[1] = 0u;
  final_half = AUDIO_NO_FINAL_HALF;
  completed_halves = 0u;
  dma_fault = false;
  memset(audio_dma, 0, sizeof(audio_dma));
}

static void fail(const char *message)
{
  stop_output();
  close_file();
  state = PLAYER_STOPPED;
  last_error = message;
  pending_event = AUDIO_PLAYER_EVENT_ERROR;
}

static bool fill_half(uint8_t half)
{
  const uint32_t index = half == AUDIO_HALF_0 ? 0u : 1u;
  int16_t *output = (int16_t *)&audio_dma[index * AUDIO_HALF_WORDS];
  const uint32_t remaining = stream.data_size - source_bytes_read;
  uint32_t source_limit = stream.channels == 1u ? AUDIO_HALF_BYTES / 2u : AUDIO_HALF_BYTES;
  if (source_limit > remaining) source_limit = remaining;

  if (source_limit == 0u) {
    memset(output, 0, AUDIO_HALF_BYTES);
    half_frames[index] = 0u;
    return true;
  }

  UINT bytes_read = 0u;
  if (f_read(&audio_file, output, (UINT)source_limit, &bytes_read) != FR_OK ||
      bytes_read != source_limit) {
    return false;
  }
  source_bytes_read += bytes_read;

  uint32_t frames;
  if (stream.channels == 1u) {
    frames = bytes_read / sizeof(int16_t);
    for (uint32_t frame = frames; frame != 0u; --frame) {
      const int16_t sample = output[frame - 1u];
      output[(frame - 1u) * 2u] = sample;
      output[(frame - 1u) * 2u + 1u] = sample;
    }
  } else {
    frames = bytes_read / (2u * sizeof(int16_t));
  }

  const uint32_t output_words = frames * 2u;
  if (output_words < AUDIO_HALF_WORDS)
    memset(&output[output_words], 0,
           (AUDIO_HALF_WORDS - output_words) * sizeof(output[0]));
  half_frames[index] = frames;

  if (source_bytes_read == stream.data_size && final_half == AUDIO_NO_FINAL_HALF)
    final_half = half;
  return true;
}

static uint8_t take_completed_halves(bool *fault)
{
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const uint8_t completed = completed_halves;
  completed_halves = 0u;
  *fault = dma_fault;
  dma_fault = false;
  if (primask == 0u) __enable_irq();
  return completed;
}

void audio_player_init(void)
{
  state = PLAYER_STOPPED;
  pending_event = AUDIO_PLAYER_EVENT_NONE;
  last_error = "";
  file_open = false;
  reset_transfer_state();
}

bool audio_player_open(const char *path)
{
  audio_player_stop();
  last_error = "";
  pending_event = AUDIO_PLAYER_EVENT_NONE;
  if (path == NULL || f_open(&audio_file, path, FA_READ) != FR_OK) {
    last_error = "OPEN WAV FAILED";
    return false;
  }
  file_open = true;
  const WavResult result = wav_open(&audio_file, &stream);
  if (result != WAV_OK) {
    last_error = wav_result_text(result);
    close_file();
    return false;
  }
  state = PLAYER_PREPARED;
  return true;
}

bool audio_player_start(void)
{
  if (state != PLAYER_PREPARED) return false;
  reset_transfer_state();
  if (f_lseek(&audio_file, stream.data_offset) != FR_OK) {
    fail("WAV SEEK FAILED");
    return false;
  }
  if (!configure_i2s(stream.sample_rate)) {
    fail("I2S CLOCK FAILED");
    return false;
  }
  if (!codec_cs43l22_init(volume) || !fill_half(AUDIO_HALF_0) ||
      !fill_half(AUDIO_HALF_1) || !codec_cs43l22_start()) {
    fail("AUDIO START FAILED");
    return false;
  }
  if (HAL_I2S_Transmit_DMA(&hi2s3, audio_dma, AUDIO_TOTAL_WORDS) != HAL_OK) {
    fail("I2S DMA FAILED");
    return false;
  }
  state = PLAYER_PLAYING;
  return true;
}

void audio_player_service(void)
{
  if (state != PLAYER_PLAYING) return;
  bool fault = false;
  const uint8_t completed = take_completed_halves(&fault);
  if (fault) {
    fail("AUDIO UNDERRUN");
    return;
  }

  const uint8_t halves[] = {AUDIO_HALF_0, AUDIO_HALF_1};
  for (uint32_t item = 0u; item < sizeof(halves); ++item) {
    const uint8_t half = halves[item];
    if ((completed & half) == 0u) continue;
    const uint32_t index = half == AUDIO_HALF_0 ? 0u : 1u;
    played_frames += half_frames[index];
    if (half == final_half) {
      stop_output();
      close_file();
      state = PLAYER_STOPPED;
      pending_event = AUDIO_PLAYER_EVENT_FINISHED;
      return;
    }
    if (!fill_half(half)) {
      fail("USB READ FAILED");
      return;
    }
  }
}

void audio_player_stop(void)
{
  stop_output();
  close_file();
  state = PLAYER_STOPPED;
  pending_event = AUDIO_PLAYER_EVENT_NONE;
  reset_transfer_state();
}

bool audio_player_toggle_pause(void)
{
  if (state == PLAYER_PLAYING) {
    /* The codec holds its hardware reset line while paused, silencing both
     * the DAC and headphone amplifier even though I2S remains clock master. */
    if (!codec_cs43l22_pause()) {
      fail("AUDIO PAUSE FAILED");
      return false;
    }
    if (HAL_I2S_DMAPause(&hi2s3) != HAL_OK) {
      fail("AUDIO PAUSE FAILED");
      return false;
    }
    state = PLAYER_PAUSED;
    return true;
  }
  if (state == PLAYER_PAUSED) {
    /* Reinitialize the codec while it is reset/muted, resume valid DMA data,
     * then release its DAC mute as part of codec_cs43l22_start(). */
    if (!codec_cs43l22_init(volume) || HAL_I2S_DMAResume(&hi2s3) != HAL_OK ||
        !codec_cs43l22_start()) {
      fail("AUDIO RESUME FAILED");
      return false;
    }
    state = PLAYER_PLAYING;
    return true;
  }
  return false;
}

bool audio_player_is_playing(void)
{
  return state == PLAYER_PLAYING || state == PLAYER_PAUSED;
}

bool audio_player_is_paused(void)
{
  return state == PLAYER_PAUSED;
}

uint8_t audio_player_volume(void)
{
  return volume;
}

bool audio_player_change_volume(int32_t steps)
{
  if (steps == 0) return true;
  int32_t next = (int32_t)volume + steps * 5;
  if (next < 0) next = 0;
  if (next > 100) next = 100;
  volume = (uint8_t)next;
  /* The codec is held in reset while paused; apply the remembered volume
   * during its resume initialization rather than attempting an I2C write. */
  if (state != PLAYER_PLAYING) return true;
  if (!codec_cs43l22_set_volume(volume)) {
    fail("VOLUME FAILED");
    return false;
  }
  return true;
}

const WavStreamInfo *audio_player_stream(void)
{
  return &stream;
}

uint32_t audio_player_elapsed_frames(void)
{
  return played_frames;
}

AudioPlayerEvent audio_player_take_event(void)
{
  const AudioPlayerEvent event = pending_event;
  pending_event = AUDIO_PLAYER_EVENT_NONE;
  return event;
}

const char *audio_player_error_text(void)
{
  return last_error;
}

static void record_completed_half(uint8_t half)
{
  if (state != PLAYER_PLAYING) return;
  if (completed_halves != 0u) dma_fault = true;
  completed_halves |= half;
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if (hi2s == &hi2s3) record_completed_half(AUDIO_HALF_0);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if (hi2s == &hi2s3) record_completed_half(AUDIO_HALF_1);
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
  if (hi2s == &hi2s3) dma_fault = true;
}
