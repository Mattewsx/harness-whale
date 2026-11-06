#include "../main/whale_battery_log.c"
#include <assert.h>
#include <stdio.h>

int main(void) {
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
    /* A full log keeps its start and moves the newest slot. */
    whale_batt_log_reset(&log);
    for (int i = 0; i < WHALE_BATT_LOG_SIZE + 10; i++)
        whale_batt_log_add(&log, (uint32_t)i, (i % 2) ? 50 : 51, 0);
    assert(log.count == WHALE_BATT_LOG_SIZE && log.e[0].t == 0);
    assert(log.e[WHALE_BATT_LOG_SIZE - 1].t == WHALE_BATT_LOG_SIZE + 9);
    log.version = 0;
    assert(!whale_batt_log_valid(&log));
    log.version = WHALE_BATT_VERSION;
    log.count = WHALE_BATT_LOG_SIZE + 1;
    assert(!whale_batt_log_valid(&log));
    puts("Whale battery-life log: dedupe, recharge split and full log PASS");
    return 0;
}
