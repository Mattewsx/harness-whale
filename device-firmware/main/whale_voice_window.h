#pragma once
#include "whale_wire.h"
#define WHALE_VOICE_WINDOW 100
#define WHALE_VOICE_PACKET 62
typedef struct {
    uint32_t produced, acked, next_tx;
    uint32_t sent_at[WHALE_VOICE_WINDOW];
    uint8_t packets[WHALE_VOICE_WINDOW][WHALE_VOICE_PACKET];
} whale_voice_window_t;
bool whale_voice_push(whale_voice_window_t *w, uint32_t stream, const uint8_t opus[50], bool tail);
bool whale_voice_confirm(whale_voice_window_t *w, uint32_t next);
const uint8_t *whale_voice_next(whale_voice_window_t *w, uint32_t now, uint32_t *seq);
void whale_voice_sent(whale_voice_window_t *w, uint32_t seq, uint32_t now);
