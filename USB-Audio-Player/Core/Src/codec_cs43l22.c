#include "codec_cs43l22.h"

#include "main.h"

#define CS43L22_ADDRESS              0x94u
#define CS43L22_POWER_CTL1           0x02u
#define CS43L22_POWER_CTL2           0x04u
#define CS43L22_CLOCKING_CTL         0x05u
#define CS43L22_INTERFACE_CTL1       0x06u
#define CS43L22_ANALOG_ZC_SR_SETT    0x0au
#define CS43L22_PLAYBACK_CTL1        0x0du
#define CS43L22_MISC_CTL             0x0eu
#define CS43L22_PCMA_VOL             0x1au
#define CS43L22_PCMB_VOL             0x1bu
#define CS43L22_MASTER_A_VOL         0x20u
#define CS43L22_MASTER_B_VOL         0x21u
#define CS43L22_HEADPHONE_A_VOL      0x22u
#define CS43L22_HEADPHONE_B_VOL      0x23u
#define CS43L22_LIMIT_CTL1           0x27u
#define CS43L22_TONE_CTL             0x1fu

extern I2C_HandleTypeDef hi2c1;

static bool codec_ready;

static bool write_register(uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(&hi2c1, CS43L22_ADDRESS, reg,
                           I2C_MEMADD_SIZE_8BIT, &value, 1u, 100u) == HAL_OK;
}

static bool set_muted(bool muted)
{
  if (muted) {
    return write_register(CS43L22_POWER_CTL2, 0xffu) &&
           write_register(CS43L22_HEADPHONE_A_VOL, 0x01u) &&
           write_register(CS43L22_HEADPHONE_B_VOL, 0x01u);
  }
  return write_register(CS43L22_HEADPHONE_A_VOL, 0x00u) &&
         write_register(CS43L22_HEADPHONE_B_VOL, 0x00u) &&
         write_register(CS43L22_POWER_CTL2, 0xafu);
}

/* Playback Control 1 mutes the DAC data path itself.  This is distinct from
 * the headphone-output mute used while fully stopping the codec. */
static bool set_master_muted(bool muted)
{
  return write_register(CS43L22_PLAYBACK_CTL1, muted ? 0x03u : 0x00u);
}

bool codec_cs43l22_set_volume(uint8_t volume)
{
  if (volume > 100u) volume = 100u;
  const uint8_t converted = (uint8_t)(((uint16_t)volume * 255u) / 100u);
  const uint8_t register_value = converted > 0xe6u
      ? (uint8_t)(converted - 0xe7u)
      : (uint8_t)(converted + 0x19u);
  return write_register(CS43L22_MASTER_A_VOL, register_value) &&
         write_register(CS43L22_MASTER_B_VOL, register_value);
}

bool codec_cs43l22_init(uint8_t volume)
{
  HAL_GPIO_WritePin(Audio_RST_GPIO_Port, Audio_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(2u);
  HAL_GPIO_WritePin(Audio_RST_GPIO_Port, Audio_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(5u);

  if (HAL_I2C_IsDeviceReady(&hi2c1, CS43L22_ADDRESS, 3u, 100u) != HAL_OK) {
    codec_ready = false;
    return false;
  }

  const bool ok =
      write_register(CS43L22_POWER_CTL1, 0x01u) &&
      write_register(CS43L22_POWER_CTL2, 0xafu) &&
      write_register(CS43L22_CLOCKING_CTL, 0x81u) &&
      write_register(CS43L22_INTERFACE_CTL1, 0x04u) &&
      set_master_muted(true) &&
      codec_cs43l22_set_volume(volume) &&
      write_register(CS43L22_ANALOG_ZC_SR_SETT, 0x00u) &&
      write_register(CS43L22_MISC_CTL, 0x04u) &&
      write_register(CS43L22_LIMIT_CTL1, 0x00u) &&
      write_register(CS43L22_TONE_CTL, 0x0fu) &&
      write_register(CS43L22_PCMA_VOL, 0x0au) &&
      write_register(CS43L22_PCMB_VOL, 0x0au);
  codec_ready = ok;
  return ok;
}

bool codec_cs43l22_start(void)
{
  if (!codec_ready) return false;
  return write_register(CS43L22_MISC_CTL, 0x06u) &&
         set_muted(false) &&
         write_register(CS43L22_POWER_CTL1, 0x9eu) &&
         set_master_muted(false);
}

bool codec_cs43l22_pause(void)
{
  if (!codec_ready) return false;
  const bool muted = set_master_muted(true);

  /* A digital mute alone was not sufficient on this board when the I2S master
   * continued generating clocks with DMA paused. Holding RESET low disables
   * the codec's DAC and headphone amplifier, guaranteeing a quiet pause. */
  HAL_Delay(2u);
  HAL_GPIO_WritePin(Audio_RST_GPIO_Port, Audio_RST_Pin, GPIO_PIN_RESET);
  codec_ready = false;
  return muted;
}

void codec_cs43l22_stop(void)
{
  if (codec_ready) {
    (void)set_master_muted(true);
    (void)set_muted(true);
    (void)write_register(CS43L22_MISC_CTL, 0x04u);
    (void)write_register(CS43L22_POWER_CTL1, 0x9fu);
  }
  HAL_GPIO_WritePin(Audio_RST_GPIO_Port, Audio_RST_Pin, GPIO_PIN_RESET);
  codec_ready = false;
}
