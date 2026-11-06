#include "whale_power.h"

void whale_power_init(whale_power_t *p, uint32_t now, bool quiet, uint32_t cycles) {
    *p = (whale_power_t){.boot_at = now,
                         .last_input = now,
                         .offline_since = now,
                         .fast_until = now + WHALE_POWER_FAST_ADV_MS,
                         .cycles = quiet ? cycles : 0,
                         .quiet = quiet,
                         .screen = quiet ? WHALE_SCREEN_OFF : WHALE_SCREEN_ON};
}

bool whale_power_input(whale_power_t *p, uint32_t now) {
    bool woke = p->screen != WHALE_SCREEN_ON;
    p->last_input = now;
    p->quiet = false;
    p->cycles = 0;
    p->screen = WHALE_SCREEN_ON;
    if (!p->linked) {
        /* Someone is at the device: let a waiting bridge find it quickly and
         * restart the offline countdown. */
        p->fast_until = now + WHALE_POWER_FAST_ADV_MS;
        p->offline_since = now;
    }
    return woke;
}

void whale_power_link(whale_power_t *p, uint32_t now, bool linked) {
    if (linked == p->linked)
        return;
    p->linked = linked;
    if (linked) {
        p->quiet = false;
        p->cycles = 0;
    } else {
        p->offline_since = now;
        p->fast_until = now + WHALE_POWER_FAST_ADV_MS;
    }
}

static bool reached(uint32_t now, uint32_t since, uint32_t span) { return now - since >= span; }

whale_power_plan_t whale_power_tick(whale_power_t *p, uint32_t now, bool busy, bool hold) {
    if (busy && p->quiet) /* e.g. a question arrived after a timer wake */
        p->quiet = false;
    if (busy)
        p->screen = WHALE_SCREEN_ON;
    else if (p->quiet || reached(now, p->last_input, WHALE_POWER_OFF_MS))
        p->screen = WHALE_SCREEN_OFF;
    else if (reached(now, p->last_input, WHALE_POWER_DIM_MS))
        p->screen = p->screen == WHALE_SCREEN_OFF ? WHALE_SCREEN_OFF : WHALE_SCREEN_DIM;
    p->slow_adv = !p->linked && (int32_t)(now - p->fast_until) >= 0;

    whale_power_plan_t plan = {.screen = p->screen, .slow_adv = p->slow_adv};
    if (!p->linked && !busy && !hold && p->screen == WHALE_SCREEN_OFF) {
        plan.sleep = p->quiet ? reached(now, p->boot_at, WHALE_POWER_WINDOW_MS)
                              : reached(now, p->offline_since, WHALE_POWER_OFFLINE_MS);
        if (plan.sleep && whale_power_next_cycles(p) <= WHALE_POWER_TIMER_CYCLES)
            plan.timer_ms = WHALE_POWER_TIMER_MS;
    }
    return plan;
}

uint32_t whale_power_next_cycles(const whale_power_t *p) {
    /* The first sleep after real use starts the count; each fruitless timer
     * wake adds one. */
    return p->quiet ? p->cycles + 1 : 1;
}

const char *whale_power_screen_name(uint8_t screen) {
    return screen == WHALE_SCREEN_ON ? "on" : screen == WHALE_SCREEN_DIM ? "dim" : "off";
}
