#include "whale_ble.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
void ble_store_config_init(void);

#define UUID(n)                                                                                    \
    BLE_UUID128_INIT(0x80, 0x90, 0x56, 0xc2, 0xc1, 0x37, 0x24, 0xa2, 0xc5, 0x48, 0x3a, 0x66, n,    \
                     0x02, 0xb5, 0x13)
static const ble_uuid128_t svc = UUID(0), cmd = UUID(1), notify = UUID(2), audio = UUID(3);
static uint16_t notify_attr, audio_attr, serial[2];
static volatile bool audio_subscribed;
static SemaphoreHandle_t send_lock;
static volatile uint16_t peer = BLE_HS_CONN_HANDLE_NONE;
static volatile bool locked, pair_pending;
static nvs_handle_t binding_store;
static bool mark_revoking(uint8_t value) {
    return nvs_set_u8(binding_store, "revoking", value) == ESP_OK &&
           nvs_commit(binding_store) == ESP_OK;
}
static uint8_t own;
#define LARGE_SLOTS 8u
static uint8_t large[LARGE_SLOTS][WHALE_FRAME_MAX];
static uint32_t large_head, large_tail; // single producer (host task), single consumer (app)
static volatile uint16_t tx_frame = WHALE_FRAME_LEGACY;
const uint8_t *whale_ble_frame(uint32_t index) { return large[index % LARGE_SLOTS]; }
void whale_ble_frame_release(void) { __atomic_fetch_add(&large_tail, 1, __ATOMIC_RELEASE); }
uint16_t whale_ble_frame_limit(void) {
    uint16_t mtu = peer == BLE_HS_CONN_HANDLE_NONE ? 0 : ble_att_mtu(peer);
    if (mtu < WHALE_FRAME_LEGACY + 3)
        return WHALE_FRAME_LEGACY;
    uint16_t frame = (uint16_t)(mtu - 3u);
    return frame > WHALE_FRAME_MAX ? WHALE_FRAME_MAX : frame;
}
void whale_ble_set_frame(uint16_t frame) {
    tx_frame = frame > WHALE_FRAME_LEGACY && frame <= WHALE_FRAME_MAX ? frame : WHALE_FRAME_LEGACY;
}
static bool authorized(uint16_t h) {
    struct ble_gap_conn_desc d;
    return !locked && h == peer && !ble_gap_conn_find(h, &d) && d.sec_state.encrypted &&
           d.sec_state.authenticated;
}
static int access_cb(uint16_t h, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg) {
    if (!authorized(h))
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    whale_event_t e = {.kind = WE_FRAME};
    uint16_t len = 0, total = ctx->om ? OS_MBUF_PKTLEN(ctx->om) : 0;
    if (ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR || total < 8 || total > WHALE_FRAME_MAX)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (total <= sizeof e.bytes) {
        if (ble_hs_mbuf_to_flat(ctx->om, e.bytes, sizeof e.bytes, &len))
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        e.len = len;
        return whale_post(&e) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    uint32_t head = large_head;
    if (head - __atomic_load_n(&large_tail, __ATOMIC_ACQUIRE) >= LARGE_SLOTS)
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    if (ble_hs_mbuf_to_flat(ctx->om, large[head % LARGE_SLOTS], WHALE_FRAME_MAX, &len) ||
        len != total)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    e.len = len;
    e.value = head;
    large_head = head + 1;
    if (whale_post(&e))
        return 0;
    large_head = head;
    return BLE_ATT_ERR_INSUFFICIENT_RES;
}
static const struct ble_gatt_chr_def chars[] = {
    {.uuid = &cmd.u,
     .access_cb = access_cb,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN},
    {.uuid = &notify.u,
     .access_cb = access_cb,
     .flags = BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &notify_attr},
    {.uuid = &audio.u,
     .access_cb = access_cb,
     .flags = BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &audio_attr},
    {0}};
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &svc.u, .characteristics = chars}, {0}};
static int gap(struct ble_gap_event *e, void *arg);
static void advertise(void) {
    const char *name = "HARNESS-WHALE";
    struct ble_hs_adv_fields f = {.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
                                  .name = (const uint8_t *)name,
                                  .name_len = strlen(name),
                                  .name_is_complete = 1};
    struct ble_gap_adv_params adv = {.conn_mode = BLE_GAP_CONN_MODE_UND,
                                     .disc_mode = BLE_GAP_DISC_MODE_GEN};
    ble_gap_adv_set_fields(&f);
    ble_gap_adv_start(own, NULL, BLE_HS_FOREVER, &adv, gap, NULL);
}
static int gap(struct ble_gap_event *e, void *arg) {
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT: {
        if (e->connect.status) {
            advertise();
            break;
        }
        uint16_t h = e->connect.conn_handle;
        struct ble_gap_conn_desc d;
        int count = 0;
        ble_addr_t bonded[1];
        if (locked || ble_gap_conn_find(h, &d) || ble_store_util_bonded_peers(bonded, &count, 1) ||
            (count && ble_addr_cmp(bonded, &d.peer_id_addr))) {
            ble_gap_terminate(h, BLE_ERR_REM_USER_CONN_TERM);
            break;
        }
        peer = h;
        ESP_LOGI("whale_ble", "connected handle=%u", (unsigned)h);
        break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (e->subscribe.attr_handle == audio_attr)
            audio_subscribed = e->subscribe.cur_notify;
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI("whale_ble", "disconnected reason=0x%x", (unsigned)e->disconnect.reason);
        audio_subscribed = false;
        tx_frame = WHALE_FRAME_LEGACY;
        peer = BLE_HS_CONN_HANDLE_NONE;
        pair_pending = false;
        whale_post(&(whale_event_t){.kind = WE_DOWN});
        advertise();
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (e->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            pair_pending = true;
            whale_post(&(whale_event_t){.kind = WE_PAIR, .value = e->passkey.params.numcmp});
        } else
            ble_gap_terminate(peer, BLE_ERR_REM_USER_CONN_TERM);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (authorized(e->enc_change.conn_handle)) {
            whale_post(&(whale_event_t){.kind = WE_LINK});
        }
        break;
    default:
        break;
    }
    return 0;
}
void whale_pair(bool yes) {
    if (pair_pending) {
        struct ble_sm_io io = {.action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = yes};
        pair_pending = false;
        ble_sm_inject_io(peer, &io);
    }
}
bool whale_unbind(void) {
    locked = true;
    if (!mark_revoking(1))
        return false;
    uint16_t old = peer;
    peer = BLE_HS_CONN_HANDLE_NONE;
    if (old != BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(old, BLE_ERR_REM_USER_CONN_TERM);
    if (ble_store_clear() || !mark_revoking(0))
        return false;
    locked = false;
    return true;
}
static void sync_host(void) {
    uint8_t pending = 0;
    esp_err_t rc = nvs_get_u8(binding_store, "revoking", &pending);
    if (rc != ESP_OK && rc != ESP_ERR_NVS_NOT_FOUND)
        locked = true;
    if (pending && !whale_unbind())
        locked = true;
    if (!ble_hs_util_ensure_addr(0) && !ble_hs_id_infer_auto(0, &own))
        advertise();
}
static void host_task(void *arg) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}
void whale_ble_start(void) {
    ESP_ERROR_CHECK(nvs_open("whale_bond", NVS_READWRITE, &binding_store));
    ESP_ERROR_CHECK(nimble_port_init());
    send_lock = xSemaphoreCreateMutex();
    assert(send_lock);
    ble_att_set_preferred_mtu(247);
    ble_hs_cfg.sync_cb = sync_host;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = ble_hs_cfg.sm_our_key_dist;
    ble_store_config_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("HARNESS-WHALE");
    ESP_ERROR_CHECK(ble_gatts_count_cfg(services));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(services));
    nimble_port_freertos_init(host_task);
}
bool whale_send(uint8_t type, const uint8_t *p, size_t n) {
    uint16_t h = peer;
    if (n > 96 || !authorized(h))
        return false;
    if (!send_lock || xSemaphoreTake(send_lock, pdMS_TO_TICKS(50)) != pdTRUE)
        return false;
    // Version 3 frames carry the negotiated payload; version 2 keeps 12-byte fragments.
    uint16_t frame = tx_frame;
    unsigned cap = frame - 8u, count = (n + cap - 1) / cap;
    uint16_t id = ++serial[whale_control(type)];
    static uint8_t f[WHALE_FRAME_MAX]; // guarded by send_lock; keeps the caller's stack small
    for (unsigned i = 0; i < count; i++) {
        f[0] = frame > WHALE_FRAME_LEGACY ? 3 : 2;
        f[1] = type;
        f[4] = i;
        f[5] = count;
        size_t size = n - i * cap;
        if (size > cap)
            size = cap;
        put16(f + 2, id);
        put16(f + 6, n);
        memcpy(f + 8, p + i * cap, size);
        struct os_mbuf *om = ble_hs_mbuf_from_flat(f, size + 8);
        if (!om || ble_gatts_notify_custom(h, notify_attr, om)) {
            xSemaphoreGive(send_lock);
            return false;
        }
    }
    xSemaphoreGive(send_lock);
    return true;
}
bool whale_ble_audio_ready(void) {
    return audio_subscribed && authorized(peer) && ble_att_mtu(peer) >= 67;
}
bool whale_ble_audio_send(const uint8_t *p, size_t n) {
    if (n != 62 || !whale_ble_audio_ready())
        return false;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(p, n);
    return om && ble_gatts_notify_custom(peer, audio_attr, om) == 0;
}
