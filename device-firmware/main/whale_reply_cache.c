#include "whale_reply_cache.h"
#include <string.h>

/* Volatile bounded cache: reply text is never written into NVS. */
static struct {
    uint64_t epoch;
    uint32_t session, revision, part, count, used;
    char text[WHALE_REPLY_BYTES + 1];
} entries[WHALE_REPLY_CACHE_SLOTS];
static uint32_t tick;

void whale_reply_cache_clear(void) {
    memset(entries, 0, sizeof entries);
    tick = 0;
}
void whale_reply_cache_forget(uint32_t session, uint32_t keep_revision) {
    for (unsigned i = 0; i < WHALE_REPLY_CACHE_SLOTS; i++)
        if (entries[i].session == session && entries[i].revision != keep_revision)
            memset(&entries[i], 0, sizeof entries[i]);
}
bool whale_reply_cache_get(uint64_t epoch, uint32_t session, uint32_t revision, uint32_t part,
                           char *text, uint32_t *count) {
    for (unsigned i = 0; i < WHALE_REPLY_CACHE_SLOTS; i++) {
        if (entries[i].count && entries[i].epoch == epoch && entries[i].session == session &&
            entries[i].revision == revision && entries[i].part == part) {
            memcpy(text, entries[i].text, sizeof entries[i].text);
            *count = entries[i].count;
            entries[i].used = ++tick;
            return true;
        }
    }
    return false;
}
void whale_reply_cache_put(uint64_t epoch, uint32_t session, uint32_t revision, uint32_t part,
                           uint32_t count, const uint8_t *text, size_t size) {
    if (!revision || !count || part >= count || size > WHALE_REPLY_BYTES)
        return;
    unsigned target = 0;
    uint32_t oldest = 0;
    for (unsigned i = 0; i < WHALE_REPLY_CACHE_SLOTS; i++) {
        if (!entries[i].count || (entries[i].epoch == epoch && entries[i].session == session &&
                                  entries[i].revision == revision && entries[i].part == part)) {
            target = i;
            break;
        }
        uint32_t age = tick - entries[i].used;
        if (age >= oldest) {
            oldest = age;
            target = i;
        }
    }
    entries[target].epoch = epoch;
    entries[target].session = session;
    entries[target].revision = revision;
    entries[target].part = part;
    entries[target].count = count;
    entries[target].used = ++tick;
    memcpy(entries[target].text, text, size);
    entries[target].text[size] = 0;
}
