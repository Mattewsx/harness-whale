#include "whale_audio.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "whale_assets.h"
#include <stdatomic.h>
#include <string.h>
static atomic_uint generation;
static atomic_bool playing;
bool whale_audio_busy(void) { return atomic_load(&playing); }
void whale_audio_interrupt(void) { atomic_fetch_add(&generation, 1); }
uint32_t whale_audio_generation(void) { return atomic_load(&generation); }
bool whale_audio_play(whale_audio_cmd_t cue) {
    if (!whale_pcm || cue.kind < 1 || cue.kind > 4 || cue.mode > 1 ||
        cue.generation != whale_audio_generation())
        return false;
    const whale_pcm_asset_t *asset = &whale_pcm_assets[cue.mode * 4 + cue.kind - 1];
    atomic_store(&playing, true);
    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        atomic_store(&playing, false);
        return false;
    }
    err = bsp_audio_wake();
    if (err == ESP_OK)
        err = bsp_audio_set_format(16000, 16, 1);
    int16_t pcm[160];
    if (err == ESP_OK) {
        /* Voice assets were level-calibrated separately; verify acoustics on
         * device. */
        bsp_audio_set_volume(cue.mode ? 80 : 85);
        for (uint32_t pos = 0; pos < asset->bytes && err == ESP_OK; pos += sizeof pcm) {
            if (cue.generation != whale_audio_generation())
                break;
            unsigned n = asset->bytes - pos;
            if (n > sizeof pcm)
                n = sizeof pcm;
            memset(pcm, 0, sizeof pcm);
            memcpy(pcm, whale_pcm + asset->offset + pos, n);
            err = bsp_audio_write(pcm, sizeof pcm);
        }
        /* Drain only completed playback. An interruption suspends immediately. */
        memset(pcm, 0, sizeof pcm);
        for (unsigned i = 0; i < 10 && err == ESP_OK && cue.generation == whale_audio_generation();
             i++)
            err = bsp_audio_write(pcm, sizeof pcm);
    }
    esp_err_t sleep = bsp_audio_sleep();
    if (err != ESP_OK || sleep != ESP_OK)
        ESP_LOGW("whale_audio", "play=%d sleep=%d", (int)err, (int)sleep);
    atomic_store(&playing, false);
    return err == ESP_OK && sleep == ESP_OK && cue.generation == whale_audio_generation();
}
bool whale_audio_done(void) {
    return whale_audio_play((whale_audio_cmd_t){3, 0, whale_audio_generation()});
}
