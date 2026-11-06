#include "../main/whale_audio.c"
#include <stdio.h>
static uint8_t test_pcm[WHALE_PCM_BYTES];
const uint8_t *whale_pcm = test_pcm, *whale_poses, *whale_companion;
static int fail_at, writes, sleeps, volume, samples, nonzero, interrupt_at;
esp_err_t bsp_audio_init(void) { return fail_at == 1 ? ESP_FAIL : ESP_OK; }
esp_err_t bsp_audio_wake(void) { return fail_at == 2 ? ESP_FAIL : ESP_OK; }
esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t ch) {
    assert(hz == 16000 && bits == 16 && ch == 1);
    return fail_at == 3 ? ESP_FAIL : ESP_OK;
}
void bsp_audio_set_volume(uint8_t percent) { volume = percent; }
esp_err_t bsp_audio_sleep(void) {
    sleeps++;
    return fail_at == 5 ? ESP_FAIL : ESP_OK;
}
esp_err_t bsp_audio_write(const void *data, size_t size) {
    assert(size == 320);
    assert(whale_audio_busy());
    writes++;
    if (fail_at == 4)
        return ESP_FAIL;
    const int16_t *pcm = data;
    for (unsigned i = 0; i < size / 2; i++) {
        samples++;
        nonzero += pcm[i] != 0;
    }
    if (interrupt_at == writes)
        whale_audio_interrupt();
    return ESP_OK;
}
static void reset(int failure) {
    fail_at = failure;
    writes = sleeps = volume = samples = nonzero = interrupt_at = 0;
}
int main(void) {
    FILE *f = fopen("main/cues.pcm", "rb");
    assert(f && fread(test_pcm, 1, sizeof test_pcm, f) == sizeof test_pcm);
    fclose(f);
    for (unsigned mode = 0; mode < 2; mode++)
        for (unsigned kind = 1; kind <= 4; kind++) {
            reset(0);
            assert(whale_audio_play((whale_audio_cmd_t){kind, mode, whale_audio_generation()}));
            assert(writes == (int)((whale_pcm_assets[mode * 4 + kind - 1].bytes + 319) / 320 + 10));
            assert(sleeps == 1 && volume == (mode ? 80 : 85) && nonzero > 0);
        }
    for (int failure = 1; failure <= 5; failure++) {
        reset(failure);
        assert(!whale_audio_done());
        assert(sleeps == (failure == 1 ? 0 : 1));
        reset(0);
        assert(whale_audio_done());
    }
    reset(0);
    interrupt_at = 3;
    assert(!whale_audio_done());
    assert(writes == 3 && sleeps == 1);
    reset(0);
    whale_audio_cmd_t stale = {3, 1, whale_audio_generation()};
    whale_audio_interrupt();
    assert(!whale_audio_play(stale) && writes == 0 && sleeps == 0);
    assert(!whale_audio_play((whale_audio_cmd_t){0, 1, whale_audio_generation()}));
    assert(!whale_audio_busy());
    puts("Eight real PCM assets, volumes, DMA tail, interrupt, stale command and "
         "recovery PASS");
    return 0;
}
