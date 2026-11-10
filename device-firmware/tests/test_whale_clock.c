#include "whale_clock.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static whale_local_time_t civil(int64_t s) {
    whale_local_time_t t;
    whale_clock_civil(s, &t);
    return t;
}

int main(void) {
    whale_local_time_t t = civil(0);
    assert(t.year == 1970 && t.month == 1 && t.day == 1 && t.wday == 4 && t.hour == 0);
    t = civil(-1); /* 1969-12-31 23:59:59, Wednesday */
    assert(t.year == 1969 && t.month == 12 && t.day == 31 && t.wday == 3 && t.hour == 23 &&
           t.minute == 59 && t.second == 59);
    t = civil(951782400); /* 2000-02-29, Tuesday (century leap year) */
    assert(t.year == 2000 && t.month == 2 && t.day == 29 && t.wday == 2);
    t = civil(1793284200); /* 2026-10-29 14:30:00 UTC, Thursday */
    assert(t.year == 2026 && t.month == 10 && t.day == 29 && t.wday == 4 && t.hour == 14 &&
           t.minute == 30 && t.second == 0);
    t = civil(4102444799LL); /* 2099-12-31 23:59:59 */
    assert(t.year == 2099 && t.month == 12 && t.day == 31);

    whale_clock_t c = {0};
    assert(!whale_clock_valid(&c) && !whale_clock_local(&c, 10, &t));
    assert(!whale_clock_sync(&c, 10, 1000, 0)); /* before 2020 */
    assert(!whale_clock_sync(&c, 10, 1793284200, 841));
    assert(!whale_clock_sync(&c, 10, WHALE_CLOCK_MAX_UNIX, 0));
    assert(!whale_clock_valid(&c));
    /* Synced at RTC second 100 to 2026-11-06 22:59:30 in UTC+8. */
    assert(whale_clock_sync(&c, 100, 1793977170, 480));
    assert(whale_clock_valid(&c) && whale_clock_local(&c, 100, &t));
    assert(t.year == 2026 && t.month == 11 && t.day == 6 && t.wday == 5 && t.hour == 22 &&
           t.minute == 59 && t.second == 30 && !whale_clock_night(&t));
    /* The RTC keeps counting (also through deep sleep): 30 s later is 23:00. */
    assert(whale_clock_local(&c, 130, &t) && t.hour == 23 && t.minute == 0 &&
           whale_clock_night(&t));
    /* Eight hours later it is 07:00 the next morning, a Saturday: day again. */
    assert(whale_clock_local(&c, 130 + 8 * 3600, &t) && t.day == 7 && t.wday == 6 &&
           t.hour == 7 && !whale_clock_night(&t));
    assert(whale_clock_local(&c, 130 + 8 * 3600 - 1, &t) && t.hour == 6 &&
           whale_clock_night(&t));
    /* The summary work day starts at 05:00: 04:59 still belongs to Friday. */
    uint32_t day, work, later;
    assert(whale_clock_day(&c, 130, &day) && whale_clock_work_day(&c, 130, &work) && work == day);
    assert(whale_clock_work_day(&c, 130 + 6 * 3600 - 1, &later) && later == day);
    assert(whale_clock_work_day(&c, 130 + 6 * 3600, &later) && later == day + 1);
    /* Negative offsets and a resync replace the previous reference. */
    assert(whale_clock_sync(&c, 5000, 1793977170, -300));
    assert(whale_clock_local(&c, 5000, &t) && t.hour == 9 && t.minute == 59 && t.day == 6);
    c.tz_min = 900; /* corrupted RTC memory */
    assert(!whale_clock_valid(&c) && !whale_clock_work_day(&c, 130, &work));
    puts("Desk clock civil time, RTC carry-over and night window and work day PASS");
    return 0;
}
