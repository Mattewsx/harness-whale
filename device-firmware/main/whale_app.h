#pragma once
#include "whale_ble.h"
#include "whale_clock.h"
#include "whale_director.h"
#include "whale_voice.h"

/* Display slots map to persistent skin IDs. This permutation is its own inverse. */
static const uint8_t whale_wardrobe_order[] = {0, 2, 1, 3};

typedef struct {
    whale_row_t rows[8];
    struct {
        uint32_t session, revision;
        char preview[81];
    } replies[8];
    uint32_t read_session, read_revision, read_page, read_count;
    uint32_t read_first, read_started;
    uint16_t read_sizes[3];
    uint8_t read_used;
    bool read_back, read_pending, read_error;
    char answer[3 * 384 + 1];
    uint64_t epoch, balance;
    uint32_t cursor, next, seq, passkey, recovered, soc_at, page_at, now;
    uint16_t total;
    uint8_t count, selected, focus, page, pages, options, all_options, result, currency, freshness;
    bool linked, ready, meta, low_power, seen_battery, soc_stale, unsupported;
    int soc, scroll, battery_level;
    uint8_t identity[20], received, parts;
    uint16_t option_ids[3];
    char prompt[513], labels[3][257];
    uint8_t question_mode; /* WHALE_MODE_*: single choice, multi-select or approval */
    uint64_t picked;       /* multi-select: chosen option ids as bits */
    bool voice_cap, voice_submitted, attention_cap;
    /* Spoken new task: voice_new targets a fresh session instead of the selected one. */
    bool voice_task_cap, voice_new;
    uint8_t voice_phase, voice_choice, voice_identity[24];
    uint32_t voice_session, voice_at, voice_ms;
    char voice_title[73], voice_text[1801];
    bool global_cap, global_valid;
    bool cache_invalidation;
    uint64_t cache_sequence;
    uint16_t global_running, global_waiting, global_unread;
    uint32_t global_at, global_seq, global_focus;
    uint32_t companion_at, action_at;
    uint8_t companion_choice, companion_action, sound_mode, menu_choice, sound_choice;
    int8_t sound_saved;
    uint32_t title_session, title_run;
    char full_title[513];
    bool list_back;
    /* Focus on the "语音新任务" row above the first session (first window only). */
    bool list_new;
    uint8_t dock, outfit_choice, bond, unlocked, worn, activity, mood;
    /* Ephemeral invitations never earn affection until the user confirms an action. */
    uint8_t invitation, invitation_variant, invitation_last;
    bool invitation_settling, invitation_scheduled, invitation_cooling, companion_invited;
    uint32_t invitation_at, invitation_due, invitation_next;
    bool back_focus, defer_question, locked_notice;
    uint8_t unlock_new; /* skins just unlocked, shown once on the companion pages */
    uint32_t activity_at, mood_at;
    uint8_t bond_today; /* bond gained on bond_day, capped by WHALE_BOND_DAILY_MAX */
    uint8_t bond_undated; /* part of bond_today earned without a valid clock */
    uint8_t bond_taps, bond_taps_undated; /* independent, persisted interaction quota */
    uint32_t bond_day;  /* latest credited local day, zero until first sync */
    whale_page_t view;
    /* Desk clock: local time from the bridge sync or the chip's RTC. */
    bool clock_valid, night;
    whale_local_time_t clock;
    /* Desk display mode (0 = today's summary, 1 = plain clock) and the
     * bridge's daily totals; stale once the local work day moves on. */
    uint8_t standby_mode, standby_choice;
    bool summary_valid, summary_stale;
    uint16_t summary_done;
    uint32_t summary_day, summary_seconds;
} whale_app_t;
/* Skin bits: apprentice witch at 20, long-sleeved pajamas at 40, loungewear at 60. */
uint8_t whale_unlocks_for(uint8_t bond);
/* The voice new-task row sits above the first session of the first window. */
static inline bool whale_list_has_new(const whale_app_t *app) {
    return app->voice_cap && app->cursor == 0;
}
void whale_ui_init(void);
void whale_ui_draw(const whale_app_t *app);
#define HOME_REPLY_LINES 6
int whale_scroll_max(const char *s);
int whale_home_scroll_max(const char *s);
size_t whale_line_offset(const char *s, int line);
int whale_scroll_at(const char *s, size_t offset);
bool whale_text_wide(const char *s);
bool whale_displayable(const char *str);
