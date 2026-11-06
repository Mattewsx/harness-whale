#pragma once
/* BLE OTA on the ESP32-C3: queue, worker task and ESP-IDF slot operations
 * around whale_ota_core. See docs/OTA.md. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WHALE_OTA_EVENT(state, code, percent)                                                      \
    ((uint32_t)(state) | ((uint32_t)(code) << 8) | ((uint32_t)(percent) << 16))

/* Reads this image's OTA state; call before whale_assets_init(). */
void whale_ota_boot(void);
/* Boot self-test result for the asset pack. A pending image whose assets do
 * not match rolls back at once; a valid image keeps running text-only. */
void whale_ota_assets_checked(bool assets_ok);
/* An authenticated BLE link proves the new image works: mark it valid. */
void whale_ota_healthy(void);
bool whale_ota_pending(void);
/* BLE host task: a write to the OTA characteristic. Never blocks. */
void whale_ota_rx(const uint8_t *p, size_t n);
void whale_ota_link_lost(void);
/* App task: the user's answer on the confirmation page. */
void whale_ota_reply(bool yes);
/* App task: the user cancelled a running transfer. */
void whale_ota_cancel(void);
/* Text for the confirmation page, valid while a request is open. */
void whale_ota_offer(char *version, size_t cap, uint32_t *size);
