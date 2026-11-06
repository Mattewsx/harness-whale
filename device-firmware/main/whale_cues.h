#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
/* Dispatcher-owned pending cues. IDs are monotonic within the link baseline. */
typedef struct {
    uint32_t session, token;
    uint8_t kind;
} whale_cue_t;
typedef struct {
    whale_cue_t pending[16];
    uint32_t seq, since;
    bool batching;
} whale_cues_t;
static inline void whale_cues_update(whale_cues_t *q, uint32_t seq, uint32_t sid, uint32_t token,
                                     uint8_t valid, uint8_t trigger, uint32_t now) {
    if (seq <= q->seq)
        return;
    q->seq = seq;
    int free_slot = -1, worst = -1;
    for (int i = 0; i < 16; i++) {
        whale_cue_t *p = &q->pending[i];
        if (p->session == sid && (p->token != token || p->kind != valid))
            p->kind = 0;
        if (p->session == sid && p->token == token && p->kind == trigger && trigger)
            return;
        if (!p->kind)
            free_slot = i;
        else if (worst < 0 || p->kind > q->pending[worst].kind)
            worst = i;
    }
    if (!trigger || trigger > 4 || valid != trigger)
        return;
    if (free_slot < 0 && worst >= 0 && trigger < q->pending[worst].kind)
        free_slot = worst;
    if (free_slot < 0)
        return;
    q->pending[free_slot] = (whale_cue_t){sid, token, trigger};
    if (!q->batching) {
        q->batching = true;
        q->since = now;
    }
}
static inline uint8_t whale_cues_take(whale_cues_t *q, uint32_t now, bool recording) {
    if (!q->batching || recording || now - q->since < 1000)
        return 0;
    uint8_t best = 0;
    for (unsigned i = 0; i < 16; i++) {
        uint8_t k = q->pending[i].kind;
        if (k && (!best || k < best))
            best = k;
    }
    memset(q->pending, 0, sizeof q->pending);
    q->batching = false;
    return best;
}
static inline void whale_cues_read(whale_cues_t *q, uint32_t sid, uint8_t kind) {
    for (unsigned i = 0; i < 16; i++)
        if (q->pending[i].session == sid && q->pending[i].kind == kind)
            q->pending[i].kind = 0;
}
