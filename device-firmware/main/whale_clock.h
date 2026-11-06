#pragma once
/* Desk-display wall clock. The bridge sends Unix time and the computer's UTC
 * offset; between syncs the chip's RTC seconds (which keep counting through
 * deep sleep) carry the time. Pure logic so host tests can drive it. */
#include <stdbool.h>
#include <stdint.h>

/* Local hours shown as the dark night clock: [START, 24) and [0, END). */
#define WHALE_NIGHT_START_HOUR 23
#define WHALE_NIGHT_END_HOUR 7
/* Accept only plausible times: 2020-01-01 .. 2100-01-01 UTC, UTC-14..+14. */
#define WHALE_CLOCK_MIN_UNIX 1577836800LL
#define WHALE_CLOCK_MAX_UNIX 4102444800LL
#define WHALE_CLOCK_MAX_TZ_MIN 840

typedef struct {
    uint32_t magic;
    int64_t offset; /* Unix seconds minus RTC seconds at the last sync */
    int16_t tz_min; /* local minus UTC, minutes */
} whale_clock_t;

typedef struct {
    uint16_t year;
    uint8_t month, day, wday; /* wday: 0 = Sunday */
    uint8_t hour, minute, second;
} whale_local_time_t;

#define WHALE_CLOCK_MAGIC 0x57434C4BUL

bool whale_clock_valid(const whale_clock_t *c);
/* Returns false (clock unchanged) for an implausible time or offset. */
bool whale_clock_sync(whale_clock_t *c, uint32_t rtc_s, int64_t unix_s, int tz_min);
bool whale_clock_local(const whale_clock_t *c, uint32_t rtc_s, whale_local_time_t *out);
void whale_clock_civil(int64_t local_s, whale_local_time_t *out);
bool whale_clock_night(const whale_local_time_t *t);
