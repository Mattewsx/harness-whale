#pragma once
/* BLE OTA state machine. Transport-, flash- and RTOS-independent so it is
 * host tested (tests/test_whale_ota_core.c); whale_ota.c supplies the ESP-IDF
 * operations. Wire format: docs/OTA.md. All integers little endian.
 *
 * Host -> device (OTA characteristic 13b50204-..., write or write without response)
 *   0x01 BEGIN    u8 format(1), u32 size, sha256[32], asset_id[8], u8 vlen, version[vlen]
 *   0x02 DATA     u32 offset, payload[1..WHALE_OTA_MAX_CHUNK]
 *   0x03 END
 *   0x04 ABORT
 *   0x05 INFO
 *   0x06 ROLLBACK
 * Device -> host (notify on the same characteristic)
 *   0x81 STATUS   u8 state, u8 code, u32 offset
 *   0x85 INFO     u8 format(1), u8 flags, u32 max_size, asset_id[8],
 *                 u8 vlen, running version, u8 olen, other-slot version
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WHALE_OTA_MAX_CHUNK 236u /* 5-byte DATA header + 236 fits MTU 247 */
/* The device acknowledges every 2 KiB; hosts keep at most 4 KiB in flight so
 * the receive queue (WHALE_OTA_QUEUE items) never overflows in normal use. */
#define WHALE_OTA_ACK_BYTES 2048u
#define WHALE_OTA_WINDOW_BYTES 4096u
#define WHALE_OTA_CONFIRM_MS 60000u
#define WHALE_OTA_IDLE_MS 15000u
#define WHALE_OTA_VERSION_MAX 31u

enum whale_ota_op {
    WOTA_BEGIN = 1,
    WOTA_DATA = 2,
    WOTA_END = 3,
    WOTA_ABORT = 4,
    WOTA_INFO = 5,
    WOTA_ROLLBACK = 6,
    WOTA_STATUS = 0x81,
    WOTA_INFO_REPLY = 0x85,
};
enum whale_ota_state {
    WOTA_IDLE = 0,
    WOTA_CONFIRM = 1,
    WOTA_RECEIVING = 2,
    WOTA_VERIFYING = 3,
    WOTA_DONE = 4,
    WOTA_FAILED = 5,
    WOTA_CANCELLED = 6,
    WOTA_ROLLBACK_CONFIRM = 7,
};
enum whale_ota_code {
    WOTA_OK = 0,
    WOTA_E_BUSY = 1,
    WOTA_E_PACKET = 2,
    WOTA_E_SIZE = 3,
    WOTA_E_DECLINED = 4,
    WOTA_E_FLASH = 5,
    WOTA_E_HASH = 6,
    WOTA_E_IMAGE = 7,
    WOTA_E_OFFSET = 8,
    WOTA_E_ASSETS = 9,
    WOTA_E_NO_ROLLBACK = 10,
    WOTA_E_TIMEOUT = 11,
    WOTA_E_LINK = 12,
};
/* flags in INFO */
#define WOTA_INFO_PENDING 1u   /* running image still awaits its self-test */
#define WOTA_INFO_ROLLBACK 2u  /* the other slot holds a bootable image */
#define WOTA_INFO_ASSETS 4u    /* asset pack matches the running image */

typedef struct {
    void *ctx;
    uint32_t max_size;
    const uint8_t *asset_id; /* 8 bytes: first bytes of the pack SHA-256 */
    bool (*begin)(void *ctx, uint32_t size);
    bool (*write)(void *ctx, const uint8_t *p, size_t n);
    /* Finalizes the slot, returns WOTA_OK or an error code and the SHA-256 of
     * the written bytes. `version` is what the user confirmed. */
    int (*finish)(void *ctx, const char *version, uint8_t digest[32]);
    bool (*activate)(void *ctx);
    void (*abort)(void *ctx);
    bool (*rollback_possible)(void *ctx);
    /* Makes the other slot the boot image. */
    bool (*rollback)(void *ctx);
    /* Writes the INFO reply body after its first byte; returns its length. */
    size_t (*info)(void *ctx, uint8_t *out, size_t cap);
    void (*notify)(void *ctx, const uint8_t *p, size_t n);
    /* Tells the UI what changed. percent is 0..100. */
    void (*event)(void *ctx, uint8_t state, uint8_t code, uint8_t percent);
} whale_ota_ops_t;

typedef struct {
    uint8_t state, code, percent;
    bool begun, nacked;
    uint32_t size, offset, acked, at;
    uint8_t sha256[32];
    char version[WHALE_OTA_VERSION_MAX + 1];
} whale_ota_t;

void whale_ota_packet(whale_ota_t *o, const whale_ota_ops_t *ops, const uint8_t *p, size_t n,
                      uint32_t now);
/* The user's answer on the confirmation page. Returns true when the device
 * must restart (state WOTA_DONE). */
bool whale_ota_answer(whale_ota_t *o, const whale_ota_ops_t *ops, bool yes, uint32_t now);
void whale_ota_tick(whale_ota_t *o, const whale_ota_ops_t *ops, uint32_t now);
/* The BLE link dropped: any transfer in flight is abandoned. */
void whale_ota_link_down(whale_ota_t *o, const whale_ota_ops_t *ops);
bool whale_ota_valid_version(const uint8_t *p, size_t n);
