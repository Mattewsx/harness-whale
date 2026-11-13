#include "whale_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    uint8_t r[97] = {0};
    put32(r, 1);
    put32(r + 4, 2);
    r[8] = 3;
    r[23] = 72;
    memset(r + 24, 'x', 72);
    whale_row_t row;
    assert(whale_parse_row(r, 96, &row, 200));
    assert(!whale_parse_row(r, 95, &row, 200));
    r[23] = 73;
    assert(!whale_parse_row(r, 97, &row, 200));
    assert(whale_crc(0, (const uint8_t *)"123456789", 9) == 0xcbf43926u);
    uint8_t f[20] = {2, 0x14, 0xff, 0xff, 0, 2, 15, 0};
    whale_rx_t rx = {0};
    whale_msg_t out;
    assert(whale_receive(&rx, f, 20, 0, &out) == 0);
    uint8_t page[20] = {2, 0x21, 0, 0, 0, 2, 15, 0};
    assert(whale_receive(&rx, page, 20, 1, &out) == 0);
    assert(whale_receive(&rx, page, 20, 2, &out) == 0 && rx.slots[0].next == 1 &&
           rx.slots[0].last_ms == 1);
    f[4] = 1;
    assert(whale_receive(&rx, f, 11, 3, &out) == 0 && rx.slots[0].active);
    uint8_t ctrl[9] = {2, 0x23, 1, 0, 0, 1, 1, 0, 4};
    assert(whale_receive(&rx, ctrl, 9, 4, &out) == 1 && out.type == 0x23);
    page[4] = 1;
    assert(whale_receive(&rx, page, 11, 5, &out) == 1 && out.type == 0x21 && out.size == 15);
    assert(whale_receive(&rx, page, 11, 6, &out) == 0);
    // Protocol 2.3: version-3 frames need a grant and use the negotiated payload.
    assert(whale_frame_grant(WHALE_CAP_LARGE_FRAMES, 244, 244, 244) == 244);
    assert(whale_frame_grant(WHALE_CAP_LARGE_FRAMES, 244, 180, 200) == 180);
    assert(whale_frame_grant(WHALE_CAP_LARGE_FRAMES, 512, 512, 512) == WHALE_FRAME_MAX);
    assert(whale_frame_grant(WHALE_CAP_LARGE_FRAMES, 244, 244, 20) == WHALE_FRAME_LEGACY);
    assert(whale_frame_grant(4095, 244, 244, 244) == WHALE_FRAME_LEGACY);
    whale_rx_t big = {0};
    uint8_t v3[WHALE_FRAME_MAX] = {3, 0x14, 7, 0, 0, 2, 0x2c, 1}; // 300 bytes in two parts
    for (unsigned k = 8; k < sizeof v3; k++)
        v3[k] = (uint8_t)k;
    assert(whale_receive(&big, v3, sizeof v3, 10, &out) == -1); // no grant yet
    big.frame = WHALE_FRAME_MAX;
    assert(whale_receive(&big, v3, sizeof v3, 11, &out) == 0 && big.slots[0].next == 1);
    uint8_t v3_tail[8 + 64] = {3, 0x14, 7, 0, 1, 2, 0x2c, 1};
    memset(v3_tail + 8, 0xab, 64);
    uint8_t v2_tail[20] = {2, 0x14, 7, 0, 1, 2, 0x2c, 1};
    assert(whale_receive(&big, v2_tail, sizeof v2_tail, 12, &out) == -1); // legacy-sized tail
    assert(whale_receive(&big, v3_tail, sizeof v3_tail - 1, 13, &out) == -1); // short tail
    assert(whale_receive(&big, v3_tail, sizeof v3_tail, 14, &out) == 1 && out.size == 300 &&
           out.bytes[0] == 8 && out.bytes[235] == 243 && out.bytes[236] == 0xab);
    v3[2] = 8;
    v3_tail[2] = 8;
    assert(whale_receive(&big, v3, sizeof v3, 15, &out) == 0);
    big.frame = 200; // a different grant must not complete a message started at 244
    assert(whale_receive(&big, v3_tail, 8 + 108, 16, &out) == -1 && !big.slots[0].active);
    big.frame = WHALE_FRAME_MAX;
    uint8_t oversize[WHALE_FRAME_MAX + 1] = {3, 0x14, 10, 0, 0, 2, 0x2c, 1};
    assert(whale_receive(&big, oversize, sizeof oversize, 18, &out) == -1);
    uint8_t legacy[20] = {2, 0x23, 2, 0, 0, 1, 1, 0, 4};
    assert(whale_receive(&big, legacy, 9, 19, &out) == 1); // 2.2 frames stay valid after a grant
    /* Logical reconnect without GAP disconnect: stale sequence state in both
     * lanes and an unfinished bulk transfer must not swallow the new HELLO. */
    whale_rx_t reopen = {0};
    reopen.frame = WHALE_FRAME_MAX;
    assert(whale_receive(&reopen, v3, sizeof v3, 20, &out) == 0);
    ctrl[2] = 90;
    assert(whale_receive(&reopen, ctrl, sizeof ctrl, 21, &out) == 1);
    uint8_t hi[18] = {2, 1, 1, 0, 0, 1, 10, 0, 2, 2, 63, 0, 0, 0, 244, 0, 244, 0};
    whale_rx_t saved = reopen;
    hi[8] = 3; // unsupported HELLO is not a session boundary
    assert(whale_receive(&reopen, hi, sizeof hi, 22, &out) == 0);
    assert(!memcmp(&saved, &reopen, sizeof saved));
    hi[8] = 2;
    assert(whale_receive(&reopen, hi, sizeof hi, 23, &out) == 1 && out.type == 1);
    assert(!reopen.frame && !reopen.slots[0].seen && !reopen.slots[0].active);
    // Retrying a valid HELLO can recover a lost ACK, even with the same serial.
    assert(whale_receive(&reopen, hi, sizeof hi, 24, &out) == 1);
    assert(whale_receive(&reopen, ctrl, sizeof ctrl, 25, &out) == 1);
    assert(whale_receive(&reopen, ctrl, sizeof ctrl, 26, &out) == 0); // other duplicates stay suppressed
    puts("wire logical reconnect: valid HELLO resets serial/reassembly/grant; malformed HELLO and non-HELLO duplicates stay protected");
    uint8_t bad[] = {0xe0, 0x80, 0x80};
    assert(!whale_text(bad, 3));
    puts("wire: lengths, UTF-8, CRC, slot priority, duplicate, serial wrap and 2.3 large frames passed");
}
