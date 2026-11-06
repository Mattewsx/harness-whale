#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint8_t kind, mode;
    uint32_t generation;
} whale_audio_cmd_t;
/* Interrupt is non-blocking. Only the existing peripheral worker plays PCM. */
void whale_audio_interrupt(void);
bool whale_audio_busy(void);
uint32_t whale_audio_generation(void);
bool whale_audio_play(whale_audio_cmd_t cue);
bool whale_audio_done(void);
