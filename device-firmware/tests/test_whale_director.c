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
    static const whale_page_t not_from[] = {LIST, COMPANION, WARDROBE, QUESTION, VOICE, PAIR,
                                            REPLY_READ, STANDBY, DESK};
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
    in.unread = 0;
    in.waiting = 1;
    assert(whale_director_summary(&in) == WHALE_GO_ATTENTION);
    in.global_valid = false;
    assert(whale_director_summary(&in) == WHALE_GO_STAY);
    in = base();
    in.view = DESK;
    assert(whale_director_trigger(&in, 0) == WHALE_GO_STAY);
    for (uint8_t t = 1; t <= 4; t++)
        assert(whale_director_trigger(&in, t) == WHALE_GO_ATTENTION);
    assert(whale_director_trigger(&in, 5) == WHALE_GO_FOCUS);
    for (int v = TASK_DETAIL; v <= STANDBY; v++) {
        if (v == DESK)
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
        if (v == COMPANION || v == WARDROBE || v == DESK)
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
    EV_COUNT
};
static const char *const event_names[EV_COUNT] = {
    "time", "key", "navigate", "link", "drop", "summary", "trigger",
    "voice", "audio", "batch", "companion", "rows", "old-bridge"};

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
    if (in->view == DESK || in->view == COMPANION || in->view == WARDROBE)
        CHECK(!s.busy, "background work keeps a resting page lit", in);
    if (in->view == PAIR || in->view == UNBINDING || in->voice_busy)
        CHECK(s.hold, "sleep may interrupt pairing, unbinding or voice", in);
    if (!whale_director_main_view(in->view))
        CHECK(s.busy, "an operation in progress lets the screen dim", in);
    /* Offline, nobody answers: after a pass no operation outlives the screen. */
    if (!in->linked && !s.hold && in->now - in->last_input >= WHALE_POWER_OFF_MS)
        CHECK(whale_director_main_view(in->view), "offline page outlives the screen-off time",
              in);
    /* An old bridge cannot prove idleness: the desk display never stays. */
    if (in->linked && in->ready && !in->global_cap)
        CHECK(in->view != DESK, "desk display stays on an old bridge", in);
}

static const whale_page_t pages[] = {
    TASK_DETAIL, LIST,     PAIR,      MANAGE,    KEEP,       REVOKE,     UNBINDING,    UNBIND_FAILED,
    LOADING,     LOAD_FAILED, QUESTION, PROMPT,  OPTION,     SENDING,    CHECKING,     UNSURE,
    RESULT,      VERSION,  LIST_LOAD, LIST_FAILED, REPLY_LOAD, REPLY_READ, REPLY_FAILED, VOICE,
    COMPANION,   SOUND,    HOME,      WARDROBE,  STANDBY};

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
            in.waiting = (uint16_t)(below(3) ? 0 : below(3));
            in.unread = (uint16_t)(below(3) ? 0 : below(3));
            {
                whale_director_in_t was = in;
                whale_go_t go = whale_director_summary(&in);
                carry_out(&in, go);
                if (was.view == DESK && was.global_valid && (was.waiting || was.unread))
                    CHECK(go == WHALE_GO_ATTENTION && in.view != DESK,
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
            if (was.view != DESK || !trigger)
                CHECK(go == WHALE_GO_STAY, "a trigger took over the user's page", &was);
            if (trigger >= 1 && trigger <= 4 && was.view == DESK)
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
    test_offline_and_screen();
    test_activity();
    test_fuzz();
    puts("Whale screen director tests PASS");
    return 0;
}
