#include "mock_hw.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#undef ESP_LOGI
static void capture_log(const char *tag, const char *fmt, ...);
#define ESP_LOGI capture_log
#include "../main/whale_voice.c"

static unsigned fail_at, reads, encoded_count, closed, slept, started_count,
    result, packets;
static uint64_t clock_us;
static uint8_t sent_head[43];
static char log_line[512];
static bool connected = true, cancel_during_read;
static const uint8_t test_id[24] = {1, 2, 3, 4, 0, 0, 0, 0, 5, 6, 7, 8};
static void capture_log(const char *tag, const char *fmt, ...) {
  (void)tag;
  va_list args;
  va_start(args, fmt);
  vsnprintf(log_line, sizeof log_line, fmt, args);
  va_end(args);
}
uint32_t esp_get_minimum_free_heap_size(void) { return 24000; }
uint32_t esp_get_free_heap_size(void) { return 24000; }
unsigned uxTaskGetStackHighWaterMark(void *task) {
  (void)task;
  return fail_at == 11 && encoded_count ? 1024 : 10000;
}
int64_t esp_timer_get_time(void) { return clock_us; }
void vTaskDelay(int ticks) { clock_us += ticks * 1000; }
bool whale_ble_audio_ready(void) { return connected; }
bool whale_post(const whale_event_t *e) {
  assert(e->kind == WE_VOICE && !memcmp(e->bytes, test_id + 8, 16));
  if (e->value == 1)
    started_count++;
  else
    result = e->value;
  return true;
}
bool whale_send(uint8_t type, const uint8_t *p, size_t n) {
  assert(!memcmp(p, test_id, 24));
  if (type == 0x42) {
    assert(n == 43);
    memcpy(sent_head, p, n);
    return fail_at != 8;
  }
  assert(type == 0x43 && n == 32);
  assert(w32(p + 24) == 4 && w32(p + 28) == 960);
  return true;
}
bool whale_ble_audio_send(const uint8_t *p, size_t n) {
  assert(n == 62 && w32(p) == w32(test_id + 8));
  assert(w32(p + 4) == packets++);
  whale_voice_ack(test_id, packets);
  return true;
}
esp_audio_err_t esp_opus_enc_open(void *cfg, uint32_t size, void **encoder) {
  esp_opus_enc_config_t *c = cfg;
  assert(size == sizeof(*c) && c->sample_rate == 16000 && c->channel == 1);
  assert(c->bits_per_sample == 16 && c->bitrate == 20000 && c->complexity == 0);
  assert(c->frame_duration == 3 && c->application_mode == 0);
  assert(!c->enable_dtx && !c->enable_vbr && !c->enable_fec);
  if (fail_at == 1)
    return -1;
  *encoder = (void *)1;
  return 0;
}
esp_audio_err_t esp_opus_enc_get_frame_size(void *e, int *in, int *out) {
  assert(e);
  *in = 640;
  *out = 1276;
  return fail_at == 2 ? -2 : 0;
}
esp_audio_err_t esp_opus_enc_get_info(void *e, esp_audio_enc_info_t *info) {
  assert(e);
  info->sample_rate = 16000;
  info->channel = 1;
  info->bits_per_sample = 16;
  info->bitrate = fail_at == 4 ? 24000 : 20000;
  /* Real 2.6.2 behavior: success leaves both optional fields empty. */
  assert(!info->codec_spec_info && !info->spec_info_len);
  return fail_at == 3 ? -3 : 0;
}
esp_audio_err_t esp_opus_enc_process(void *e, esp_audio_enc_in_frame_t *in,
                                     esp_audio_enc_out_frame_t *out) {
  assert(e && in->len == 640 && out->len == 1276);
  clock_us += 1000;
  if (fail_at == 10)
    return -10;
  if (encoded_count == 3) {
    for (unsigned i = 0; i < in->len; i++)
      assert(in->buffer[i] == 0);
  }
  encoded_count++;
  memset(out->buffer, 0x42, 50);
  out->encoded_bytes = 50;
  return 0;
}
void esp_opus_enc_close(void *e) {
  assert(e);
  closed++;
}
esp_err_t bsp_audio_init(void) { return fail_at == 5 ? -5 : 0; }
esp_err_t bsp_audio_wake(void) { return fail_at == 6 ? -6 : 0; }
esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t ch) {
  assert(hz == 16000 && bits == 16 && ch == 1);
  return fail_at == 7 ? -7 : 0;
}
esp_err_t bsp_audio_sleep(void) {
  slept++;
  return 0;
}
esp_err_t bsp_audio_read(void *p, size_t n) {
  assert(n == 640);
  reads++;
  clock_us += 20000;
  memset(p, 0x12, n);
  if (cancel_during_read)
    whale_voice_stop(true);
  else if (reads == 3)
    whale_voice_stop(false);
  return fail_at == 9 ? -9 : 0;
}
static void reset(unsigned failure) {
  assert(!whale_voice_busy());
  fail_at = failure;
  reads = encoded_count = closed = slept = started_count = result = packets = 0;
  clock_us = 1000000;
  connected = true;
  cancel_during_read = false;
  memset(sent_head, 0, sizeof sent_head);
  log_line[0] = 0;
}
static void success(void) {
  reset(0);
  assert(whale_voice_begin(test_id));
  assert(!whale_voice_begin(test_id));
  whale_voice_run();
  assert(!whale_voice_busy() && result == 2 && started_count == 1);
  assert(reads == 3 && encoded_count == 4 && packets == 4 && closed == 1 &&
         slept == 1);
  assert(whale_voice_milliseconds() == 60);
  assert(!memcmp(sent_head + 24, "OpusHead\1\1", 10));
  assert(w16(sent_head + 34) == 312 && w32(sent_head + 36) == 16000);
  assert(!sent_head[40] && !sent_head[41] && !sent_head[42]);
}
int main(void) {
  success();
  const char *stages[] = {"",
                          "encoder_open",
                          "encoder_frame_size",
                          "encoder_info",
                          "encoder_contract",
                          "audio_init",
                          "audio_wake",
                          "audio_format",
                          "header_send",
                          "audio_read",
                          "encode", "stack_margin"};
  for (unsigned i = 1; i <= 11; i++) {
    reset(i);
    assert(whale_voice_begin(test_id));
    whale_voice_run();
    assert(!whale_voice_busy() && result == (i == 8 ? 4u : 3u));
    assert(strstr(log_line, stages[i]));
    assert(closed == (i == 1 ? 0u : 1u));
    assert(slept == (i <= 5 ? 0u : 1u));
    if (i < 9)
      assert(!reads);
    if (i == 11) assert(packets == 0 && encoded_count == 1);
    success(); /* clean retry after every failure */
  }
  reset(0);
  assert(whale_voice_begin(test_id));
  whale_voice_stop(true);
  whale_voice_run();
  assert(result == 5 && !reads && !closed && !slept && !started_count);
  reset(0);
  cancel_during_read = true;
  assert(whale_voice_begin(test_id));
  whale_voice_run();
  assert(result == 5 && closed == 1 && slept == 1);
  reset(0);
  connected = false;
  assert(!whale_voice_begin(test_id));
  success();
  puts("Voice worker empty codec metadata, Opus header/tail, stage faults and "
       "retry PASS");
}
