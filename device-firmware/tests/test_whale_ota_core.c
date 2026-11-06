#undef NDEBUG
#include "whale_ota_core.h"
#include "whale_asset_pack.h"
#include "whale_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t flash[8192], last[96], digest[32], asset[8] = {1, 2, 3, 4, 5, 6, 7, 8};
static size_t written, last_len;
static int begins, aborts, activations, events, finish_code, fail_write_at = -1;
static bool rollback_ok, activate_ok = true;
static uint8_t ev_state, ev_code, ev_percent;
static char finished_version[32];

static bool f_begin(void *c, uint32_t size) {
    (void)c;
    assert(size <= sizeof flash);
    begins++;
    written = 0;
    return true;
}
static bool f_write(void *c, const uint8_t *p, size_t n) {
    (void)c;
    if (fail_write_at >= 0 && written >= (size_t)fail_write_at)
        return false;
    memcpy(flash + written, p, n);
    written += n;
    return true;
}
static int f_finish(void *c, const char *version, uint8_t out[32]) {
    (void)c;
    strcpy(finished_version, version);
    memcpy(out, digest, 32);
    return finish_code;
}
static bool f_activate(void *c) {
    (void)c;
    activations++;
    return activate_ok;
}
static void f_abort(void *c) {
    (void)c;
    aborts++;
}
static bool f_rollback(void *c) {
    (void)c;
    return rollback_ok;
}
static int rollbacks;
static bool f_do_rollback(void *c) {
    (void)c;
    rollbacks++;
    return activate_ok;
}
static size_t f_info(void *c, uint8_t *out, size_t cap) {
    (void)c;
    assert(cap >= 4);
    memcpy(out, "\x01\x04xy", 4);
    return 4;
}
static void f_notify(void *c, const uint8_t *p, size_t n) {
    (void)c;
    assert(n <= sizeof last);
    memcpy(last, p, n);
    last_len = n;
}
static void f_event(void *c, uint8_t s, uint8_t code, uint8_t percent) {
    (void)c;
    events++;
    ev_state = s;
    ev_code = code;
    ev_percent = percent;
}
static const whale_ota_ops_t ops = {
    .max_size = sizeof flash,
    .asset_id = asset,
    .begin = f_begin,
    .write = f_write,
    .finish = f_finish,
    .activate = f_activate,
    .abort = f_abort,
    .rollback_possible = f_rollback,
    .rollback = f_do_rollback,
    .info = f_info,
    .notify = f_notify,
    .event = f_event,
};
static void expect_status(uint8_t state, uint8_t code, uint32_t offset) {
    assert(last_len == 7 && last[0] == WOTA_STATUS);
    if (last[1] != state || last[2] != code || w32(last + 3) != offset) {
        fprintf(stderr, "status %u/%u/%u, want %u/%u/%u\n", last[1], last[2],
                (unsigned)w32(last + 3), state, code, (unsigned)offset);
        assert(0);
    }
}
static size_t begin_packet(uint8_t *p, uint32_t size, const char *version) {
    size_t v = strlen(version);
    p[0] = WOTA_BEGIN;
    p[1] = 1;
    put32(p + 2, size);
    memset(p + 6, 0xab, 32);
    memcpy(p + 38, asset, 8);
    p[46] = (uint8_t)v;
    memcpy(p + 47, version, v);
    return 47 + v;
}
static void send_data(whale_ota_t *o, uint32_t offset, size_t n, uint32_t now) {
    uint8_t p[5 + WHALE_OTA_MAX_CHUNK];
    p[0] = WOTA_DATA;
    put32(p + 1, offset);
    for (size_t i = 0; i < n; i++)
        p[5 + i] = (uint8_t)(offset + i);
    whale_ota_packet(o, &ops, p, 5 + n, now);
}
static void reset(whale_ota_t *o) {
    memset(o, 0, sizeof *o);
    memset(digest, 0xab, sizeof digest);
    begins = aborts = activations = events = 0;
    finish_code = WOTA_OK;
    fail_write_at = -1;
    activate_ok = true;
    rollback_ok = false;
}
static void start(whale_ota_t *o, uint32_t size) {
    uint8_t p[96];
    whale_ota_packet(o, &ops, p, begin_packet(p, size, "whale-v1.4.0"), 0);
    expect_status(WOTA_CONFIRM, WOTA_OK, 0);
    assert(ev_state == WOTA_CONFIRM && !strcmp(o->version, "whale-v1.4.0"));
    assert(!whale_ota_answer(o, &ops, true, 10));
    expect_status(WOTA_RECEIVING, WOTA_OK, 0);
    assert(begins == 1);
}

static void test_full_transfer(void) {
    whale_ota_t o;
    reset(&o);
    start(&o, 5000);
    uint32_t offset = 0;
    while (offset < 5000) {
        size_t n = 5000 - offset < WHALE_OTA_MAX_CHUNK ? 5000 - offset : WHALE_OTA_MAX_CHUNK;
        send_data(&o, offset, n, 20);
        offset += n;
    }
    expect_status(WOTA_RECEIVING, WOTA_OK, 5000);
    assert(written == 5000 && flash[4999] == (uint8_t)4999);
    assert(ev_percent == 100);
    uint8_t end = WOTA_END;
    whale_ota_packet(&o, &ops, &end, 1, 30);
    expect_status(WOTA_DONE, WOTA_OK, 5000);
    assert(activations == 1 && aborts == 0 && ev_state == WOTA_DONE);
    assert(!strcmp(finished_version, "whale-v1.4.0"));
    /* Nothing but INFO is accepted until the restart. */
    uint8_t p[96];
    whale_ota_packet(&o, &ops, p, begin_packet(p, 300, "x"), 40);
    expect_status(WOTA_DONE, WOTA_E_BUSY, 5000);
}
static void test_acks_every_window(void) {
    whale_ota_t o;
    reset(&o);
    start(&o, 8000);
    uint32_t offset = 0, acks = 0;
    while (offset < 8000) {
        size_t n = 8000 - offset < WHALE_OTA_MAX_CHUNK ? 8000 - offset : WHALE_OTA_MAX_CHUNK;
        last_len = 0;
        send_data(&o, offset, n, 20);
        offset += n;
        if (last_len)
            acks++;
    }
    assert(acks == 4); /* at >= 2048, 4096 and 6144 bytes, then at the end */
}
static void test_offset_gap_requests_one_rewind(void) {
    whale_ota_t o;
    reset(&o);
    start(&o, 2000);
    send_data(&o, 0, 100, 20);
    last_len = 0;
    send_data(&o, 200, 100, 21); /* chunk at 100 was dropped */
    expect_status(WOTA_RECEIVING, WOTA_E_OFFSET, 100);
    last_len = 0;
    send_data(&o, 300, 100, 22);
    assert(last_len == 0); /* rest of the stale window is ignored quietly */
    send_data(&o, 100, 100, 23);
    assert(o.offset == 200 && written == 200 && flash[150] == 150);
    send_data(&o, 250, 10, 24); /* a new gap gets a new rewind request */
    expect_status(WOTA_RECEIVING, WOTA_E_OFFSET, 200);
}
static void test_hash_mismatch_keeps_old_image(void) {
    whale_ota_t o;
    reset(&o);
    start(&o, 300);
    send_data(&o, 0, 236, 1);
    send_data(&o, 236, 64, 2);
    digest[0] ^= 1;
    uint8_t end = WOTA_END;
    whale_ota_packet(&o, &ops, &end, 1, 3);
    expect_status(WOTA_FAILED, WOTA_E_HASH, 300);
    assert(activations == 0 && ev_state == WOTA_FAILED && ev_code == WOTA_E_HASH);
    assert(aborts == 0); /* finish() already closed the handle */
}
static void test_image_rejected_by_finish(void) {
    whale_ota_t o;
    reset(&o);
    start(&o, 300);
    send_data(&o, 0, 236, 1);
    send_data(&o, 236, 64, 2);
    finish_code = WOTA_E_IMAGE;
    uint8_t end = WOTA_END;
    whale_ota_packet(&o, &ops, &end, 1, 3);
    expect_status(WOTA_FAILED, WOTA_E_IMAGE, 300);
    assert(activations == 0);
}
static void test_short_end_and_overrun(void) {
    whale_ota_t o;
    reset(&o);
    start(&o, 300);
    send_data(&o, 0, 200, 1);
    uint8_t end = WOTA_END;
    whale_ota_packet(&o, &ops, &end, 1, 2);
    expect_status(WOTA_FAILED, WOTA_E_SIZE, 200);
    assert(aborts == 1 && activations == 0);
    reset(&o);
    start(&o, 300);
    send_data(&o, 0, 236, 1);
    send_data(&o, 236, 100, 2); /* 36 bytes beyond the announced size */
    expect_status(WOTA_FAILED, WOTA_E_SIZE, 236);
    assert(aborts == 1);
}
static void test_begin_validation(void) {
    whale_ota_t o;
    uint8_t p[96];
    reset(&o);
    whale_ota_packet(&o, &ops, p, begin_packet(p, sizeof flash + 1, "v"), 0);
    expect_status(WOTA_FAILED, WOTA_E_SIZE, 0);
    whale_ota_packet(&o, &ops, p, begin_packet(p, 100, "v"), 0);
    expect_status(WOTA_FAILED, WOTA_E_SIZE, 0);
    size_t n = begin_packet(p, 1000, "bad version");
    whale_ota_packet(&o, &ops, p, n, 0);
    expect_status(WOTA_FAILED, WOTA_E_PACKET, 0);
    n = begin_packet(p, 1000, "v1");
    whale_ota_packet(&o, &ops, p, n - 1, 0);
    expect_status(WOTA_FAILED, WOTA_E_PACKET, 0);
    n = begin_packet(p, 1000, "v1");
    p[40] ^= 0xff;
    whale_ota_packet(&o, &ops, p, n, 0);
    expect_status(WOTA_FAILED, WOTA_E_ASSETS, 0);
    assert(begins == 0 && events == 0);
    /* A second BEGIN during a session is refused without disturbing it. */
    start(&o, 1000);
    whale_ota_packet(&o, &ops, p, begin_packet(p, 2000, "v2"), 1);
    expect_status(WOTA_RECEIVING, WOTA_E_BUSY, 0);
    assert(o.size == 1000);
}
static void test_decline_timeout_and_link_loss(void) {
    whale_ota_t o;
    uint8_t p[96];
    reset(&o);
    whale_ota_packet(&o, &ops, p, begin_packet(p, 1000, "v1"), 0);
    assert(!whale_ota_answer(&o, &ops, false, 5));
    expect_status(WOTA_CANCELLED, WOTA_E_DECLINED, 0);
    assert(begins == 0 && aborts == 0);
    send_data(&o, 0, 10, 6); /* ignored after cancel */
    assert(written == 0);

    reset(&o);
    whale_ota_packet(&o, &ops, p, begin_packet(p, 1000, "v1"), 0);
    whale_ota_tick(&o, &ops, WHALE_OTA_CONFIRM_MS - 1);
    assert(o.state == WOTA_CONFIRM);
    whale_ota_tick(&o, &ops, WHALE_OTA_CONFIRM_MS);
    expect_status(WOTA_CANCELLED, WOTA_E_TIMEOUT, 0);

    reset(&o);
    start(&o, 1000);
    send_data(&o, 0, 100, 100);
    whale_ota_tick(&o, &ops, 100 + WHALE_OTA_IDLE_MS);
    expect_status(WOTA_FAILED, WOTA_E_TIMEOUT, 100);
    assert(aborts == 1);

    reset(&o);
    start(&o, 1000);
    whale_ota_link_down(&o, &ops);
    expect_status(WOTA_FAILED, WOTA_E_LINK, 0);
    assert(aborts == 1);
    last_len = 0;
    whale_ota_link_down(&o, &ops);
    assert(last_len == 0);
}
static void test_flash_write_failure_and_abort(void) {
    whale_ota_t o;
    reset(&o);
    start(&o, 1000);
    fail_write_at = 0;
    send_data(&o, 0, 100, 1);
    expect_status(WOTA_FAILED, WOTA_E_FLASH, 0);
    assert(aborts == 1);
    reset(&o);
    start(&o, 1000);
    uint8_t abort_op = WOTA_ABORT;
    whale_ota_packet(&o, &ops, &abort_op, 1, 2);
    expect_status(WOTA_CANCELLED, WOTA_OK, 0);
    assert(aborts == 1);
}
static void test_info_and_rollback(void) {
    whale_ota_t o;
    reset(&o);
    uint8_t op = WOTA_INFO;
    whale_ota_packet(&o, &ops, &op, 1, 0);
    assert(last_len == 5 && last[0] == WOTA_INFO_REPLY && !memcmp(last + 1, "\x01\x04xy", 4));
    op = WOTA_ROLLBACK;
    whale_ota_packet(&o, &ops, &op, 1, 0);
    expect_status(WOTA_FAILED, WOTA_E_NO_ROLLBACK, 0);
    rollback_ok = true;
    whale_ota_packet(&o, &ops, &op, 1, 0);
    expect_status(WOTA_ROLLBACK_CONFIRM, WOTA_OK, 0);
    assert(ev_state == WOTA_ROLLBACK_CONFIRM);
    assert(!whale_ota_answer(&o, &ops, false, 1));
    expect_status(WOTA_CANCELLED, WOTA_E_DECLINED, 0);
    activate_ok = false;
    whale_ota_packet(&o, &ops, &op, 1, 2);
    assert(!whale_ota_answer(&o, &ops, true, 3));
    expect_status(WOTA_FAILED, WOTA_E_NO_ROLLBACK, 0);
    activate_ok = true;
    whale_ota_packet(&o, &ops, &op, 1, 4);
    assert(whale_ota_answer(&o, &ops, true, 5));
    expect_status(WOTA_DONE, WOTA_OK, 0);
    assert(rollbacks == 2 && activations == 0);
}
static void test_asset_pack_parser(void) {
    uint8_t pack[0x100 + 16];
    memset(pack, 0xff, sizeof pack);
    memcpy(pack, "WHALEPAK", 8);
    put32(pack + 8, 1);
    put32(pack + 12, 2);
    put32(pack + 16, 0x100);
    put32(pack + 20, 16);
    memset(pack + 24, 0x5a, 32);
    memset(pack + 56, 0, 48);
    memcpy(pack + 56, "a.rgb", 5);
    put32(pack + 72, 0);
    put32(pack + 76, 8);
    memcpy(pack + 80, "b.pcm", 5);
    put32(pack + 96, 8);
    put32(pack + 100, 8);
    put32(pack + 0xfc, whale_crc(0, pack, 0xfc));
    whale_pack_t parsed;
    assert(whale_pack_parse(pack, sizeof pack, &parsed));
    assert(parsed.count == 2 && parsed.body_size == 16 && parsed.sha256[0] == 0x5a);
    assert(whale_pack_find(&parsed, "b.pcm", 8)->offset == 8);
    assert(!whale_pack_find(&parsed, "b.pcm", 9));
    assert(!whale_pack_find(&parsed, "c", 8));
    assert(!whale_pack_parse(pack, sizeof pack - 1, &parsed)); /* body truncated */
    pack[0xfc] ^= 1;
    assert(!whale_pack_parse(pack, sizeof pack, &parsed)); /* header CRC */
    pack[0xfc] ^= 1;
    put32(pack + 100, 9);
    put32(pack + 0xfc, whale_crc(0, pack, 0xfc));
    assert(!whale_pack_parse(pack, sizeof pack, &parsed)); /* entry past body */
}
int main(void) {
    test_full_transfer();
    test_acks_every_window();
    test_offset_gap_requests_one_rewind();
    test_hash_mismatch_keeps_old_image();
    test_image_rejected_by_finish();
    test_short_end_and_overrun();
    test_begin_validation();
    test_decline_timeout_and_link_loss();
    test_flash_write_failure_and_abort();
    test_info_and_rollback();
    test_asset_pack_parser();
    puts("whale OTA core: PASS");
    return 0;
}
