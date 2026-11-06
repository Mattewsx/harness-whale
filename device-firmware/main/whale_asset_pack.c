#include "whale_asset_pack.h"
#include "whale_wire.h"
#include <string.h>

bool whale_pack_parse(const uint8_t *p, size_t available, whale_pack_t *out) {
    if (!p || !out || available < WHALE_PACK_HEADER_BYTES || memcmp(p, "WHALEPAK", 8) ||
        w32(p + 8) != 1)
        return false;
    if (whale_crc(0, p, WHALE_PACK_HEADER_BYTES - 4) != w32(p + WHALE_PACK_HEADER_BYTES - 4))
        return false;
    uint32_t count = w32(p + 12), offset = w32(p + 16), size = w32(p + 20);
    if (!count || count > WHALE_PACK_MAX_ENTRIES || offset != WHALE_PACK_HEADER_BYTES ||
        !size || size > available - offset)
        return false;
    memset(out, 0, sizeof *out);
    out->body_offset = offset;
    out->body_size = size;
    out->count = count;
    memcpy(out->sha256, p + 24, 32);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = p + 56 + i * 24;
        whale_pack_entry_t *entry = &out->entries[i];
        if (!e[0] || memchr(e, 0, 16) == NULL)
            return false;
        memcpy(entry->name, e, 16);
        entry->offset = w32(e + 16);
        entry->size = w32(e + 20);
        if (!entry->size || entry->offset % 4 || entry->offset > size ||
            entry->size > size - entry->offset)
            return false;
    }
    return true;
}

const whale_pack_entry_t *whale_pack_find(const whale_pack_t *pack, const char *name,
                                          uint32_t size) {
    for (uint32_t i = 0; pack && i < pack->count; i++)
        if (!strcmp(pack->entries[i].name, name))
            return pack->entries[i].size == size ? &pack->entries[i] : NULL;
    return NULL;
}
