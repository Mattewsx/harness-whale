#include "whale_battery_log.h"
#include <string.h>

void whale_batt_log_reset(whale_batt_log_t *log) {
    memset(log, 0, sizeof *log);
    log->version = WHALE_BATT_VERSION;
}

bool whale_batt_log_valid(const whale_batt_log_t *log) {
    return log->version == WHALE_BATT_VERSION && log->count <= WHALE_BATT_LOG_SIZE;
}

bool whale_batt_log_new_run(const whale_batt_log_t *log, int soc) {
    if (soc < 0 || soc > 100 || !whale_batt_log_valid(log) || !log->count ||
        (log->e[log->count - 1].flags & WHALE_BATT_CHARGING))
        return false;
    int low = log->e[0].soc;
    for (unsigned i = 1; i < log->count; i++) {
        /* Start at the latest charging peak or independent clock segment. */
        if ((log->e[i].flags & WHALE_BATT_CHARGING) || log->e[i].t < log->e[i - 1].t ||
            log->e[i].soc < low)
            low = log->e[i].soc;
    }
    return soc >= low + WHALE_BATT_RECHARGE;
}

whale_batt_result_t whale_batt_log_add(whale_batt_log_t *log, uint32_t t, int soc, uint8_t flags) {
    if (soc < 0 || soc > 100)
        return WHALE_BATT_SAME;
    whale_batt_result_t result = WHALE_BATT_ADDED;
    if (whale_batt_log_new_run(log, soc)) {
        whale_batt_log_reset(log);
        result = WHALE_BATT_NEW_RUN;
        flags |= WHALE_BATT_CHARGING;
    } else if (log->count && log->e[log->count - 1].soc == soc &&
               t >= log->e[log->count - 1].t) {
        return WHALE_BATT_SAME;
    } else if (log->count && (log->e[log->count - 1].flags & WHALE_BATT_CHARGING) &&
               soc >= log->e[log->count - 1].soc) {
        /* Keep clock resets, but carry charging through them. Within each
         * clock segment only the latest charging peak is needed. */
        if (t >= log->e[log->count - 1].t)
            log->count--;
        flags |= WHALE_BATT_CHARGING;
    }
    if (log->count == WHALE_BATT_LOG_SIZE) {
        /* Keep the start and recent samples, including the recharge trough. */
        memmove(log->e + 1, log->e + 2, (WHALE_BATT_LOG_SIZE - 2) * sizeof log->e[0]);
        log->count--;
    }
    log->e[log->count++] = (whale_batt_entry_t){.t = t, .soc = (uint8_t)soc, .flags = flags};
    return result;
}
