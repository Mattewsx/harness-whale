#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
uint8_t whale_sound_load(void);
bool whale_sound_save(uint8_t mode);
/* Desk display: 0 = today's summary (default), 1 = plain clock. */
uint8_t whale_standby_load(void);
bool whale_standby_save(uint8_t mode);
/* Fixed-size blobs in the "whale" namespace; a size mismatch reads as absent. */
bool whale_blob_load(const char *key, void *data, size_t size);
bool whale_blob_save(const char *key, const void *data, size_t size);
