#pragma once
/* Whale-mode power policy: screen dim/off, advertising cadence and offline
 * sleep. Pure logic (no ESP-IDF/LVGL) so the host tests can drive it. */
#include <stdbool.h>
#include <stdint.h>

#ifdef WHALE_POWER_TEST
/* Short thresholds for bench runs: build with -DWHALE_POWER_TEST=1. */
#define WHALE_POWER_DIM_MS 10000u
#define WHALE_POWER_OFF_MS 20000u
#define WHALE_POWER_FAST_ADV_MS 10000u
#define WHALE_POWER_OFFLINE_MS 60000u
#define WHALE_POWER_WINDOW_MS 30000u
#define WHALE_POWER_TIMER_MS 60000u
#define WHALE_POWER_TIMER_CYCLES 3u
#else
#define WHALE_POWER_DIM_MS 120000u
#define WHALE_POWER_OFF_MS 300000u
/* Fast advertising after boot, disconnect or a key press while offline. */
#define WHALE_POWER_FAST_ADV_MS 30000u
/* Continuously offline this long (and screen off) before deep sleep. */
#define WHALE_POWER_OFFLINE_MS 600000u
/* After a timer wake: stay discoverable this long. Longer than one bridge
 * retry cycle (8 s scan + at most 60 s backoff) so a waiting bridge finds it. */
#define WHALE_POWER_WINDOW_MS 75000u
#define WHALE_POWER_TIMER_MS 600000u
/* About two hours of periodic wakes; after that only a key wakes the device. */
#define WHALE_POWER_TIMER_CYCLES 12u
#endif

/* Advertising intervals in 0.625 ms units. */
#define WHALE_ADV_FAST_MIN 48u   /* 30 ms */
#define WHALE_ADV_FAST_MAX 96u   /* 60 ms */
#define WHALE_ADV_SLOW_MIN 1636u /* 1022.5 ms */
#define WHALE_ADV_SLOW_MAX 2056u /* 1285 ms */

enum { WHALE_SCREEN_ON, WHALE_SCREEN_DIM, WHALE_SCREEN_OFF };

typedef struct {
    uint32_t boot_at, last_input, offline_since, fast_until;
    uint32_t cycles; /* consecutive timer wakes that found no computer */
    bool linked, quiet;
    uint8_t screen;
    bool slow_adv;
} whale_power_t;

typedef struct {
    uint8_t screen;
    bool slow_adv;
    bool sleep;
    uint32_t timer_ms; /* deep-sleep timer wake; 0 = key wake only */
} whale_power_plan_t;

/* quiet: this boot is a timer wake from offline sleep; the screen stays off
 * and the device sleeps again unless a key or the computer shows up. */
void whale_power_init(whale_power_t *p, uint32_t now, bool quiet, uint32_t cycles);
/* Any key press. Returns true when the press only woke the screen. */
bool whale_power_input(whale_power_t *p, uint32_t now);
void whale_power_link(whale_power_t *p, uint32_t now, bool linked);
/* busy: something wants the screen lit (running task, question, open page).
 * hold: something must not be interrupted by sleep (voice, audio, pairing). */
whale_power_plan_t whale_power_tick(whale_power_t *p, uint32_t now, bool busy, bool hold);
/* Cycle count to keep across the next sleep. */
uint32_t whale_power_next_cycles(const whale_power_t *p);
const char *whale_power_screen_name(uint8_t screen);
