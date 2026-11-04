#include "whale_ble.h"
#include "esp_check.h"
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
static const ble_uuid128_t svc = UUID(0), cmd = UUID(1), notify = UUID(2);
static uint16_t notify_attr, serial[2];
static volatile uint16_t peer = BLE_HS_CONN_HANDLE_NONE;
static volatile bool locked, pair_pending;
static nvs_handle_t binding_store;
static bool mark_revoking(uint8_t value) {
    return nvs_set_u8(binding_store, "revoking", value) == ESP_OK &&
           nvs_commit(binding_store) == ESP_OK;
}
static uint8_t own;
static bool authorized(uint16_t h) {
    struct ble_gap_conn_desc d;
    return !locked && h == peer && !ble_gap_conn_find(h, &d) && d.sec_state.encrypted &&
           d.sec_state.authenticated;
}
static int access_cb(uint16_t h, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg) {
    if (!authorized(h))
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    whale_event_t e = {.kind = WE_FRAME};
    uint16_t len = 0;
    if (ctx->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
        ble_hs_mbuf_to_flat(ctx->om, e.bytes, 20, &len) || len < 8)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    e.len = len;
    return whale_post(&e) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
static const struct ble_gatt_chr_def chars[] = {
    {.uuid = &cmd.u,
     .access_cb = access_cb,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN},
    {.uuid = &notify.u,
     .access_cb = access_cb,
     .flags = BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &notify_attr},
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
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
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
        if (authorized(e->enc_change.conn_handle))
            whale_post(&(whale_event_t){.kind = WE_LINK});
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
    unsigned count = (n + 11) / 12;
    uint16_t id = ++serial[whale_control(type)];
    for (unsigned i = 0; i < count; i++) {
        uint8_t f[20] = {2, type, 0, 0, i, count};
        size_t size = n - i * 12;
        if (size > 12)
            size = 12;
        put16(f + 2, id);
        put16(f + 6, n);
        memcpy(f + 8, p + i * 12, size);
        struct os_mbuf *om = ble_hs_mbuf_from_flat(f, size + 8);
        if (!om || ble_gatts_notify_custom(h, notify_attr, om))
            return false;
    }
    return true;
}