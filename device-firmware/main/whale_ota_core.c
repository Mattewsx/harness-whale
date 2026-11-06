#include "whale_ota_core.h"
#include "whale_wire.h"
#include <string.h>

static void status(whale_ota_t *o, const whale_ota_ops_t *ops, uint8_t code) {
    uint8_t p[7] = {WOTA_STATUS, o->state, code};
    put32(p + 3, o->offset);
    ops->notify(ops->ctx, p, sizeof p);
}
static void event(whale_ota_t *o, const whale_ota_ops_t *ops) {
    ops->event(ops->ctx, o->state, o->code, o->percent);
}
/* Ends a session. The previous image stays bootable: the boot slot is only
 * switched after every check in finish() passed. */
static void stop(whale_ota_t *o, const whale_ota_ops_t *ops, uint8_t state, uint8_t code) {
    if (o->begun)
        ops->abort(ops->ctx);
    o->begun = false;
    o->state = state;
    o->code = code;
    status(o, ops, code);
    event(o, ops);
}
bool whale_ota_valid_version(const uint8_t *p, size_t n) {
    if (!n || n > WHALE_OTA_VERSION_MAX)
        return false;
    for (size_t i = 0; i < n; i++)
        if (p[i] < 0x21 || p[i] > 0x7e)
            return false;
    return true;
}
static bool active(const whale_ota_t *o) {
    return o->state == WOTA_CONFIRM || o->state == WOTA_RECEIVING ||
           o->state == WOTA_VERIFYING || o->state == WOTA_ROLLBACK_CONFIRM;
}
static void begin(whale_ota_t *o, const whale_ota_ops_t *ops, const uint8_t *p, size_t n,
                  uint32_t now) {
    if (active(o)) {
        status(o, ops, WOTA_E_BUSY);
        return;
    }
    if (n < 47 || p[1] != 1 || n != 47u + p[46] || !whale_ota_valid_version(p + 47, p[46])) {
        o->state = WOTA_FAILED;
        o->offset = 0;
        status(o, ops, WOTA_E_PACKET);
        return;
    }
    uint32_t size = w32(p + 2);
    o->offset = 0;
    if (size < 256 || size > ops->max_size) {
        o->state = WOTA_FAILED;
        status(o, ops, WOTA_E_SIZE);
        return;
    }
    if (memcmp(p + 38, ops->asset_id, 8)) {
        /* The image was built for a different asset pack: it would fail its
         * self-test and roll back, so refuse before touching Flash. */
        o->state = WOTA_FAILED;
        status(o, ops, WOTA_E_ASSETS);
        return;
    }
    o->size = size;
    o->acked = 0;
    o->percent = 0;
    o->nacked = false;
    memcpy(o->sha256, p + 6, 32);
    memcpy(o->version, p + 47, p[46]);
    o->version[p[46]] = 0;
    o->state = WOTA_CONFIRM;
    o->code = WOTA_OK;
    o->at = now;
    status(o, ops, WOTA_OK);
    event(o, ops);
}
static void data(whale_ota_t *o, const whale_ota_ops_t *ops, const uint8_t *p, size_t n,
                 uint32_t now) {
    if (o->state != WOTA_RECEIVING)
        return; /* Late chunks after abort/failure are dropped silently. */
    if (n < 6 || n > 5 + WHALE_OTA_MAX_CHUNK) {
        stop(o, ops, WOTA_FAILED, WOTA_E_PACKET);
        return;
    }
    uint32_t offset = w32(p + 1), size = n - 5;
    if (offset != o->offset) {
        /* A write without response was dropped (queue full) or reordered.
         * Ask once for a rewind; ignore the rest of the stale window. */
        if (!o->nacked) {
            o->nacked = true;
            status(o, ops, WOTA_E_OFFSET);
        }
        return;
    }
    if (size > o->size - o->offset) {
        stop(o, ops, WOTA_FAILED, WOTA_E_SIZE);
        return;
    }
    if (!ops->write(ops->ctx, p + 5, size)) {
        stop(o, ops, WOTA_FAILED, WOTA_E_FLASH);
        return;
    }
    o->nacked = false;
    o->offset += size;
    o->at = now;
    if (o->offset - o->acked >= WHALE_OTA_ACK_BYTES || o->offset == o->size) {
        o->acked = o->offset;
        status(o, ops, WOTA_OK);
    }
    uint8_t percent = (uint8_t)((uint64_t)o->offset * 100u / o->size);
    if (percent != o->percent) {
        o->percent = percent;
        event(o, ops);
    }
}
static void end(whale_ota_t *o, const whale_ota_ops_t *ops) {
    if (o->state != WOTA_RECEIVING) {
        status(o, ops, WOTA_E_PACKET);
        return;
    }
    if (o->offset != o->size) {
        stop(o, ops, WOTA_FAILED, WOTA_E_SIZE);
        return;
    }
    o->state = WOTA_VERIFYING;
    event(o, ops);
    uint8_t digest[32];
    int code = ops->finish(ops->ctx, o->version, digest);
    o->begun = false; /* finish() closed the handle whatever the result. */
    if (code != WOTA_OK) {
        stop(o, ops, WOTA_FAILED, (uint8_t)code);
        return;
    }
    if (memcmp(digest, o->sha256, 32)) {
        stop(o, ops, WOTA_FAILED, WOTA_E_HASH);
        return;
    }
    if (!ops->activate(ops->ctx)) {
        stop(o, ops, WOTA_FAILED, WOTA_E_FLASH);
        return;
    }
    o->state = WOTA_DONE;
    o->code = WOTA_OK;
    o->percent = 100;
    status(o, ops, WOTA_OK);
    event(o, ops);
}
void whale_ota_packet(whale_ota_t *o, const whale_ota_ops_t *ops, const uint8_t *p, size_t n,
                      uint32_t now) {
    if (!n)
        return;
    if (o->state == WOTA_DONE && p[0] != WOTA_INFO) {
        status(o, ops, WOTA_E_BUSY); /* Restart pending; nothing else is accepted. */
        return;
    }
    switch (p[0]) {
    case WOTA_BEGIN:
        begin(o, ops, p, n, now);
        break;
    case WOTA_DATA:
        data(o, ops, p, n, now);
        break;
    case WOTA_END:
        if (n != 1) {
            status(o, ops, WOTA_E_PACKET);
            break;
        }
        end(o, ops);
        break;
    case WOTA_ABORT:
        if (active(o))
            stop(o, ops, WOTA_CANCELLED, WOTA_OK);
        else
            status(o, ops, WOTA_OK);
        break;
    case WOTA_INFO: {
        uint8_t out[96] = {WOTA_INFO_REPLY};
        size_t size = ops->info(ops->ctx, out + 1, sizeof out - 1);
        ops->notify(ops->ctx, out, size + 1);
        break;
    }
    case WOTA_ROLLBACK:
        if (active(o)) {
            status(o, ops, WOTA_E_BUSY);
        } else if (!ops->rollback_possible(ops->ctx)) {
            o->state = WOTA_FAILED;
            o->offset = 0;
            status(o, ops, WOTA_E_NO_ROLLBACK);
        } else {
            o->state = WOTA_ROLLBACK_CONFIRM;
            o->code = WOTA_OK;
            o->offset = 0;
            o->at = now;
            status(o, ops, WOTA_OK);
            event(o, ops);
        }
        break;
    default:
        status(o, ops, WOTA_E_PACKET);
        break;
    }
}
bool whale_ota_answer(whale_ota_t *o, const whale_ota_ops_t *ops, bool yes, uint32_t now) {
    if (o->state == WOTA_ROLLBACK_CONFIRM) {
        if (!yes) {
            stop(o, ops, WOTA_CANCELLED, WOTA_E_DECLINED);
            return false;
        }
        if (!ops->rollback(ops->ctx)) {
            stop(o, ops, WOTA_FAILED, WOTA_E_NO_ROLLBACK);
            return false;
        }
        o->state = WOTA_DONE;
        o->code = WOTA_OK;
        status(o, ops, WOTA_OK);
        event(o, ops);
        return true;
    }
    if (o->state != WOTA_CONFIRM)
        return false;
    if (!yes) {
        stop(o, ops, WOTA_CANCELLED, WOTA_E_DECLINED);
        return false;
    }
    if (!ops->begin(ops->ctx, o->size)) {
        stop(o, ops, WOTA_FAILED, WOTA_E_FLASH);
        return false;
    }
    o->begun = true;
    o->state = WOTA_RECEIVING;
    o->at = now;
    status(o, ops, WOTA_OK);
    event(o, ops);
    return false;
}
void whale_ota_tick(whale_ota_t *o, const whale_ota_ops_t *ops, uint32_t now) {
    uint32_t idle = now - o->at;
    if ((o->state == WOTA_CONFIRM || o->state == WOTA_ROLLBACK_CONFIRM) &&
        idle >= WHALE_OTA_CONFIRM_MS)
        stop(o, ops, WOTA_CANCELLED, WOTA_E_TIMEOUT);
    else if (o->state == WOTA_RECEIVING && idle >= WHALE_OTA_IDLE_MS)
        stop(o, ops, WOTA_FAILED, WOTA_E_TIMEOUT);
}
void whale_ota_link_down(whale_ota_t *o, const whale_ota_ops_t *ops) {
    if (active(o))
        stop(o, ops, WOTA_FAILED, WOTA_E_LINK);
}
