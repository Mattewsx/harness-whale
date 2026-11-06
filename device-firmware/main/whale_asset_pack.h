#pragma once
/* Parser for the asset pack in the "assets" data partition.
 * Format: tools/make_whale_asset_pack.py. No ESP-IDF dependency (host tested). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WHALE_PACK_HEADER_BYTES 0x100u
#define WHALE_PACK_MAX_ENTRIES 8u

typedef struct {
    char name[16];
    uint32_t offset, size;
} whale_pack_entry_t;

typedef struct {
    uint32_t body_offset, body_size, count;
    uint8_t sha256[32];
    whale_pack_entry_t entries[WHALE_PACK_MAX_ENTRIES];
} whale_pack_t;

/* Validates magic, format, header CRC and entry bounds against `available`
 * bytes of mapped partition. The body hash is not recomputed here. */
bool whale_pack_parse(const uint8_t *p, size_t available, whale_pack_t *out);
/* Returns the body-relative entry or NULL when missing or of the wrong size. */
const whale_pack_entry_t *whale_pack_find(const whale_pack_t *pack, const char *name,
                                          uint32_t size);
