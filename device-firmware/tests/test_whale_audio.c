#include "../main/whale_audio.c"
#include <stdio.h>

static int fail_at, writes, sleeps, volume, samples, nonzero;
static int16_t captured[4000];
esp_err_t bsp_audio_init(void) { return fail_at == 1 ? ESP_FAIL : ESP_OK; }
esp_err_t bsp_audio_wake(void) { return fail_at == 2 ? ESP_FAIL : ESP_OK; }
esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t channels) {
    assert(hz == 16000 && bits == 16 && channels == 1);
    return fail_at == 3 ? ESP_FAIL : ESP_OK;
}
void bsp_audio_set_volume(uint8_t percent) { volume = percent; }
esp_err_t bsp_audio_sleep(void) {
    sleeps++;
    return fail_at == 5 ? ESP_FAIL : ESP_OK;
}
esp_err_t bsp_audio_write(const void *data, size_t size) {
    assert(size == 320);
    writes++;
    if (fail_at == 4)
        return ESP_FAIL;
    const int16_t *pcm = data;
    for (unsigned i = 0; i < size / 2; i++) {
        assert(samples < 4000 && pcm[i] >= -6000 && pcm[i] <= 6000);
        captured[samples++] = pcm[i];
        nonzero += pcm[i] != 0;
    }
    return ESP_OK;
}
static void reset(int failure) {
    fail_at = failure;
    writes = sleeps = volume = samples = nonzero = 0;
    memset(captured, 0, sizeof captured);
}
int main(void) {
    reset(0);
    assert(whale_audio_done());
    assert(writes == 25 && samples == 4000 && nonzero > 1600 && sleeps == 1 && volume == 85);
    assert(captured[0] == 0 && captured[2399] == 0);
    for (int i = 2400; i < 4000; i++)
        assert(captured[i] == 0); /* enough silence to drain the DMA tail */
    for (int failure = 1; failure <= 5; failure++) {
        reset(failure);
        assert(!whale_audio_done());
        assert(sleeps == (failure == 1 ? 0 : 1));
        if (failure <= 3)
            assert(writes == 0);
        if (failure == 4)
            assert(writes == 1);
        reset(0);
        assert(whale_audio_done()); /* a later notification retries cleanly */
    }
    puts("Completion PCM envelope, DMA tail and audio fault recovery PASS");
    return 0;
}
