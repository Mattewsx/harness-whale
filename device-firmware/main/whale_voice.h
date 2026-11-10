#pragma once
#include <stdbool.h>
#include <stdint.h>

/* IDF task stack sizes and high-water marks are bytes. The pinned C3 SILK
 * path exceeds 19 KiB (R11.2 matched panic + NSQ allocations). Reserve 24 KiB
 * for this worker only; leave BLE, display and reply-cache budgets unchanged. */
#define WHALE_PERIPHERAL_STACK_BYTES (24 * 1024)
#define WHALE_VOICE_STACK_MARGIN_BYTES 2048

/* Worker API: only the peripheral task owns codec/I2S and calls run(). */
bool whale_voice_begin(const uint8_t identity[24]);
void whale_voice_stop(bool discard);
void whale_voice_ack(const uint8_t identity[24], uint32_t next);
bool whale_voice_busy(void);
bool whale_voice_pending(void);
void whale_voice_run(void);
uint32_t whale_voice_milliseconds(void);

enum {
    VOICE_INTRO = 0,
    VOICE_PREPARE,
    VOICE_RECORD,
    VOICE_FINISH,
    VOICE_PREVIEW,
    VOICE_ACTIONS,
    VOICE_SENDING,
    VOICE_SENT,
    VOICE_QUEUED,
    VOICE_UNKNOWN,
    VOICE_FAILED,
    VOICE_UNSUPPORTED,
    VOICE_ASR_ERROR,
    VOICE_ARCHIVED,
    VOICE_QUESTION,
    VOICE_EXPIRED,
    VOICE_EMPTY,
    VOICE_BROKEN,
    VOICE_DISCONNECTED,
    VOICE_MIC_ERROR,
    VOICE_NO_TARGET /* new task only: no folder on the computer to open it in */
};
