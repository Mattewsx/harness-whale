#pragma once
/* Whale screen director: decides which page owns the screen and when.
 *
 * Pure logic (no ESP-IDF, LVGL or BLE) so host tests and the seeded fuzz test
 * can drive it. whale_app.c gathers a snapshot, asks the director, and carries
 * out the answer (opening pages, sending snapshot requests, cancelling voice).
 *
 * Who owns the screen, highest first (docs/DIRECTOR.md has the full table):
 *   1. work that needs the user (questions, approvals, unread replies)
 *   2. voice (recording, uploading, playing a cue)
 *   3. the page the user is reading or operating
 *   4. companion interaction
 *   5. everyday idle animation
 *   6. desk display (today's summary or clock)
 */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TASK_DETAIL,
    LIST,
    PAIR,
    MANAGE,
    KEEP,
    REVOKE,
    UNBINDING,
    UNBIND_FAILED,
    LOADING,
    LOAD_FAILED,
    QUESTION,
    PROMPT,
    OPTION,
    SENDING,
    CHECKING,
    UNSURE,
    RESULT,
    VERSION,
    LIST_LOAD,
    LIST_FAILED,
    REPLY_LOAD,
    REPLY_READ,
    REPLY_FAILED,
    VOICE,
    COMPANION,
    SOUND,
    HOME,
    WARDROBE,
    DESK, /* desk display: clock, whale and today's summary, entered when idle */
    STANDBY /* settings: choose the desk display (summary or plain clock) */
} whale_page_t;

/* Idle time on the home or task page before the desk display takes over. */
#ifndef WHALE_DESK_IDLE_MS
#define WHALE_DESK_IDLE_MS 120000u
#endif
/* A global summary older than this cannot prove that every session is idle. */
#define WHALE_SUMMARY_FRESH_MS 30000u
/* Pending queue: the result page shows this long before the next question. */
#define WHALE_QUEUE_NEXT_MS 1500u
/* The home page pops a new question only after the user left it alone this long. */
#define WHALE_QUEUE_IDLE_MS 10000u
/* Feelings: a question left unanswered this long makes the whale anxious,
 * then very anxious; a finished session is celebrated for a few seconds. */
#define WHALE_ANXIOUS_MS 120000u
#define WHALE_VERY_ANXIOUS_MS 300000u
#define WHALE_CELEBRATE_MS 4000u
/* The whale's mood steers the idle animation for this long. */
#define WHALE_MOOD_MS 300000u

typedef enum {
    WHALE_GO_STAY,      /* keep the current page */
    WHALE_GO_HOME,      /* task home page */
    WHALE_GO_DESK,      /* desk display */
    WHALE_GO_ATTENTION, /* list opened at the highest-priority items */
    WHALE_GO_FOCUS,     /* the session that just started work */
    WHALE_GO_QUEUE      /* the oldest question the device can answer, else the list */
} whale_go_t;

typedef enum {
    WHALE_FEEL_CALM,
    WHALE_FEEL_CELEBRATE,
    WHALE_FEEL_ANXIOUS,
    WHALE_FEEL_VERY_ANXIOUS
} whale_feel_t;

typedef struct {
    whale_page_t view;
    uint32_t now, last_input;
    uint32_t page_at; /* when the current page opened */
    bool answered;    /* result page for an answer the computer accepted */
    bool linked, ready;
    /* Global summary (0x50) from a bridge with capability bit 11. */
    bool global_cap, global_valid;
    uint32_t global_at;
    uint16_t running, waiting, unread;
    bool voice_busy;       /* recording or uploading */
    bool audio_busy;       /* a cue or reply is playing */
    bool cues_batching;    /* reminder cues are still being collected */
    bool companion_action; /* a companion reaction is still on screen */
    bool live_task;        /* a cached row is running or has a question */
    bool invitation;       /* the whale is inviting the user to interact */
    bool cheer;            /* a session finished at cheer_at */
    uint32_t cheer_at;
    uint32_t waiting_at;   /* when the waiting count last rose from zero */
} whale_director_in_t;

typedef struct {
    bool busy; /* something wants the screen lit */
    bool hold; /* something must not be interrupted by sleep */
} whale_director_screen_t;

/* Pages the user can rest on; any other page is an operation in progress. */
bool whale_director_main_view(whale_page_t view);
/* Every session is verifiably idle (or the computer is gone). */
bool whale_director_idle(const whale_director_in_t *in);
/* Periodic desk rule: enter it after idling, leave it for an old bridge. */
whale_go_t whale_director_desk(const whale_director_in_t *in);
/* Periodic offline rule: nobody answers an offline page, so return home once
 * the screen would turn off. Pairing, unbinding, voice and cues finish first. */
whale_go_t whale_director_offline(const whale_director_in_t *in);
/* A global summary (0x50) arrived; in already holds its counts. */
whale_go_t whale_director_summary(const whale_director_in_t *in);
/* A live trigger (0x51, nonzero) arrived: 1..4 are cues, 5 is work started. */
whale_go_t whale_director_trigger(const whale_director_in_t *in, uint8_t trigger);
/* A key press. The desk display always yields to the user. */
whale_go_t whale_director_key(const whale_director_in_t *in);
/* Pending queue. Questions wait in arrival order on the computer; the device
 * shows one at a time. The confirm key on an accepted result opens the next. */
bool whale_director_more_waiting(const whale_director_in_t *in);
/* Periodic: an accepted result moves on to the next question by itself. */
whale_go_t whale_director_queue(const whale_director_in_t *in);
/* Confirm on the result page: next question if one waits, else stay. */
whale_go_t whale_director_result_key(const whale_director_in_t *in);
/* Screen and sleep wishes passed on to the power policy. */
whale_director_screen_t whale_director_screen(const whale_director_in_t *in);

/* What the whale shows on the home page and above the list. Celebration is
 * short and always ends; anxiety counts from the later of the question
 * arriving and the last key press, so it never nags someone who is busy
 * with the device. Calm on every other page and during voice, a companion
 * reaction or an invitation. */
whale_feel_t whale_director_feel(const whale_director_in_t *in);

/* Idle animation on the home page. mood: 0 none, 1..2 happy, 3 calm, 4+ shy. */
#define WHALE_ACTIVITY_SETS 5u
#define WHALE_ACTIVITY_CHOICES 6u
extern const uint8_t whale_activity_pool[WHALE_ACTIVITY_SETS][WHALE_ACTIVITY_CHOICES];
uint8_t whale_director_activity_set(bool night, uint8_t mood, uint32_t mood_age);
/* Next activity from the set, starting at a random pick and avoiding a repeat. */
uint8_t whale_director_activity(uint8_t set, uint8_t current, uint8_t pick);
