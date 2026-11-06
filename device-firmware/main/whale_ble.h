#pragma once
#include "whale_wire.h"
enum whale_event_kind { WE_FRAME, WE_LINK, WE_DOWN, WE_PAIR, WE_KEY, WE_VOICE, WE_OTA };
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

/* OTA characteristic 13b50204-...: notify a status/info reply (docs/OTA.md). */
bool whale_ble_ota_notify(const uint8_t *bytes, size_t length);
