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
    return soc >= 0 && soc <= 100 && log->count &&
           soc >= log->e[log->count - 1].soc + WHALE_BATT_RECHARGE;
}

whale_batt_result_t whale_batt_log_add(whale_batt_log_t *log, uint32_t t, int soc, uint8_t flags) {
    if (soc < 0 || soc > 100)
        return WHALE_BATT_SAME;
    whale_batt_result_t result = WHALE_BATT_ADDED;
    if (whale_batt_log_new_run(log, soc)) {
        whale_batt_log_reset(log);
        result = WHALE_BATT_NEW_RUN;
    } else if (log->count && log->e[log->count - 1].soc == soc) {
        return WHALE_BATT_SAME;
    }
    if (log->count == WHALE_BATT_LOG_SIZE) {
        /* Keep the start of the run; the newest slot tracks the end point. */
        log->count--;
    }
    log->e[log->count++] = (whale_batt_entry_t){.t = t, .soc = (uint8_t)soc, .flags = flags};
    return result;
}
