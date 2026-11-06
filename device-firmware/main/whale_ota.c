#include "whale_ota.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "whale_asset_pack_id.h"
#include "whale_assets.h"
#include "whale_ble.h"
#include "whale_ota_core.h"
#include <stdio.h>
#include <string.h>

#define WHALE_OTA_QUEUE 20
#define WHALE_OTA_STACK 6144 /* esp_ota_end() verifies the whole image */
enum { RX_PACKET, RX_YES, RX_NO, RX_LINK };
typedef struct {
    uint8_t kind, len;
    uint8_t bytes[5 + WHALE_OTA_MAX_CHUNK];
} rx_item_t;

static const char *TAG = "whale_ota";
static QueueHandle_t rx;
static volatile bool worker_running;
static whale_ota_t core;
static const esp_partition_t *target;
static esp_ota_handle_t handle;
static bool sha_open;
static mbedtls_sha256_context sha;
static volatile bool pending;
static char offer_version[WHALE_OTA_VERSION_MAX + 1];
static uint32_t offer_size;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool op_begin(void *ctx, uint32_t size) {
    (void)ctx;
    target = esp_ota_get_next_update_partition(NULL);
    if (!target || size > target->size)
        return false;
    esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "begin failed: %s", esp_err_to_name(err));
        handle = 0;
        return false;
    }
    mbedtls_sha256_init(&sha);
    sha_open = mbedtls_sha256_starts(&sha, 0) == 0;
    ESP_LOGI(TAG, "receiving %lu bytes into %s", (unsigned long)size, target->label);
    return sha_open;
}
static bool op_write(void *ctx, const uint8_t *p, size_t n) {
    (void)ctx;
    return handle && sha_open && esp_ota_write(handle, p, n) == ESP_OK &&
           mbedtls_sha256_update(&sha, p, n) == 0;
}
static void close_sha(void) {
    if (sha_open)
        mbedtls_sha256_free(&sha);
    sha_open = false;
}
static int op_finish(void *ctx, const char *version, uint8_t digest[32]) {
    (void)ctx;
    int code = WOTA_OK;
    if (!sha_open || mbedtls_sha256_finish(&sha, digest))
        code = WOTA_E_FLASH;
    close_sha();
    esp_err_t err = esp_ota_end(handle); /* also verifies the image checksum/hash */
    handle = 0;
    if (code != WOTA_OK)
        return code;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "image rejected: %s", esp_err_to_name(err));
        return err == ESP_ERR_OTA_VALIDATE_FAILED ? WOTA_E_IMAGE : WOTA_E_FLASH;
    }
    esp_app_desc_t desc;
    const esp_app_desc_t *own = esp_app_get_description();
    if (esp_ota_get_partition_description(target, &desc) != ESP_OK ||
        strncmp(desc.project_name, own->project_name, sizeof desc.project_name) ||
        strncmp(desc.version, version, sizeof desc.version)) {
        ESP_LOGE(TAG, "image is not the confirmed %s firmware", own->project_name);
        return WOTA_E_IMAGE;
    }
    return WOTA_OK;
}
static bool op_activate(void *ctx) {
    (void)ctx;
    esp_err_t err = esp_ota_set_boot_partition(target);
    ESP_LOGI(TAG, "boot slot -> %s: %s", target ? target->label : "?", esp_err_to_name(err));
    return err == ESP_OK;
}
static void op_abort(void *ctx) {
    (void)ctx;
    close_sha();
    if (handle)
        esp_ota_abort(handle);
    handle = 0;
}
static bool op_rollback_possible(void *ctx) {
    (void)ctx;
    return esp_ota_check_rollback_is_possible();
}
static bool op_rollback(void *ctx) {
    (void)ctx;
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    return other && esp_ota_check_rollback_is_possible() &&
           esp_ota_set_boot_partition(other) == ESP_OK;
}
static size_t put_text(uint8_t *out, size_t cap, const char *s) {
    size_t n = strnlen(s, WHALE_OTA_VERSION_MAX);
    if (cap < n + 1)
        n = cap ? cap - 1 : 0;
    if (cap)
        out[0] = (uint8_t)n;
    memcpy(out + 1, s, n);
    return n + 1;
}
static size_t op_info(void *ctx, uint8_t *out, size_t cap) {
    (void)ctx;
    if (cap < 16)
        return 0;
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t other = {0};
    bool has_other = next && esp_ota_get_partition_description(next, &other) == ESP_OK;
    out[0] = 1;
    out[1] = (pending ? WOTA_INFO_PENDING : 0) |
             (has_other && esp_ota_check_rollback_is_possible() ? WOTA_INFO_ROLLBACK : 0) |
             (whale_assets_ready() ? WOTA_INFO_ASSETS : 0);
    uint32_t max = next ? next->size : 0;
    memcpy(out + 2, &max, 4); /* little endian target */
    memcpy(out + 6, whale_asset_pack_sha256, 8);
    size_t n = 14;
    n += put_text(out + n, cap - n, esp_app_get_description()->version);
    n += put_text(out + n, cap - n, has_other ? other.version : "");
    return n;
}
static void op_notify(void *ctx, const uint8_t *p, size_t n) {
    (void)ctx;
    if (!whale_ble_ota_notify(p, n))
        ESP_LOGW(TAG, "status notify dropped (op 0x%02x)", p[0]);
}
static void op_event(void *ctx, uint8_t state, uint8_t code, uint8_t percent) {
    (void)ctx;
    if (state == WOTA_CONFIRM) {
        memcpy(offer_version, core.version, sizeof offer_version);
        offer_size = core.size;
    }
    if (state != WOTA_RECEIVING || percent % 2 == 0)
        whale_post(&(whale_event_t){.kind = WE_OTA, .value = WHALE_OTA_EVENT(state, code, percent)});
    if (state == WOTA_FAILED || state == WOTA_CANCELLED)
        ESP_LOGW(TAG, "session ended state=%u code=%u at %lu", state, code,
                 (unsigned long)core.offset);
}
static whale_ota_ops_t ops = {
    .begin = op_begin,
    .write = op_write,
    .finish = op_finish,
    .activate = op_activate,
    .abort = op_abort,
    .rollback_possible = op_rollback_possible,
    .rollback = op_rollback,
    .info = op_info,
    .notify = op_notify,
    .event = op_event,
};

static void worker(void *unused) {
    (void)unused;
    static rx_item_t item; /* static: keep the stack for esp_ota_end */
    for (;;) {
        if (xQueueReceive(rx, &item, pdMS_TO_TICKS(1000)) == pdTRUE) {
            uint32_t now = now_ms();
            if (item.kind == RX_PACKET)
                whale_ota_packet(&core, &ops, item.bytes, item.len, now);
            else if (item.kind == RX_LINK)
                whale_ota_link_down(&core, &ops);
            else
                whale_ota_answer(&core, &ops, item.kind == RX_YES, now);
        }
        whale_ota_tick(&core, &ops, now_ms());
    }
}
static bool post(const rx_item_t *item) {
    if (!rx) {
        const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
        ops.max_size = next ? next->size : 0;
        ops.asset_id = whale_asset_pack_sha256;
        rx = xQueueCreate(WHALE_OTA_QUEUE, sizeof(rx_item_t));
        if (!rx)
            return false;
    }
    if (!worker_running) {
        /* Created on first use so devices that never update keep the RAM. */
        worker_running = xTaskCreate(worker, "whale_ota", WHALE_OTA_STACK, NULL, 3, NULL) == pdPASS;
        if (!worker_running)
            return false;
    }
    return xQueueSend(rx, item, 0) == pdTRUE;
}
void whale_ota_rx(const uint8_t *p, size_t n) {
    static rx_item_t item; /* BLE host stack headroom is small; callbacks are serialized */
    if (!n || n > sizeof item.bytes)
        return;
    item.kind = RX_PACKET;
    item.len = (uint8_t)n;
    memcpy(item.bytes, p, n);
    if (!post(&item) && p[0] != WOTA_DATA)
        ESP_LOGW(TAG, "OTA request dropped: queue unavailable");
}
void whale_ota_link_lost(void) {
    if (rx)
        post(&(rx_item_t){.kind = RX_LINK});
}
void whale_ota_reply(bool yes) {
    if (rx)
        post(&(rx_item_t){.kind = yes ? RX_YES : RX_NO});
}
void whale_ota_cancel(void) {
    if (rx)
        post(&(rx_item_t){.kind = RX_PACKET, .len = 1, .bytes = {WOTA_ABORT}});
}
void whale_ota_offer(char *version, size_t cap, uint32_t *size) {
    snprintf(version, cap, "%s", offer_version);
    *size = offer_size;
}
void whale_ota_boot(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    pending = running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
              state == ESP_OTA_IMG_PENDING_VERIFY;
    ESP_LOGI(TAG, "running %s from %s%s", esp_app_get_description()->version,
             running ? running->label : "?", pending ? " (pending self-test)" : "");
}
void whale_ota_assets_checked(bool assets_ok) {
    if (pending && !assets_ok) {
        ESP_LOGE(TAG, "self-test failed: asset pack mismatch, rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}
void whale_ota_healthy(void) {
    if (!pending)
        return;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        pending = false;
        ESP_LOGI(TAG, "self-test passed: image marked valid");
    }
}
bool whale_ota_pending(void) { return pending; }
