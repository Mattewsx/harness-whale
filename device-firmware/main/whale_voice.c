#include "whale_voice.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_opus_enc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "whale_ble.h"
#include "whale_voice_window.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static atomic_uint pending, running, stop, acknowledged, duration;
static uint8_t identity[24];
static uint32_t now_ms(void) { return esp_timer_get_time() / 1000; }
bool whale_voice_busy(void) {
  return atomic_load(&pending) || atomic_load(&running);
}
bool whale_voice_pending(void) { return atomic_load(&pending); }
uint32_t whale_voice_milliseconds(void) { return atomic_load(&duration); }
bool whale_voice_begin(const uint8_t id[24]) {
  if (whale_voice_busy() || !whale_ble_audio_ready())
    return false;
  memcpy(identity, id, 24);
  atomic_store(&stop, 0);
  atomic_store(&acknowledged, 0);
  atomic_store(&duration, 0);
  atomic_store(&pending, 1);
  return true;
}
void whale_voice_stop(bool discard) {
  unsigned wanted = discard ? 2 : 1;
  if (atomic_load(&stop) < wanted)
    atomic_store(&stop, wanted);
}
void whale_voice_ack(const uint8_t id[24], uint32_t next) {
  if (!memcmp(identity, id, 24) && next <= 3001 &&
      next > atomic_load(&acknowledged))
    atomic_store(&acknowledged, next);
}
static void event(unsigned value) {
  whale_event_t e = {.kind = WE_VOICE, .len = 16, .value = value};
  memcpy(e.bytes, identity + 8, 16);
  whale_post(&e);
}
static bool drain(whale_voice_window_t *w) {
  if (!whale_ble_audio_ready() ||
      !whale_voice_confirm(w, atomic_load(&acknowledged)))
    return false;
  for (unsigned i = 0; i < 4; i++) {
    uint32_t seq;
    const uint8_t *p = whale_voice_next(w, now_ms(), &seq);
    if (!p || !whale_ble_audio_send(p, WHALE_VOICE_PACKET))
      break;
    whale_voice_sent(w, seq, now_ms());
  }
  return true;
}
void whale_voice_run(void) {
  if (!atomic_load(&pending))
    return;
  atomic_store(&running, 1);
  atomic_store(&pending, 0);
  void *encoder = NULL;
  whale_voice_window_t *window = calloc(1, sizeof(*window));
  uint8_t *encoded = NULL;
  unsigned outcome = 3;
  const char *stage = "window_alloc";
  int error = 0;
  bool audio_started = false;
  uint32_t samples = 0, max_encode_us = 0;
  esp_opus_enc_config_t config = ESP_OPUS_ENC_CONFIG_DEFAULT();
  config.sample_rate = 16000;
  config.channel = 1;
  config.bitrate = 20000;
  config.complexity = 0;
  config.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_20_MS;
  config.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP;
  config.enable_fec = config.enable_dtx = config.enable_vbr = false;
  if (atomic_load(&stop) == 2)
    goto done;
  if (!window)
    goto done;
#define CHECK_AT(label, call)                                                  \
  do {                                                                         \
    stage = label;                                                             \
    error = (call);                                                            \
    if (error != 0)                                                            \
      goto done;                                                               \
  } while (0)
  CHECK_AT("encoder_open",
           esp_opus_enc_open(&config, sizeof(config), &encoder));
  int input_size = 0, output_size = 0;
  esp_audio_enc_info_t info = {0};
  CHECK_AT("encoder_frame_size",
           esp_opus_enc_get_frame_size(encoder, &input_size, &output_size));
  CHECK_AT("encoder_info", esp_opus_enc_get_info(encoder, &info));
  stage = "encoder_contract";
  if (input_size != 640 || output_size < 50 || output_size > 4096 ||
      info.sample_rate != 16000 || info.channel != 1 ||
      info.bits_per_sample != 16 || info.bitrate != 20000)
    goto done;
  stage = "output_alloc";
  encoded = malloc(output_size);
  if (!encoded)
    goto done;
  CHECK_AT("audio_init", bsp_audio_init());
  audio_started = true;
  CHECK_AT("audio_wake", bsp_audio_wake());
  CHECK_AT("audio_format", bsp_audio_set_format(16000, 16, 1));
  /* esp_audio_codec 2.6.2 returns raw Opus, with no codec_spec_info.
   * RFC 7845 mapping-family 0 header. The pinned C3 encoder's VOIP
   * lookahead is Fs/400 + Fs/250 = 104 samples at 16 kHz, or 312 at
   * the Ogg 48 kHz clock. Revalidate this when upgrading the codec;
   * do not read private encoder handles to obtain the delay.
   * One zero tail frame below flushes that lookahead without clipping speech.
   */
  static const uint8_t opus_head[19] = {'O', 'p', 'u', 's',  'H',  'e',  'a',
                                        'd', 1,   1,   0x38, 0x01, 0x80, 0x3e,
                                        0,   0,   0,   0,    0};
  uint8_t header[43];
  memcpy(header, identity, 24);
  memcpy(header + 24, opus_head, sizeof opus_head);
  stage = "header_send";
  if (!whale_send(0x42, header, sizeof header)) {
    outcome = 4;
    goto done;
  }
  event(1);
  ESP_LOGI("whale_voice", "capture ready heap_free=%lu stack_min_bytes=%lu",
           (unsigned long)esp_get_free_heap_size(),
           (unsigned long)uxTaskGetStackHighWaterMark(NULL));
  uint32_t started = now_ms();
  for (;;) {
    int16_t pcm[320] = {0};
    bool tail = atomic_load(&stop) || samples >= 960000;
    if (atomic_load(&stop) == 2) {
      outcome = 5;
      break;
    }
    if (!tail) {
      CHECK_AT("audio_read", bsp_audio_read(pcm, sizeof pcm));
    }
    if (!tail)
      samples += 320;
    esp_audio_enc_in_frame_t in = {.buffer = (uint8_t *)pcm, .len = sizeof pcm};
    esp_audio_enc_out_frame_t out = {.buffer = encoded, .len = output_size};
    int64_t before = esp_timer_get_time();
    esp_audio_err_t err = esp_opus_enc_process(encoder, &in, &out);
    stage = "encode";
    error = err;
    uint32_t took = esp_timer_get_time() - before;
    if (took > max_encode_us)
      max_encode_us = took;
    if (err != ESP_AUDIO_ERR_OK || out.encoded_bytes != 50)
      break;
    /* Inspect the first encoded frame, then once per second. This is a low
     * margin abort for subsequent frames, not a substitute for a sized stack
     * or the hardware stack protector. Avoid scanning the stack every 20 ms. */
    if (window->produced % 50 == 0 || tail) {
      unsigned remaining = uxTaskGetStackHighWaterMark(NULL);
      ESP_LOGI("whale_voice", "capture progress duration_ms=%lu max_encode_us=%lu "
               "heap_free=%lu stack_min_bytes=%u",
               (unsigned long)(samples / 16), (unsigned long)max_encode_us,
               (unsigned long)esp_get_free_heap_size(), remaining);
      if (remaining < WHALE_VOICE_STACK_MARGIN_BYTES) {
        stage = "stack_margin";
        break;
      }
    }
    stage = "upload";
    if (!whale_voice_push(window, w32(identity + 8), encoded, tail) ||
        !drain(window) || (!tail && now_ms() - started > samples / 16 + 180)) {
      outcome = 4;
      break;
    }
    atomic_store(&duration, samples / 16);
    if (tail) {
      stage = "finish_ack";
      uint8_t end[32];
      memcpy(end, identity, 24);
      put32(end + 24, window->produced);
      put32(end + 28, samples);
      uint32_t until = now_ms() + 2000, resend = 0;
      while ((int32_t)(until - now_ms()) > 0 && atomic_load(&stop) != 2) {
        if (!drain(window))
          break;
        if (now_ms() - resend >= 200) {
          whale_send(0x43, end, sizeof end);
          resend = now_ms();
        }
        if (window->acked == window->produced) {
          outcome = 2;
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
      }
      if (outcome != 2)
        outcome = 4;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
done:
  if (encoder)
    esp_opus_enc_close(encoder);
  free(encoded);
  free(window);
  if (audio_started)
    bsp_audio_sleep();
  if (atomic_load(&stop) == 2)
    outcome = 5;
  ESP_LOGI("whale_voice",
           "capture result=%u stage=%s error=%d duration_ms=%lu "
           "max_encode_us=%lu min_heap=%lu "
           "stack_min_bytes=%lu",
           outcome, stage, error, (unsigned long)(samples / 16),
           (unsigned long)max_encode_us,
           (unsigned long)esp_get_minimum_free_heap_size(),
           (unsigned long)uxTaskGetStackHighWaterMark(NULL));
  event(outcome);
  atomic_store(&running, 0);
#undef CHECK_AT
}
