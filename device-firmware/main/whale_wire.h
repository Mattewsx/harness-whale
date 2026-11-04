#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline uint16_t w16(const uint8_t *p) {
    return p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t w32(const uint8_t *p) {
    return w16(p) | ((uint32_t)w16(p + 2) << 16);
}
static inline uint64_t w64(const uint8_t *p) {
    return w32(p) | ((uint64_t)w32(p + 4) << 32);
}
static inline void put16(uint8_t *p, uint16_t n) {
    p[0] = n;
    p[1] = n >> 8;
}
static inline void put32(uint8_t *p, uint32_t n) {
    put16(p, n);
    put16(p + 2, n >> 16);
}
static inline void put64(uint8_t *p, uint64_t n) {
    put32(p, n);
    put32(p + 4, n >> 32);
}

typedef struct {
    uint16_t id, length, used;
    uint8_t type, parts, next;
    bool active, seen;
    uint32_t last_ms;
} whale_slot_t;
typedef struct {
    whale_slot_t slots[2];
    uint8_t data[2048], control[96];
} whale_rx_t;
typedef struct {
    uint32_t session, run, elapsed, question, at;
    uint16_t done, total;
    uint8_t state, tool, flags, hint;
    char title[73];
} whale_row_t;
typedef struct {
    uint8_t type;
    const uint8_t *bytes;
    size_t size;
} whale_msg_t;
bool whale_control(uint8_t type);
int whale_receive(whale_rx_t *rx, const uint8_t *frame, size_t n, uint32_t ms, whale_msg_t *out);
bool whale_parse_row(const uint8_t *p, size_t n, whale_row_t *row, uint32_t ms);
bool whale_text(const uint8_t *p, size_t n);
uint32_t whale_crc(uint32_t crc, const uint8_t *p, size_t n);
