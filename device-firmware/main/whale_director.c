#include "whale_director.h"
#include "whale_power.h"

bool whale_director_main_view(whale_page_t view) {
    return view == HOME || view == TASK_DETAIL || view == LIST || view == COMPANION ||
           view == WARDROBE || view == DESK;
}

bool whale_director_idle(const whale_director_in_t *in) {
    return !in->linked ||
           (in->ready && in->global_cap && in->global_valid &&
            in->now - in->global_at < WHALE_SUMMARY_FRESH_MS && !in->running && !in->waiting &&
            !in->unread);
}

whale_go_t whale_director_desk(const whale_director_in_t *in) {
    if ((in->view == HOME || in->view == TASK_DETAIL) &&
        in->now - in->last_input >= WHALE_DESK_IDLE_MS && whale_director_idle(in) &&
        !in->voice_busy && !in->companion_action)
        return WHALE_GO_DESK;
    /* After an offline desk display reconnects to an old bridge, the cached
     * page cannot prove all sessions are idle. Return to the task home. */
    if (in->view == DESK && in->linked && in->ready && !in->global_cap)
        return WHALE_GO_HOME;
    return WHALE_GO_STAY;
}

whale_director_screen_t whale_director_screen(const whale_director_in_t *in) {
    whale_director_screen_t s;
    s.hold = in->voice_busy || in->audio_busy || in->cues_batching || in->view == PAIR ||
             in->view == UNBINDING;
    /* Rows are stale while offline; only a live task keeps the screen lit. */
    s.busy = !whale_director_main_view(in->view) || (in->linked && in->live_task);
    /* Background work may keep a task visible, but never keeps these pages lit. */
    if (in->view == COMPANION || in->view == WARDROBE || in->view == DESK)
        s.busy = false;
    return s;
}

whale_go_t whale_director_offline(const whale_director_in_t *in) {
    if (!in->linked && !whale_director_screen(in).hold && !whale_director_main_view(in->view) &&
        in->now - in->last_input >= WHALE_POWER_OFF_MS)
        return WHALE_GO_HOME;
    return WHALE_GO_STAY;
}

/* Work interrupts only the desk display; every other page belongs to the user. */
whale_go_t whale_director_summary(const whale_director_in_t *in) {
    if (in->view != DESK || !in->global_valid)
        return WHALE_GO_STAY;
    if (in->waiting || in->unread)
        return WHALE_GO_ATTENTION;
    return in->running ? WHALE_GO_FOCUS : WHALE_GO_STAY;
}

whale_go_t whale_director_trigger(const whale_director_in_t *in, uint8_t trigger) {
    if (in->view != DESK || !trigger)
        return WHALE_GO_STAY;
    return trigger == 5 ? WHALE_GO_FOCUS : WHALE_GO_ATTENTION;
}

whale_go_t whale_director_key(const whale_director_in_t *in) {
    return in->view == DESK ? WHALE_GO_HOME : WHALE_GO_STAY;
}

const uint8_t whale_activity_pool[WHALE_ACTIVITY_SETS][WHALE_ACTIVITY_CHOICES] = {
    {0, 1, 3, 6, 4, 7}, /* day */
    {2, 2, 2, 7, 7, 6}, /* night */
    {4, 4, 5, 5, 0, 1}, /* happy */
    {0, 5, 7, 0, 5, 7}, /* calm */
    {3, 4, 1, 3, 4, 1}  /* shy */
};

uint8_t whale_director_activity_set(bool night, uint8_t mood, uint32_t mood_age) {
    if (mood && mood_age < WHALE_MOOD_MS)
        return mood <= 2 ? 2 : mood == 3 ? 3 : 4;
    return night ? 1 : 0;
}

uint8_t whale_director_activity(uint8_t set, uint8_t current, uint8_t pick) {
    if (set >= WHALE_ACTIVITY_SETS)
        set = 0;
    for (unsigned i = 0; i < WHALE_ACTIVITY_CHOICES; i++) {
        uint8_t candidate = whale_activity_pool[set][(pick + i) % WHALE_ACTIVITY_CHOICES];
        if (candidate != current)
            return candidate;
    }
    return current;
}
