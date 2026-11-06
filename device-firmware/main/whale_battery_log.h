#pragma once
/* Battery-life log kept in NVS so a discharge run can be read back over USB
 * afterwards (USB power would charge the cell and spoil the measurement).
 * One entry per SOC change; a jump up of WHALE_BATT_RECHARGE points starts a
 * new run; the app keeps the finished run as the previous one. */
#include <stdbool.h>
#include <stdint.h>

#define WHALE_BATT_LOG_SIZE 128
#define WHALE_BATT_RECHARGE 5
#define WHALE_BATT_VERSION 1

enum {
    WHALE_BATT_LINKED = 1,   /* computer connected */
    WHALE_BATT_SCREEN = 2,   /* backlight on or dimmed */
    WHALE_BATT_TIMER = 4,    /* sampled during a periodic offline wake */
    WHALE_BATT_SLOW_ADV = 8, /* long advertising interval */
};

typedef struct {
    uint32_t t; /* RTC seconds; keeps counting through deep sleep */
    uint8_t soc, flags;
} whale_batt_entry_t;

typedef struct {
    uint8_t version;
    uint16_t count;
    whale_batt_entry_t e[WHALE_BATT_LOG_SIZE];
} whale_batt_log_t;

typedef enum { WHALE_BATT_SAME, WHALE_BATT_ADDED, WHALE_BATT_NEW_RUN } whale_batt_result_t;

void whale_batt_log_reset(whale_batt_log_t *log);
/* Loaded blobs from another version or with a bad count are discarded. */
bool whale_batt_log_valid(const whale_batt_log_t *log);
/* True when soc would start a new run: save the old one first if wanted. */
bool whale_batt_log_new_run(const whale_batt_log_t *log, int soc);
whale_batt_result_t whale_batt_log_add(whale_batt_log_t *log, uint32_t t, int soc, uint8_t flags);
