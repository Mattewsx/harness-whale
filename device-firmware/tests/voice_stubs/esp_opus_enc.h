#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int esp_audio_err_t;
#define ESP_AUDIO_ERR_OK 0
#define ESP_OPUS_ENC_FRAME_DURATION_20_MS 3
#define ESP_OPUS_ENC_APPLICATION_VOIP 0
typedef struct {
  int sample_rate, channel, bits_per_sample, bitrate, frame_duration,
      application_mode, complexity;
  bool enable_fec, enable_dtx, enable_vbr;
} esp_opus_enc_config_t;
#define ESP_OPUS_ENC_CONFIG_DEFAULT() {.bits_per_sample = 16}
typedef struct {
  uint32_t sample_rate;
  uint8_t channel, bits_per_sample;
  uint32_t bitrate;
  const uint8_t *codec_spec_info;
  uint32_t spec_info_len;
} esp_audio_enc_info_t;
typedef struct {
  uint8_t *buffer;
  uint32_t len;
} esp_audio_enc_in_frame_t;
typedef struct {
  uint8_t *buffer;
  uint32_t len, encoded_bytes;
  uint64_t pts;
} esp_audio_enc_out_frame_t;
esp_audio_err_t esp_opus_enc_open(void *, uint32_t, void **);
esp_audio_err_t esp_opus_enc_get_frame_size(void *, int *, int *);
esp_audio_err_t esp_opus_enc_get_info(void *, esp_audio_enc_info_t *);
esp_audio_err_t esp_opus_enc_process(void *, esp_audio_enc_in_frame_t *,
                                     esp_audio_enc_out_frame_t *);
void esp_opus_enc_close(void *);
