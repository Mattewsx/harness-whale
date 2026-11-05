#include "whale_voice_window.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    whale_voice_window_t w = {0};
    uint8_t opus[50] = {0};
    uint32_t seq;
    for (unsigned i = 0; i < 100; i++)
        assert(whale_voice_push(&w, 7, opus, false));
    assert(!whale_voice_push(&w, 7, opus, false));
    assert(w32(whale_voice_next(&w, 10, &seq)) == 7 && seq == 0);
    whale_voice_sent(&w, 0, 10);
    assert(whale_voice_next(&w, 209, &seq) && seq == 1);
    assert(whale_voice_next(&w, 210, &seq) && seq == 0);
    assert(!whale_voice_confirm(&w, 101));
    assert(whale_voice_confirm(&w, 1));
    assert(!whale_voice_confirm(&w, 0));
    assert(whale_voice_push(&w, 7, opus, true));
    for (unsigned i = 1; i < 101; i++) {
        assert(whale_voice_next(&w, 20, &seq) && seq == i);
        whale_voice_sent(&w, i, 20);
        assert(whale_voice_confirm(&w, i + 1));
    }
    assert(!whale_voice_next(&w, 1000, &seq));
    puts("voice bounded window: PASS");
    return 0;
}
