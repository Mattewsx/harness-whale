#include "whale_voice_window.h"
#include <string.h>
bool whale_voice_push(whale_voice_window_t *w, uint32_t stream, const uint8_t opus[50], bool tail) {
    if (w->produced - w->acked >= WHALE_VOICE_WINDOW || w->produced >= 3001)
        return false;
    uint32_t seq = w->produced++;
    uint8_t *p = w->packets[seq % WHALE_VOICE_WINDOW];
    put32(p, stream);
    put32(p + 4, seq);
    put16(p + 8, tail ? 0 : 320);
    p[10] = tail;
    p[11] = 1;
    memcpy(p + 12, opus, 50);
    w->sent_at[seq % WHALE_VOICE_WINDOW] = 0;
    return true;
}
bool whale_voice_confirm(whale_voice_window_t *w, uint32_t next) {
    if (next > w->produced || next < w->acked)
        return false;
    w->acked = next;
    if (w->next_tx < next)
        w->next_tx = next;
    return true;
}
const uint8_t *whale_voice_next(whale_voice_window_t *w, uint32_t now, uint32_t *seq) {
    if (w->acked < w->next_tx && now - w->sent_at[w->acked % WHALE_VOICE_WINDOW] >= 200)
        *seq = w->acked;
    else if (w->next_tx < w->produced)
        *seq = w->next_tx;
    else
        return NULL;
    return w->packets[*seq % WHALE_VOICE_WINDOW];
}
void whale_voice_sent(whale_voice_window_t *w, uint32_t seq, uint32_t now) {
    w->sent_at[seq % WHALE_VOICE_WINDOW] = now;
    if (seq == w->next_tx)
        w->next_tx++;
}
