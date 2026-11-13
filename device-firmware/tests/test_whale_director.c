/* Screen director: table tests for every rule, then a seeded fuzz test that
 * drives random event sequences through a small model of whale_app.c and
 * checks the invariants listed in docs/DIRECTOR.md after every step.
 *
 * Reproduce a failure: WHALE_FUZZ_SEED=<seed> ./test_whale_director
 * Longer runs:         WHALE_FUZZ_SEEDS=5000 WHALE_FUZZ_STEPS=20000 ./test_whale_director
 */
#include "whale_director.h"
#include "whale_power.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static whale_director_in_t base(void) {
    whale_director_in_t in;
    memset(&in, 0, sizeof in);
    in.view = HOME;
    in.now = 1000000;
    in.last_input = in.now - WHALE_DESK_IDLE_MS;
    in.linked = in.ready = in.global_cap = in.global_valid = true;
    in.global_at = in.now - 1000;
    return in;
}

static void test_idle(void) {
    whale_director_in_t in = base();
    assert(whale_director_idle(&in));
    in.running = 1;
    assert(!whale_director_idle(&in));
    in = base();
    in.waiting = 1;
    assert(!whale_director_idle(&in));
    in = base();
    in.unread = 1;
    assert(!whale_director_idle(&in));
    in = base();
    in.global_at = in.now - WHALE_SUMMARY_FRESH_MS; /* stale summary proves nothing */
    assert(!whale_director_idle(&in));
    in = base();
    in.global_cap = false; /* old bridge */
    assert(!whale_director_idle(&in));
    in.linked = false; /* offline: nothing can need the user */
    assert(whale_director_idle(&in));
}

static void test_desk(void) {
    whale_director_in_t in = base();
    assert(whale_director_desk(&in) == WHALE_GO_DESK);
    in.view = TASK_DETAIL;
    assert(whale_director_desk(&in) == WHALE_GO_DESK);
    static const whale_page_t not_from[] = {LIST, COMPANION, WARDROBE, BADGES, QUESTION, VOICE,
                                            PAIR, REPLY_READ, STANDBY, DESK};
    for (unsigned i = 0; i < sizeof not_from / sizeof *not_from; i++) {
        in.view = not_from[i];
        assert(whale_director_desk(&in) == WHALE_GO_STAY);
    }
    in = base();
    in.last_input++;
    assert(whale_director_desk(&in) == WHALE_GO_STAY);
    in = base();
    in.voice_busy = true;
    assert(whale_director_desk(&in) == WHALE_GO_STAY);
    in = base();
    in.companion_action = true;
    assert(whale_director_desk(&in) == WHALE_GO_STAY);
    in = base();
    in.waiting = 1;
    assert(whale_director_desk(&in) == WHALE_GO_STAY);
    /* Offline, an idle home page still becomes the desk display. */
    in = base();
    in.linked = in.ready = in.global_cap = in.global_valid = false;
    assert(whale_director_desk(&in) == WHALE_GO_DESK);
    /* Reconnected to an old bridge: leave. Before it is ready: stay. */
    in = base();
    in.view = DESK;
    in.global_cap = false;
    assert(whale_director_desk(&in) == WHALE_GO_HOME);
    in.ready = false;
    assert(whale_director_desk(&in) == WHALE_GO_STAY);
}

static void test_work_interrupts_only_the_desk(void) {
    whale_director_in_t in = base();
    in.view = DESK;
    assert(whale_director_summary(&in) == WHALE_GO_STAY);
    in.running = 1;
    assert(whale_director_summary(&in) == WHALE_GO_FOCUS);
    in.unread = 1;
    assert(whale_director_summary(&in) == WHALE_GO_ATTENTION);
    in.waiting = 1; /* a waiting question opens the queue */
    assert(whale_director_summary(&in) == WHALE_GO_QUEUE);
    in.unread = 0;
    assert(whale_director_summary(&in) == WHALE_GO_QUEUE);
    in.global_valid = false;
    assert(whale_director_summary(&in) == WHALE_GO_STAY);
    in = base();
    in.view = DESK;
    assert(whale_director_trigger(&in, 0) == WHALE_GO_STAY);
    assert(whale_director_trigger(&in, 1) == WHALE_GO_QUEUE);
    for (uint8_t t = 2; t <= 4; t++)
        assert(whale_director_trigger(&in, t) == WHALE_GO_ATTENTION);
    assert(whale_director_trigger(&in, 5) == WHALE_GO_FOCUS);
    for (int v = TASK_DETAIL; v <= STANDBY; v++) {
        if (v == DESK || v == HOME)
            continue;
        in = base();
        in.view = (whale_page_t)v;
        in.waiting = in.running = 1;
        assert(whale_director_summary(&in) == WHALE_GO_STAY);
        assert(whale_director_trigger(&in, 1) == WHALE_GO_STAY);
        assert(whale_director_trigger(&in, 5) == WHALE_GO_STAY);
        assert(whale_director_key(&in) == WHALE_GO_STAY);
    }
    in = base();
    in.view = DESK;
    assert(whale_director_key(&in) == WHALE_GO_HOME);
}

static whale_director_in_t answered(void) {
    whale_director_in_t in = base();
    in.view = RESULT;
    in.answered = true;
    in.page_at = in.now - WHALE_QUEUE_NEXT_MS;
    in.global_at = in.page_at + 200; /* summary after the answer */
    in.waiting = 2;
    in.last_input = in.page_at;
    return in;
}

static void test_queue(void) {
    /* A new question pops up on a home page left alone, not on one in use. */
    whale_director_in_t in = base();
    in.last_input = in.now - WHALE_QUEUE_IDLE_MS;
    assert(whale_director_trigger(&in, 1) == WHALE_GO_QUEUE);
    for (uint8_t t = 2; t <= 5; t++)
        assert(whale_director_trigger(&in, t) == WHALE_GO_STAY);
    in.last_input++;
    assert(whale_director_trigger(&in, 1) == WHALE_GO_STAY);
    in.last_input--;
    in.voice_busy = true;
    assert(whale_director_trigger(&in, 1) == WHALE_GO_STAY);
    in.voice_busy = false;
    in.companion_action = true;
    assert(whale_director_trigger(&in, 1) == WHALE_GO_STAY);

    /* After an accepted answer, move on to the next question by itself. */
    in = answered();
    assert(whale_director_more_waiting(&in));
    assert(whale_director_queue(&in) == WHALE_GO_QUEUE);
    assert(whale_director_result_key(&in) == WHALE_GO_QUEUE);
    in.page_at++; /* the result shows for WHALE_QUEUE_NEXT_MS first */
    in.global_at++;
    assert(whale_director_queue(&in) == WHALE_GO_STAY);
    assert(whale_director_result_key(&in) == WHALE_GO_QUEUE); /* confirm skips the wait */
    in = answered();
    in.global_at = in.page_at - 1; /* an older summary may still count this answer */
    assert(!whale_director_more_waiting(&in));
    assert(whale_director_queue(&in) == WHALE_GO_STAY);
    assert(whale_director_result_key(&in) == WHALE_GO_STAY);
    in = answered();
    in.waiting = 0;
    assert(whale_director_queue(&in) == WHALE_GO_STAY);
    in = answered();
    in.answered = false; /* withdrawn, expired or handled on the computer */
    assert(whale_director_queue(&in) == WHALE_GO_STAY);
    in = answered();
    in.voice_busy = true;
    assert(whale_director_queue(&in) == WHALE_GO_STAY);
    in = answered();
    in.linked = false;
    assert(whale_director_queue(&in) == WHALE_GO_STAY);
    in = answered();
    in.global_at = in.now - WHALE_SUMMARY_FRESH_MS;
    in.page_at = in.global_at - 1;
    assert(whale_director_queue(&in) == WHALE_GO_STAY);
    in = answered();
    in.view = QUESTION;
    assert(whale_director_queue(&in) == WHALE_GO_STAY);
}

static void test_feel(void) {
    /* Anxious: a question left alone, counted from the later of its arrival
     * and the last key press. */
    whale_director_in_t in = base();
    in.waiting = 1;
    in.waiting_at = in.now - WHALE_ANXIOUS_MS;
    in.last_input = in.now - WHALE_ANXIOUS_MS;
    assert(whale_director_feel(&in) == WHALE_FEEL_ANXIOUS);
    in.view = LIST;
    assert(whale_director_feel(&in) == WHALE_FEEL_ANXIOUS);
    in.view = HOME;
    in.waiting_at++;
    assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    in.waiting_at--;
    in.last_input++; /* a recent key press: the user is busy with the device */
    assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    in.waiting_at = in.last_input = in.now - WHALE_VERY_ANXIOUS_MS;
    assert(whale_director_feel(&in) == WHALE_FEEL_VERY_ANXIOUS);
    in.last_input++;
    assert(whale_director_feel(&in) == WHALE_FEEL_ANXIOUS);
    in.last_input--;
    in.waiting = 0; /* nothing waits: never anxious */
    assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    in.waiting = 1;
    in.linked = false;
    assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    in.linked = true;
    in.global_valid = false;
    assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    in.global_valid = true;
    in.global_at = in.now - WHALE_SUMMARY_FRESH_MS;
    assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    in.global_at = in.now;
    assert(whale_director_feel(&in) == WHALE_FEEL_VERY_ANXIOUS);
    /* Calm wherever the whale is not on show, and while she is occupied. */
    static const whale_page_t elsewhere[] = {TASK_DETAIL, QUESTION, REPLY_READ, VOICE, COMPANION,
                                             WARDROBE, BADGES, DESK, RESULT, PAIR};
    for (unsigned i = 0; i < sizeof elsewhere / sizeof *elsewhere; i++) {
        in.view = elsewhere[i];
        assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    }
    in.view = HOME;
    in.voice_busy = true;
    assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    in.voice_busy = false;
    in.companion_action = true;
    assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    in.companion_action = false;
    in.invitation = true;
    assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    in.invitation = false;
    /* Celebrate: short, ends by itself, and comes before anxiety. */
    in.cheer = true;
    in.cheer_at = in.now;
    assert(whale_director_feel(&in) == WHALE_FEEL_CELEBRATE);
    in.cheer_at = in.now - WHALE_CELEBRATE_MS + 1;
    assert(whale_director_feel(&in) == WHALE_FEEL_CELEBRATE);
    in.cheer_at--;
    assert(whale_director_feel(&in) == WHALE_FEEL_VERY_ANXIOUS);
    in.waiting = 0;
    assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    in = base();
    in.linked = false; /* a celebration already started finishes offline */
    in.cheer = true;
    in.cheer_at = in.now - 1000;
    assert(whale_director_feel(&in) == WHALE_FEEL_CELEBRATE);

    /* Badge unlock celebration waits for a quiet home page, then runs 4 s. */
    in = base();
    in.badge_pending = true;
    in.view = QUESTION;
    assert(!whale_director_badge_start(&in));
    in.view = LIST;
    assert(!whale_director_badge_start(&in));
    in.view = HOME;
    in.voice_busy = true;
    assert(!whale_director_badge_start(&in));
    in.voice_busy = false;
    in.companion_action = true;
    assert(!whale_director_badge_start(&in));
    in.companion_action = false;
    in.invitation = true;
    assert(!whale_director_badge_start(&in));
    in.invitation = false;
    assert(whale_director_badge_start(&in));
    assert(whale_director_desk(&in) == WHALE_GO_STAY);
    in.badge_pending = false;
    in.badge_cheer = true;
    in.badge_cheer_at = in.now;
    assert(whale_director_feel(&in) == WHALE_FEEL_CELEBRATE);
    assert(whale_director_desk(&in) == WHALE_GO_STAY);
    in.badge_cheer_at = in.now - WHALE_CELEBRATE_MS;
    assert(whale_director_feel(&in) == WHALE_FEEL_CALM);
    assert(whale_director_desk(&in) == WHALE_GO_DESK);
}

static void test_offline_and_screen(void) {
    whale_director_in_t in = base();
    in.linked = false;
    in.view = QUESTION;
    in.last_input = in.now - WHALE_POWER_OFF_MS;
    assert(whale_director_offline(&in) == WHALE_GO_HOME);
    in.last_input++;
    assert(whale_director_offline(&in) == WHALE_GO_STAY);
    in.last_input--;
    in.view = PAIR;
    assert(whale_director_offline(&in) == WHALE_GO_STAY);
    in.view = UNBINDING;
    assert(whale_director_offline(&in) == WHALE_GO_STAY);
    in.view = VOICE;
    assert(whale_director_offline(&in) == WHALE_GO_HOME);
    in.voice_busy = true;
    assert(whale_director_offline(&in) == WHALE_GO_STAY);
    in.voice_busy = false;
    in.audio_busy = true;
    assert(whale_director_offline(&in) == WHALE_GO_STAY);
    in.audio_busy = false;
    in.cues_batching = true;
    assert(whale_director_offline(&in) == WHALE_GO_STAY);
    in.cues_batching = false;
    in.view = LIST;
    assert(whale_director_offline(&in) == WHALE_GO_STAY);
    in.view = QUESTION;
    in.linked = true;
    assert(whale_director_offline(&in) == WHALE_GO_STAY);

    in = base();
    whale_director_screen_t s = whale_director_screen(&in);
    assert(!s.busy && !s.hold);
    in.live_task = true;
    assert(whale_director_screen(&in).busy);
    in.linked = false; /* stale rows never keep the screen lit */
    assert(!whale_director_screen(&in).busy);
    in = base();
    in.view = QUESTION;
    assert(whale_director_screen(&in).busy);
    in.live_task = true;
    for (whale_page_t v = COMPANION; v <= DESK; v++) {
        in.view = v;
        if (v == COMPANION || v == WARDROBE || v == BADGES || v == DESK)
            assert(!whale_director_screen(&in).busy);
    }
    in = base();
    in.view = PAIR;
    assert(whale_director_screen(&in).hold);
}

static void test_activity(void) {
    assert(whale_director_activity_set(false, 0, 0) == 0);
    assert(whale_director_activity_set(true, 0, 0) == 1);
    assert(whale_director_activity_set(true, 1, 0) == 2);
    assert(whale_director_activity_set(false, 2, WHALE_MOOD_MS - 1) == 2);
    assert(whale_director_activity_set(false, 3, 0) == 3);
    assert(whale_director_activity_set(false, 4, 0) == 4);
    assert(whale_director_activity_set(true, 3, WHALE_MOOD_MS) == 1);
    for (uint8_t set = 0; set < WHALE_ACTIVITY_SETS; set++)
        for (unsigned current = 0; current < 8; current++)
            for (unsigned pick = 0; pick < 256; pick++) {
                uint8_t next = whale_director_activity(set, (uint8_t)current, (uint8_t)pick);
                bool in_pool = false;
                for (unsigned i = 0; i < WHALE_ACTIVITY_CHOICES; i++)
                    in_pool |= whale_activity_pool[set][i] == next;
                assert(in_pool && next != current);
            }
}

/* ---- Seeded fuzz ------------------------------------------------------- */

static uint64_t rng;
static uint32_t next_random(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 32);
}
static uint32_t below(uint32_t n) { return next_random() % n; }

enum {
    EV_TIME,
    EV_KEY,
    EV_NAVIGATE,
    EV_LINK,
    EV_DROP,
    EV_SUMMARY,
    EV_TRIGGER,
    EV_VOICE,
    EV_AUDIO,
    EV_BATCH,
    EV_COMPANION,
    EV_ROWS,
    EV_OLD_BRIDGE,
    EV_CHEER,
    EV_BADGE,
    EV_INVITE,
    EV_COUNT
};
static const char *const event_names[EV_COUNT] = {
    "time",  "key",   "navigate",  "link", "drop",       "summary", "trigger", "voice",
    "audio", "batch", "companion", "rows", "old-bridge", "cheer",   "badge",   "invite"};

#define TRACE 48u
static struct {
    uint8_t kind;
    uint32_t arg;
    whale_page_t view;
} trace[TRACE];
static unsigned traced;
static uint64_t current_seed;
static unsigned current_step;

static void fail(const char *why, const whale_director_in_t *in) {
    fprintf(stderr, "director fuzz: %s\n  seed=%llu step=%u view=%d linked=%d ready=%d cap=%d"
                    " valid=%d run/wait/unread=%u/%u/%u voice=%d audio=%d batch=%d action=%d\n"
                    "  reproduce: WHALE_FUZZ_SEED=%llu ./test_whale_director\n  last events:\n",
            why, (unsigned long long)current_seed, current_step, in->view, in->linked, in->ready,
            in->global_cap, in->global_valid, in->running, in->waiting, in->unread,
            in->voice_busy, in->audio_busy, in->cues_batching, in->companion_action,
            (unsigned long long)current_seed);
    unsigned n = traced < TRACE ? traced : TRACE;
    for (unsigned i = traced - n; i < traced; i++)
        fprintf(stderr, "    #%u %-10s arg=%u -> view %d\n", i, event_names[trace[i % TRACE].kind],
                trace[i % TRACE].arg, trace[i % TRACE].view);
    abort();
}
#define CHECK(cond, why, in)                                                                       \
    do {                                                                                           \
        if (!(cond))                                                                               \
            fail(why, in);                                                                         \
    } while (0)

/* What whale_app.c does with each answer. */
static void carry_out(whale_director_in_t *in, whale_go_t go) {
    switch (go) {
    case WHALE_GO_HOME:
        if (in->view == VOICE)
            in->voice_busy = false; /* voice_cancel() */
        in->view = HOME;
        break;
    case WHALE_GO_DESK:
        in->view = DESK;
        break;
    case WHALE_GO_ATTENTION:
        in->view = in->ready && in->linked ? LIST_LOAD : LIST;
        break;
    case WHALE_GO_FOCUS:
        in->view = below(2) ? TASK_DETAIL : LIST_LOAD;
        break;
    case WHALE_GO_QUEUE:
        /* Oldest answerable question, or the list, or home when none. */
        in->view = below(3) == 0 ? QUESTION : below(2) ? LIST : HOME;
        in->answered = false;
        in->page_at = in->now;
        break;
    default:
        break;
    }
}

/* One main-loop pass: companion_tick() then power_step(). */
static void loop_pass(whale_director_in_t *in) {
    whale_director_in_t before = *in;
    whale_go_t desk = whale_director_desk(in);
    CHECK(desk == whale_director_desk(in), "desk rule is not deterministic", in);
    if (desk == WHALE_GO_DESK) {
        CHECK(before.view == HOME || before.view == TASK_DETAIL,
              "desk display entered from a page the user is operating", &before);
        CHECK(before.now - before.last_input >= WHALE_DESK_IDLE_MS,
              "desk display entered before the idle time", &before);
        CHECK(whale_director_idle(&before), "desk display entered while work needs the screen",
              &before);
        CHECK(!before.voice_busy, "desk display interrupted voice", &before);
        CHECK(!before.companion_action, "desk display interrupted a companion reaction",
              &before);
        CHECK(!(before.view == HOME &&
                (before.badge_pending ||
                 (before.badge_cheer && before.now - before.badge_cheer_at < WHALE_CELEBRATE_MS))),
              "desk display interrupted a badge celebration", &before);
    }
    if (desk == WHALE_GO_STAY) {
        whale_go_t next = whale_director_queue(in);
        if (next == WHALE_GO_QUEUE) {
            CHECK(before.view == RESULT && before.answered,
                  "the queue moved on from a page that is not an accepted answer", &before);
            CHECK(before.now - before.page_at >= WHALE_QUEUE_NEXT_MS,
                  "the queue hid the result before it was shown", &before);
            CHECK(!before.voice_busy, "the queue interrupted voice", &before);
            CHECK(before.waiting && (int32_t)(before.global_at - before.page_at) >= 0,
                  "the queue moved on without a newer summary showing more questions",
                  &before);
        }
        desk = next;
    }
    carry_out(in, desk);
    /* No flapping: a page the director just chose is stable on the next pass. */
    if (desk != WHALE_GO_STAY)
        CHECK(whale_director_desk(in) == WHALE_GO_STAY, "desk rule flaps within one pass", in);

    whale_director_in_t power = *in;
    bool hold = whale_director_screen(&power).hold;
    whale_go_t off = whale_director_offline(&power);
    if (off == WHALE_GO_HOME) {
        CHECK(!power.linked, "offline rule moved a linked device", &power);
        CHECK(!hold, "offline rule interrupted voice, audio, pairing or unbinding", &power);
        CHECK(!whale_director_main_view(power.view), "offline rule left a resting page", &power);
        CHECK(power.now - power.last_input >= WHALE_POWER_OFF_MS,
              "offline rule fired before the screen-off time", &power);
    }
    carry_out(in, off);
    whale_director_screen_t s = whale_director_screen(in);
    if (in->view == DESK || in->view == COMPANION || in->view == WARDROBE || in->view == BADGES)
        CHECK(!s.busy, "background work keeps a resting page lit", in);
    if (in->view == PAIR || in->view == UNBINDING || in->voice_busy)
        CHECK(s.hold, "sleep may interrupt pairing, unbinding or voice", in);
    if (!whale_director_main_view(in->view))
        CHECK(s.busy, "an operation in progress lets the screen dim", in);
    /* Offline, nobody answers: after a pass no operation outlives the screen. */
    if (!in->linked && !s.hold && in->now - in->last_input >= WHALE_POWER_OFF_MS)
        CHECK(whale_director_main_view(in->view), "offline page outlives the screen-off time",
              in);
    /* Feelings. The app drops an ended cheer and starts queued badge cheers on quiet home. */
    if (in->cheer && in->now - in->cheer_at >= WHALE_CELEBRATE_MS)
        in->cheer = false;
    if (in->badge_cheer && in->now - in->badge_cheer_at >= WHALE_CELEBRATE_MS)
        in->badge_cheer = false;
    if (whale_director_badge_start(in)) {
        CHECK(in->view == HOME && !in->voice_busy && !in->companion_action && !in->invitation,
              "badge celebration started outside a quiet home page", in);
        in->badge_pending = false;
        in->badge_cheer = true;
        in->badge_cheer_at = in->now;
    }
    whale_feel_t feel = whale_director_feel(in);
    CHECK(feel == whale_director_feel(in), "feeling is not deterministic", in);
    if (feel != WHALE_FEEL_CALM) {
        CHECK(in->view == HOME || in->view == LIST, "a feeling shows on a page without the whale",
              in);
        CHECK(!in->voice_busy && !in->companion_action && !in->invitation,
              "a feeling interrupts voice, a reaction or an invitation", in);
    }
    if (feel == WHALE_FEEL_CELEBRATE)
        CHECK((in->cheer && in->now - in->cheer_at < WHALE_CELEBRATE_MS) ||
                  (in->badge_cheer && in->now - in->badge_cheer_at < WHALE_CELEBRATE_MS),
              "a celebration with nothing finished, or one that does not end", in);
    if (feel == WHALE_FEEL_ANXIOUS || feel == WHALE_FEEL_VERY_ANXIOUS) {
        uint32_t limit = feel == WHALE_FEEL_ANXIOUS ? WHALE_ANXIOUS_MS : WHALE_VERY_ANXIOUS_MS;
        CHECK(in->waiting && in->linked && in->ready && in->global_valid,
              "anxious with nothing verifiably waiting", in);
        CHECK(in->now - in->waiting_at >= limit, "anxious before the question waited long enough",
              in);
        CHECK(in->now - in->last_input >= limit, "anxious right after a key press", in);
    }
    /* An old bridge cannot prove idleness: the desk display never stays. */
    if (in->linked && in->ready && !in->global_cap)
        CHECK(in->view != DESK, "desk display stays on an old bridge", in);
}

static const whale_page_t pages[] = {
    TASK_DETAIL, LIST,     PAIR,      MANAGE,    KEEP,       REVOKE,     UNBINDING,    UNBIND_FAILED,
    LOADING,     LOAD_FAILED, QUESTION, PROMPT,  OPTION,     SENDING,    CHECKING,     UNSURE,
    RESULT,      VERSION,  LIST_LOAD, LIST_FAILED, REPLY_LOAD, REPLY_READ, REPLY_FAILED, VOICE,
    COMPANION,   SOUND,    HOME,      WARDROBE,  BADGES,     STANDBY};

static void fuzz_one(uint64_t seed, unsigned steps) {
    current_seed = seed;
    rng = seed * 0x9e3779b97f4a7c15ull + 1;
    traced = 0;
    whale_director_in_t in = base();
    in.now = below(1u << 30);
    in.last_input = in.now;
    in.global_at = in.now;
    for (current_step = 0; current_step < steps; current_step++) {
        uint8_t kind = (uint8_t)below(EV_COUNT);
        uint32_t arg = next_random();
        switch (kind) {
        case EV_TIME: {
            static const uint32_t jumps[] = {125, 1000, 9000, 30000, WHALE_DESK_IDLE_MS,
                                             WHALE_POWER_OFF_MS, WHALE_POWER_OFFLINE_MS};
            uint32_t j = jumps[arg % 7];
            in.now += below(2) ? j : (j ? below(j) + 1 : 1);
            break;
        }
        case EV_KEY: {
            if (in.view == RESULT && below(2)) {
                /* Confirm on the result page. */
                whale_go_t next = whale_director_result_key(&in);
                CHECK(next == WHALE_GO_STAY || (in.answered && in.waiting),
                      "confirm opened the queue with nothing waiting", &in);
                in.last_input = in.now;
                if (next == WHALE_GO_QUEUE)
                    carry_out(&in, next);
                else {
                    in.view = TASK_DETAIL;
                    in.page_at = in.now;
                }
                break;
            }
            whale_go_t go = whale_director_key(&in);
            CHECK(go == (in.view == DESK ? WHALE_GO_HOME : WHALE_GO_STAY),
                  "a key press did not reach the user's page", &in);
            in.last_input = in.now;
            carry_out(&in, go);
            CHECK(in.view != DESK, "the desk display survived a key press", &in);
            break;
        }
        case EV_NAVIGATE:
            /* The user opened some page; the desk display is left only by keys. */
            if (in.view != DESK) {
                in.view = pages[arg % (sizeof pages / sizeof *pages)];
                in.last_input = in.now;
                in.page_at = in.now;
                in.answered = in.view == RESULT && below(2);
                in.voice_busy = in.view == VOICE && below(2);
            }
            break;
        case EV_LINK:
            in.linked = true;
            in.ready = below(4) != 0;
            in.global_cap = below(6) != 0;
            break;
        case EV_DROP:
            in.linked = in.ready = false;
            in.global_valid = false;
            break;
        case EV_SUMMARY:
            if (!in.linked || !in.global_cap)
                break;
            in.global_valid = below(8) != 0;
            in.global_at = in.now;
            in.running = (uint16_t)(below(3) ? 0 : below(4));
            {
                uint16_t waiting = (uint16_t)(below(3) ? 0 : below(3));
                if (waiting && !(in.waiting && in.global_valid))
                    in.waiting_at = in.now; /* the app times the wait from here */
                in.waiting = waiting;
            }
            in.unread = (uint16_t)(below(3) ? 0 : below(3));
            {
                whale_director_in_t was = in;
                whale_go_t go = whale_director_summary(&in);
                carry_out(&in, go);
                if (was.view == DESK && was.global_valid && (was.waiting || was.unread))
                    CHECK((go == WHALE_GO_QUEUE) == (was.waiting != 0) && in.view != DESK,
                          "work that needs the user left the desk display up", &was);
                if (was.view != DESK)
                    CHECK(go == WHALE_GO_STAY, "a summary took over the user's page", &was);
            }
            break;
        case EV_TRIGGER: {
            if (!in.linked || !in.global_cap)
                break;
            uint8_t trigger = (uint8_t)below(6);
            whale_director_in_t was = in;
            whale_go_t go = whale_director_trigger(&in, trigger);
            carry_out(&in, go);
            if (was.view == DESK && trigger)
                CHECK(in.view != DESK, "a live trigger left the desk display up", &was);
            if (go == WHALE_GO_QUEUE && was.view != DESK)
                CHECK(was.view == HOME && trigger == 1 &&
                          was.now - was.last_input >= WHALE_QUEUE_IDLE_MS && !was.voice_busy &&
                          !was.companion_action,
                      "a question popped over a page in use", &was);
            else if (was.view != DESK || !trigger)
                CHECK(go == WHALE_GO_STAY, "a trigger took over the user's page", &was);
            if (trigger == 1 && was.view == DESK)
                CHECK(go == WHALE_GO_QUEUE, "a question did not open the queue", &was);
            if (trigger >= 2 && trigger <= 4 && was.view == DESK)
                CHECK(go == WHALE_GO_ATTENTION, "a cue did not open the attention list", &was);
            break;
        }
        case EV_VOICE:
            in.voice_busy = !in.voice_busy;
            break;
        case EV_AUDIO:
            in.audio_busy = !in.audio_busy;
            break;
        case EV_BATCH:
            in.cues_batching = !in.cues_batching;
            break;
        case EV_COMPANION:
            in.companion_action = !in.companion_action;
            break;
        case EV_ROWS:
            in.live_task = !in.live_task;
            break;
        case EV_OLD_BRIDGE:
            if (in.linked)
                in.global_cap = false;
            break;
        case EV_CHEER:
            in.cheer = true;
            in.cheer_at = in.now;
            break;
        case EV_BADGE:
            in.badge_pending = true;
            break;
        case EV_INVITE:
            in.invitation = !in.invitation;
            break;
        }
        trace[traced % TRACE].kind = kind;
        trace[traced % TRACE].arg = arg;
        loop_pass(&in);
        trace[traced % TRACE].view = in.view;
        traced++;
    }
}

static unsigned env_number(const char *name, unsigned fallback) {
    const char *s = getenv(name);
    return s && *s ? (unsigned)strtoul(s, NULL, 10) : fallback;
}

static void test_fuzz(void) {
    unsigned steps = env_number("WHALE_FUZZ_STEPS", 4000);
    const char *one = getenv("WHALE_FUZZ_SEED");
    if (one && *one) {
        fuzz_one(strtoull(one, NULL, 10), steps);
        printf("Director fuzz seed %s x %u steps PASS\n", one, steps);
        return;
    }
    unsigned seeds = env_number("WHALE_FUZZ_SEEDS", 300);
    for (unsigned s = 1; s <= seeds; s++)
        fuzz_one(s, steps);
    printf("Director fuzz %u seeds x %u steps PASS\n", seeds, steps);
}

int main(void) {
    test_idle();
    test_desk();
    test_work_interrupts_only_the_desk();
    test_queue();
    test_feel();
    test_offline_and_screen();
    test_activity();
    test_fuzz();
    puts("Whale screen director tests PASS");
    return 0;
}
