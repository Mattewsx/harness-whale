#include "../main/whale_battery_log.c"
#include <assert.h>
#include <stdio.h>

static whale_batt_result_t add_with_previous(whale_batt_log_t *log, whale_batt_log_t *previous,
                                           uint32_t t, int soc) {
    bool new_run = whale_batt_log_new_run(log, soc);
    if (new_run)
        *previous = *log;
    whale_batt_result_t result = whale_batt_log_add(log, t, soc, WHALE_BATT_SCREEN);
    assert(new_run == (result == WHALE_BATT_NEW_RUN));
    return result;
}

static void test_charging_clock_reset(int reboot_soc) {
    static whale_batt_log_t log, previous, discharge, persisted;
    whale_batt_log_reset(&log);
    whale_batt_log_reset(&previous);
    assert(add_with_previous(&log, &previous, 0, 100) == WHALE_BATT_ADDED);
    assert(add_with_previous(&log, &previous, 3600, 20) == WHALE_BATT_ADDED);
    discharge = log;
    assert(add_with_previous(&log, &previous, 3660, 25) == WHALE_BATT_NEW_RUN);
    assert(memcmp(&previous, &discharge, sizeof previous) == 0);

    /* Restore the persisted blob with a reset RTC and ordinary caller flags. */
    persisted = log;
    whale_batt_log_reset(&log);
    log = persisted;
    assert(add_with_previous(&log, &previous, 1, reboot_soc) == WHALE_BATT_ADDED);
    assert(log.count == 2 && log.e[1].t == 1 && log.e[1].soc == reboot_soc);
    assert(log.e[1].flags == (WHALE_BATT_SCREEN | WHALE_BATT_CHARGING));
    assert(add_with_previous(&log, &previous, 2, reboot_soc) == WHALE_BATT_SAME);
    for (int soc = reboot_soc + 1; soc <= 100; soc++) {
        assert(add_with_previous(&log, &previous, (uint32_t)soc * 60, soc) ==
               WHALE_BATT_ADDED);
        assert(memcmp(&previous, &discharge, sizeof previous) == 0);
    }
    assert(log.count == 2 && log.e[0].t == 3660 && log.e[1].soc == 100);
    assert(log.e[1].flags & WHALE_BATT_CHARGING);

    /* A real discharge ends charging; the next recharge replaces previous. */
    assert(add_with_previous(&log, &previous, 9600, 80) == WHALE_BATT_ADDED);
    assert(!(log.e[2].flags & WHALE_BATT_CHARGING));
    for (int soc = 81; soc < 85; soc++)
        assert(add_with_previous(&log, &previous, 9600 + (uint32_t)soc, soc) ==
               WHALE_BATT_ADDED);
    assert(memcmp(&previous, &discharge, sizeof previous) == 0);
    discharge = log;
    assert(add_with_previous(&log, &previous, 9700, 85) == WHALE_BATT_NEW_RUN);
    assert(memcmp(&previous, &discharge, sizeof previous) == 0);
    assert(previous.e[1].soc == 100 && previous.e[2].soc == 80);
    assert(log.count == 1 && (log.e[0].flags & WHALE_BATT_CHARGING));
}

int main(void) {
    test_charging_clock_reset(25);
    test_charging_clock_reset(30);
    static whale_batt_log_t log;
    whale_batt_log_reset(&log);
    assert(whale_batt_log_valid(&log) && log.count == 0);
    assert(whale_batt_log_add(&log, 10, -1, 0) == WHALE_BATT_SAME);
    assert(whale_batt_log_add(&log, 10, 101, 0) == WHALE_BATT_SAME && log.count == 0);
    assert(whale_batt_log_add(&log, 10, 98, WHALE_BATT_LINKED) == WHALE_BATT_ADDED);
    assert(whale_batt_log_add(&log, 40, 98, 0) == WHALE_BATT_SAME && log.count == 1);
    assert(whale_batt_log_add(&log, 70, 97, WHALE_BATT_TIMER) == WHALE_BATT_ADDED);
    /* Gauge jitter of a few points up is not a recharge. */
    assert(!whale_batt_log_new_run(&log, 101) && !whale_batt_log_new_run(&log, 100));
    assert(!whale_batt_log_new_run(&log, 97 + WHALE_BATT_RECHARGE - 1));
    assert(whale_batt_log_add(&log, 80, 99, 0) == WHALE_BATT_ADDED && log.count == 3);
    assert(log.e[1].t == 70 && log.e[1].soc == 97 && log.e[1].flags == WHALE_BATT_TIMER);
    whale_batt_log_reset(&log);
    assert(whale_batt_log_add(&log, 1, 20, 0) == WHALE_BATT_ADDED);
    assert(whale_batt_log_new_run(&log, 25));
    assert(whale_batt_log_add(&log, 2, 25, 0) == WHALE_BATT_NEW_RUN && log.count == 1);
    assert(log.e[0].soc == 25 && log.e[0].t == 2);
    /* Gradual recharge splits once, preserving the completed discharge even
     * through continued charging and a reboot (the marker is in the blob). */
    whale_batt_log_reset(&log);
    whale_batt_log_add(&log, 0, 100, 0);
    whale_batt_log_add(&log, 3600, 80, 0);
    unsigned splits = 0;
    static whale_batt_log_t previous;
    for (int soc = 81; soc <= 100; soc++) {
        if (whale_batt_log_new_run(&log, soc)) {
            previous = log;
            splits++;
        }
        whale_batt_log_add(&log, 3600 + (uint32_t)(soc - 80) * 60, soc, 0);
    }
    assert(splits == 1 && previous.e[0].soc == 100 && previous.e[1].soc == 80);
    assert(log.count == 1 && log.e[0].soc == 100 && log.e[0].t == 4800);
    assert(whale_batt_log_valid(&log));
    assert(whale_batt_log_add(&log, 8400, 80, 0) == WHALE_BATT_ADDED);
    assert(log.count == 2 && !(log.e[1].flags & WHALE_BATT_CHARGING));
    for (int soc = 81; soc < 85; soc++)
        assert(whale_batt_log_add(&log, 8400 + (uint32_t)soc, soc, 0) == WHALE_BATT_ADDED);
    assert(whale_batt_log_new_run(&log, 85));
    /* A full log keeps its start and moves the newest slot. */
    whale_batt_log_reset(&log);
    for (int i = 0; i < WHALE_BATT_LOG_SIZE + 10; i++)
        whale_batt_log_add(&log, (uint32_t)i, (i % 2) ? 50 : 51, 0);
    assert(log.count == WHALE_BATT_LOG_SIZE && log.e[0].t == 0);
    assert(log.e[WHALE_BATT_LOG_SIZE - 1].t == WHALE_BATT_LOG_SIZE + 9);
    /* A new low at capacity must survive the subsequent gradual recharge. */
    whale_batt_log_add(&log, 200, 20, 0);
    for (int soc = 21; soc < 25; soc++)
        assert(whale_batt_log_add(&log, 200 + (uint32_t)soc, soc, 0) == WHALE_BATT_ADDED);
    assert(whale_batt_log_add(&log, 230, 25, 0) == WHALE_BATT_NEW_RUN);
    /* Preserve a clock reset even if SOC is unchanged. */
    assert(whale_batt_log_add(&log, 1, 25, 0) == WHALE_BATT_ADDED);
    assert(log.count == 2 && log.e[1].t == 1);
    log.version = 0;
    assert(!whale_batt_log_valid(&log));
    log.version = WHALE_BATT_VERSION;
    log.count = WHALE_BATT_LOG_SIZE + 1;
    assert(!whale_batt_log_valid(&log));
    puts("Whale battery-life log: dedupe, recharge split and full log PASS");
    return 0;
}
