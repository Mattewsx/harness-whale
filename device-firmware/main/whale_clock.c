#include "whale_clock.h"

bool whale_clock_valid(const whale_clock_t *c) {
    return c->magic == WHALE_CLOCK_MAGIC && c->tz_min >= -WHALE_CLOCK_MAX_TZ_MIN &&
           c->tz_min <= WHALE_CLOCK_MAX_TZ_MIN;
}

bool whale_clock_sync(whale_clock_t *c, uint32_t rtc_s, int64_t unix_s, int tz_min) {
    if (unix_s < WHALE_CLOCK_MIN_UNIX || unix_s >= WHALE_CLOCK_MAX_UNIX ||
        tz_min < -WHALE_CLOCK_MAX_TZ_MIN || tz_min > WHALE_CLOCK_MAX_TZ_MIN)
        return false;
    c->offset = unix_s - (int64_t)rtc_s;
    c->tz_min = (int16_t)tz_min;
    c->magic = WHALE_CLOCK_MAGIC;
    return true;
}

static int64_t floor_div(int64_t a, int64_t b) {
    return a / b - (a % b != 0 && (a < 0) != (b < 0));
}

void whale_clock_civil(int64_t local_s, whale_local_time_t *out) {
    int64_t days = floor_div(local_s, 86400);
    int64_t secs = local_s - days * 86400;
    out->hour = (uint8_t)(secs / 3600);
    out->minute = (uint8_t)(secs / 60 % 60);
    out->second = (uint8_t)(secs % 60);
    out->wday = (uint8_t)(((days % 7) + 11) % 7); /* 1970-01-01 was a Thursday */
    /* Howard Hinnant's days-to-civil algorithm. */
    int64_t z = days + 719468;
    int64_t era = floor_div(z, 146097);
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    out->year = (uint16_t)(yoe + era * 400 + (m <= 2));
    out->month = (uint8_t)m;
    out->day = (uint8_t)d;
}

bool whale_clock_local(const whale_clock_t *c, uint32_t rtc_s, whale_local_time_t *out) {
    if (!whale_clock_valid(c))
        return false;
    whale_clock_civil((int64_t)rtc_s + c->offset + (int64_t)c->tz_min * 60, out);
    return true;
}

bool whale_clock_night(const whale_local_time_t *t) {
    return t->hour >= WHALE_NIGHT_START_HOUR || t->hour < WHALE_NIGHT_END_HOUR;
}
