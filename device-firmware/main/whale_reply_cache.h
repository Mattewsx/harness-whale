#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WHALE_REPLY_BYTES 384
#define WHALE_REPLY_CACHE_SLOTS 32
void whale_reply_cache_clear(void);
void whale_reply_cache_forget(uint32_t session, uint32_t keep_revision);
bool whale_reply_cache_get(uint64_t epoch, uint32_t session, uint32_t revision, uint32_t part,
                           char *text, uint32_t *count);
void whale_reply_cache_put(uint64_t epoch, uint32_t session, uint32_t revision, uint32_t part,
                           uint32_t count, const uint8_t *text, size_t size);
