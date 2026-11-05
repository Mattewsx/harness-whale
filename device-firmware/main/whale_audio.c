#include "whale_audio.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include <stdint.h>
#include <string.h>

bool whale_audio_done(void) {
    /* 1 kHz, 150 ms, with 5 ms attack/release; no heap-sized sound asset. */
    static const int16_t wave[16] = {0,     2296,  4243,  5543,  6000,  5543,  4243,  2296,
                                     0,     -2296, -4243, -5543, -6000, -5543, -4243, -2296};
    int16_t pcm[160];
    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGW("whale_audio", "Completion audio init failed: %d", (int)err);
        return false;
    }
    err = bsp_audio_wake();
    if (err == ESP_OK)
        err = bsp_audio_set_format(16000, 16, 1);
    if (err == ESP_OK) {
        bsp_audio_set_volume(85);
        for (unsigned start = 0; start < 2400 && err == ESP_OK; start += 160) {
            for (unsigned i = 0; i < 160; i++) {
                unsigned pos = start + i;
                unsigned gain = pos < 80 ? pos : pos >= 2320 ? 2399 - pos : 80;
                pcm[i] = (int32_t)wave[pos % 16] * (int32_t)gain / 80;
            }
            err = bsp_audio_write(pcm, sizeof pcm);
        }
        /* Six 240-frame DMA buffers hold 90 ms at 16 kHz. Enqueue 100 ms
         * of silence so the final tone samples have drained before suspend. */
        memset(pcm, 0, sizeof pcm);
        for (unsigned i = 0; i < 10 && err == ESP_OK; i++)
            err = bsp_audio_write(pcm, sizeof pcm);
    }
    esp_err_t sleep = bsp_audio_sleep();
    if (err != ESP_OK || sleep != ESP_OK) {
        ESP_LOGW("whale_audio", "Completion audio failed: play=%d sleep=%d", (int)err,
                 (int)sleep);
        return false;
    }
    ESP_LOGI("whale_audio", "Completion cue played");
    return true;
}
