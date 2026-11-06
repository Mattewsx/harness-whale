#include "whale_wire.h"
#include <string.h>

bool whale_control(uint8_t t) {
    switch (t) {
    case 0x50:
    case 0x51:
    case 0x52:
    case 0x40:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x4a:
    case 1:
    case 2:
    case 0x10:
    case 0x1a:
    case 0x16:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x30:
    case 0x31:
    case 0x32:
        return true;
    default:
        return false;
    }
}
static unsigned priority(uint8_t t) {
    if (t == 0x20 || t == 0x21 || t == 0x17)
        return 3;
    if (t >= 0x11 && t <= 0x13)
        return 2;
    return 1;
}
int whale_receive(whale_rx_t *rx, const uint8_t *f, size_t n, uint32_t ms, whale_msg_t *out) {
    unsigned cap;
    if (n >= 8 && n <= WHALE_FRAME_LEGACY && f[0] == 2)
        cap = WHALE_FRAME_LEGACY - 8;
    else if (n >= 8 && f[0] == 3 && rx->frame > WHALE_FRAME_LEGACY && n <= rx->frame)
        cap = rx->frame - 8u;
    else
        return -1;
    bool ctl = whale_control(f[1]);
    whale_slot_t *s = &rx->slots[ctl];
    uint8_t *buf = ctl ? rx->control : rx->data;
    uint16_t id = w16(f + 2), len = w16(f + 6);
    unsigned i = f[4], count = f[5];
    if (!len || len > (ctl ? sizeof(rx->control) : sizeof(rx->data)) ||
        count != (len + cap - 1u) / cap || i >= count)
        return -1;
    size_t offset = i * cap, part = len - offset;
    if (part > cap)
        part = cap;
    if (n != part + 8)
        return -1;
    if (s->active && ms - s->last_ms >= 2000)
        s->active = false;
    if (!i && (!s->seen || id != s->id)) {
        uint16_t distance = (uint16_t)(id - s->id);
        if (s->seen && distance >= 0x8000)
            return distance == 0x8000 ? -1 : 0;
        if (s->active && (ctl || priority(f[1]) <= priority(s->type)))
            return -1;
        *s = (whale_slot_t){.id = id,
                            .type = f[1],
                            .parts = count,
                            .length = len,
                            .cap = cap,
                            .active = true,
                            .seen = true};
    }
    if (!s->active || id != s->id)
        return 0;
    if (f[1] != s->type || len != s->length || count != s->parts || cap != s->cap || i > s->next) {
        s->active = false;
        return -1;
    }
    if (i < s->next) {
        if (!memcmp(buf + offset, f + 8, part))
            return 0;
        s->active = false;
        return -1;
    }
    memcpy(buf + offset, f + 8, part);
    s->next++;
    s->last_ms = ms;
    if (s->next < count)
        return 0;
    s->active = false;
    *out = (whale_msg_t){f[1], buf, len};
    return 1;
}
uint16_t whale_frame_grant(uint32_t caps, uint16_t peer_rx, uint16_t peer_tx, uint16_t limit) {
    uint16_t frame = peer_rx < peer_tx ? peer_rx : peer_tx;
    if (limit < frame)
        frame = limit;
    if (frame > WHALE_FRAME_MAX)
        frame = WHALE_FRAME_MAX;
    return (caps & WHALE_CAP_LARGE_FRAMES) && frame > WHALE_FRAME_LEGACY ? frame
                                                                        : WHALE_FRAME_LEGACY;
}
bool whale_text(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n;) {
        uint8_t a = p[i++];
        if (a < 32 || a == 127)
            return false;
        if (a < 128)
            continue;
        unsigned k = a >= 0xc2 && a <= 0xdf   ? 1
                     : a >= 0xe0 && a <= 0xef ? 2
                     : a >= 0xf0 && a <= 0xf4 ? 3
                                              : 0;
        if (!k || i + k > n)
            return false;
        uint8_t b = p[i];
        if ((a == 0xe0 && b < 0xa0) || (a == 0xed && b >= 0xa0) || (a == 0xf0 && b < 0x90) ||
            (a == 0xf4 && b >= 0x90))
            return false;
        while (k--) {
            if ((p[i++] & 0xc0) != 0x80)
                return false;
        }
    }
    return true;
}
bool whale_parse_row(const uint8_t *p, size_t n, whale_row_t *row, uint32_t ms) {
    if (n < 24 || p[23] > 72 || n != 24u + p[23] || p[8] > 8 || p[9] > 7 || (p[10] & ~31) ||
        !w32(p) || !w32(p + 4) || w16(p + 15) > w16(p + 17) || !whale_text(p + 24, p[23]))
        return false;
    *row = (whale_row_t){.session = w32(p),
                         .run = w32(p + 4),
                         .state = p[8],
                         .tool = p[9],
                         .flags = p[10],
                         .elapsed = w32(p + 11),
                         .done = w16(p + 15),
                         .total = w16(p + 17),
                         .question = w32(p + 19),
                         .at = ms};
    memcpy(row->title, p + 24, p[23]);
    return true;
}
uint32_t whale_crc(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
    }
    return ~crc;
}