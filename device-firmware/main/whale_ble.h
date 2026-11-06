#pragma once
#include "whale_wire.h"
enum whale_event_kind { WE_FRAME, WE_LINK, WE_DOWN, WE_PAIR, WE_KEY, WE_VOICE };
typedef struct {
    uint8_t kind, len, bytes[20];
    uint32_t value;
} whale_event_t;
bool whale_post(const whale_event_t *event);
void whale_ble_start(void);
bool whale_send(uint8_t type, const uint8_t *p, size_t n);
void whale_pair(bool yes);
bool whale_unbind(void);

bool whale_ble_audio_ready(void);
bool whale_ble_audio_send(const uint8_t *bytes, size_t length);
/* Long advertising interval while the computer has been away for a while. */
void whale_ble_set_slow(bool slow);
/* Stop advertising and the NimBLE host before deep sleep. Refuses (false)
 * while a central is connected or pairing; the radio is unusable after true.
 * Restarts the chip if the host cannot be stopped. */
bool whale_ble_stop_for_sleep(void);
