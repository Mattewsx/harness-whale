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
    uint8_t bad[] = {0xe0, 0x80, 0x80};
    assert(!whale_text(bad, 3));
    puts("wire: lengths, UTF-8, CRC, slot priority, duplicate and serial wrap passed");
}
