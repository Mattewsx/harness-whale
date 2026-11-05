#pragma once
#include "whale_ble.h"
#include "whale_voice.h"

typedef enum {
    HOME,
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
    VOICE
} whale_page_t;
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
    bool voice_cap, voice_submitted, attention_cap;
    uint8_t voice_phase, voice_choice, voice_identity[24];
    uint32_t voice_session, voice_at, voice_ms;
    char voice_title[73], voice_text[1801];
    whale_page_t view;
} whale_app_t;
void whale_ui_init(void);
void whale_ui_draw(const whale_app_t *app);
#define HOME_REPLY_LINES 6
int whale_scroll_max(const char *s);
int whale_home_scroll_max(const char *s);
size_t whale_line_offset(const char *s, int line);
int whale_scroll_at(const char *s, size_t offset);
bool whale_text_wide(const char *s);
bool whale_displayable(const char *str);
