#include "whale_app.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "whale_audio.h"
#include "whale_battery_log.h"
#include "whale_cues.h"
#include "whale_power.h"
#include "whale_reply_cache.h"
#include "whale_settings.h"
#include "whale_sleep.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static whale_app_t a = {.soc = -1, .next = UINT32_MAX};
static whale_rx_t rx;
static QueueHandle_t events;
static QueueHandle_t cues;
static whale_cues_t reminders;
static uint32_t disconnected_at, seek_session, title_requested_at;
static bool disconnect_pending;
static bool seek_to_list, priority_list;
static char title_prefix[73];
/* Cue-queue command for the peripheral task (1..6 are sounds and settings). */
#define WHALE_CMD_SLEEP 7
/* Serial-only health line for the hardware stress script. No session data. */
#define WHALE_DIAG_MS 30000
static volatile unsigned periph_stack_min;
static whale_power_t power;
static whale_sleep_info_t boot_info;
/* Shared with the peripheral task: power state for the battery log and the
 * parameters of a pending sleep request. */
static volatile uint8_t power_flags;
static volatile uint32_t sleep_timer_ms, sleep_cycles;
static bool sleep_requested;
static uint32_t sleep_retry_at, key_guard_until;
/* Idle this long on the task home (or companion) with no work: desk display. */
#define WHALE_DESK_IDLE_MS 120000u
static uint32_t rtc_seconds(void);
static void clock_step(void);
static unsigned stack_headroom(const char *task) {
    TaskHandle_t handle = xTaskGetHandle(task);
    return handle ? (unsigned)uxTaskGetStackHighWaterMark(handle) : 0;
}
static void log_diag(const char *why) {
    unsigned main_stack = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    unsigned lvgl_stack = stack_headroom("taskLVGL");
    unsigned ble_stack = stack_headroom("nimble_host");
    ESP_LOGI("whale_diag",
             "why=%s heap_free=%lu heap_min=%lu stack_main=%u stack_periph=%u "
             "stack_lvgl=%u stack_ble=%u",
             why, (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size(), main_stack, periph_stack_min,
             lvgl_stack, ble_stack);
    ESP_LOGI("whale_power",
             "why=%s boot=%s sleeps=%lu cycles=%lu screen=%s adv=%s linked=%d "
             "offline_ms=%lu idle_ms=%lu soc=%d",
             why, whale_sleep_boot_name(boot_info.boot), (unsigned long)boot_info.sleeps,
             (unsigned long)power.cycles, whale_power_screen_name(power.screen),
             power.slow_adv ? "slow" : "fast", (int)a.linked,
             (unsigned long)(a.linked ? 0 : a.now - power.offline_since),
             (unsigned long)(a.now - power.last_input), a.soc);
    (void)why;
    (void)main_stack;
    (void)lvgl_stack;
    (void)ble_stack;
}
static bool queue_cue(uint8_t kind) {
    whale_audio_cmd_t cmd = {kind, a.sound_mode, whale_audio_generation()};
    return cues && xQueueSend(cues, &cmd, 0) == pdTRUE;
}
static struct {
    uint32_t session, run;
} sounded[16];
static unsigned sounded_next;
static struct {
    uint8_t body[17];
    uint64_t epoch;
    bool used;
} pending_reads[16];
static unsigned pending_read_next;
static uint32_t read_retry_at;
static void flush_reads(void) {
    if (!a.ready || !a.linked)
        return;
    for (unsigned i = 0; i < 16; i++) {
        if (!pending_reads[i].used)
            continue;
        if (pending_reads[i].epoch != a.epoch || whale_send(0x4a, pending_reads[i].body, 17))
            pending_reads[i].used = false;
    }
}
static uint32_t view_revision, pressed_revision[3];
static bool armed[3];
static uint32_t last_input, load_progress, last_draw, unbind_at;
static unsigned queries;
static bool dim, dirty = true, request_snap;
static uint8_t screen_applied = WHALE_SCREEN_ON;
/* dim means "not fully lit" (dimmed or off); the first press only wakes it. */
static void apply_screen(uint8_t screen) {
    if (screen == screen_applied && dim == (screen != WHALE_SCREEN_ON))
        return;
    screen_applied = screen;
    dim = screen != WHALE_SCREEN_ON;
    bsp_display_backlight(screen == WHALE_SCREEN_ON    ? 75
                          : screen == WHALE_SCREEN_DIM ? 18
                                                       : 0);
    dirty = true;
    ESP_LOGI("whale_power", "screen=%s", whale_power_screen_name(screen));
}
static void user_input(void) {
    last_input = a.now;
    whale_power_input(&power, a.now);
    if (dim)
        apply_screen(WHALE_SCREEN_ON);
}
static struct {
    uint64_t epoch;
    uint32_t snap, base, cursor, next, crc, deadline;
    uint16_t total;
    uint8_t count, taken;
    bool active;
    whale_row_t rows[8];
} stage;
static uint32_t requested_cursor, load_started;
static uint32_t ms(void) { return esp_timer_get_time() / 1000; }
static void completion_cue(const whale_row_t *before, const whale_row_t *after) {
    if (a.global_cap)
        return; /* New bridges report all sessions, not only this page. */
    bool active = (before->state >= 2 && before->state <= 4) || before->state == 8;
    if (!active || after->state != 5 || !before->run || before->session != after->session ||
        before->run != after->run)
        return;
    for (unsigned i = 0; i < 16; i++)
        if (sounded[i].session == after->session && sounded[i].run == after->run)
            return;
    if (!queue_cue(3)) {
        ESP_LOGW("whale", "Completion cue queue unavailable");
        return;
    }
    sounded[sounded_next].session = after->session;
    sounded[sounded_next].run = after->run;
    sounded_next = (sounded_next + 1) % 16;
}
bool whale_post(const whale_event_t *e) { return events && xQueueSend(events, e, 0) == pdTRUE; }
static void button(bsp_btn_t key, bsp_btn_ev_t ev, void *unused) {
    (void)unused;
    if (ev == BSP_BTN_PRESS || ev == BSP_BTN_LONG || ev == BSP_BTN_RELEASE)
        whale_post(&(whale_event_t){.kind = WE_KEY, .value = (uint32_t)key | ((unsigned)ev << 8)});
}
static void page(whale_page_t v) {
    a.confirm_holding = false;
    if (v != LIST_LOAD && v != LIST_FAILED) {
        seek_session = 0;
        seek_to_list = false;
    }
    if (v == MANAGE && a.view != SOUND && a.view != KEEP && a.view != REVOKE && a.view != VERSION &&
        a.view != UNBIND_FAILED)
        a.settings_return = a.view == LIST ? LIST : HOME;
    if (v != REPLY_READ && v != REPLY_LOAD && v != REPLY_FAILED)
        a.read_pending = false;
    view_revision++;
    if (v != COMPANION)
        a.companion_action = 0;
    if (v == MANAGE)
        a.menu_choice = 0;
    a.view = v;
    a.page_at = a.now;
    a.scroll = 0;
    dirty = true;
}
static void open_task(void);
static void voice_page(unsigned phase) {
    ESP_LOGI("whale_diag", "voice phase=%u", phase);
    a.voice_phase = phase;
    a.voice_at = a.now;
    page(VOICE);
}
static void attention_read(uint8_t kind) {
    if (!a.count)
        return;
    whale_row_t *r = &a.rows[a.selected];
    if (!(r->flags & (kind == 1 ? 8 : 16)))
        return;
    uint8_t b[17];
    put64(b, a.epoch);
    put32(b + 8, r->session);
    put32(b + 12, kind == 1 ? a.read_revision : r->run);
    b[16] = kind;
    if (kind == 1 &&
        (r->session != a.read_session || a.replies[a.selected].revision != a.read_revision))
        return;
    memcpy(pending_reads[pending_read_next].body, b, sizeof b);
    pending_reads[pending_read_next].epoch = a.epoch;
    pending_reads[pending_read_next].used = true;
    pending_read_next = (pending_read_next + 1) % 16;
    r->flags &= ~(kind == 1 ? 8 : 16);
    whale_cues_read(&reminders, r->session, kind == 1 ? 3 : 2);
    flush_reads();
}
static void voice_cancel(void) {
    whale_voice_stop(true);
    if (a.ready)
        whale_send(0x44, a.voice_identity, 24);
    memset(a.voice_identity, 0, sizeof a.voice_identity);
    memset(a.voice_text, 0, sizeof a.voice_text);
    a.voice_submitted = false;
    page(HOME);
}
static void voice_prepare(void) {
    whale_audio_interrupt();
    if (!a.ready || !a.linked) {
        voice_page(VOICE_DISCONNECTED);
        return;
    }
    if (!a.voice_cap || !whale_ble_audio_ready()) {
        voice_page(VOICE_UNSUPPORTED);
        return;
    }
    if (whale_voice_busy()) {
        voice_page(VOICE_MIC_ERROR);
        return;
    }
    put64(a.voice_identity, a.epoch);
    esp_fill_random(a.voice_identity + 8, 16);
    a.voice_text[0] = 0;
    a.voice_submitted = false;
    uint8_t b[28];
    put64(b, a.epoch);
    put32(b + 8, a.voice_session);
    memcpy(b + 12, a.voice_identity + 8, 16);
    voice_page(VOICE_PREPARE);
    if (!whale_send(0x40, b, sizeof b))
        voice_page(VOICE_DISCONNECTED);
}
static void voice_key(unsigned k, bool hold) {
    if (hold) {
        if (k == BSP_BTN_OK && a.voice_phase != VOICE_SENDING)
            voice_cancel();
        return;
    }
    unsigned p = a.voice_phase;
    a.voice_at = a.now;
    if (p == VOICE_RECORD) {
        if (k == BSP_BTN_UP)
            voice_cancel();
        if (k == BSP_BTN_OK) {
            whale_voice_stop(false);
            voice_page(VOICE_FINISH);
        }
    } else if (p == VOICE_PREVIEW) {
        if (k == BSP_BTN_OK) {
            a.voice_choice = 0;
            voice_page(a.voice_submitted ? VOICE_UNKNOWN : VOICE_ACTIONS);
        } else {
            int next = a.scroll + (k == BSP_BTN_DOWN ? 1 : -1);
            int max = whale_scroll_max(a.voice_text);
            a.scroll = next < 0 ? 0 : next > max ? max : next;
        }
    } else if (p == VOICE_ACTIONS) {
        if (k == BSP_BTN_UP && a.voice_choice)
            a.voice_choice--;
        if (k == BSP_BTN_DOWN && a.voice_choice < 2)
            a.voice_choice++;
        if (k == BSP_BTN_OK) {
            if (a.voice_choice == 1) {
                whale_send(0x44, a.voice_identity, 24);
                voice_prepare();
            } else if (a.voice_choice == 2)
                voice_cancel();
            else if (!a.ready)
                voice_page(VOICE_DISCONNECTED);
            else {
                a.voice_submitted = true;
                voice_page(VOICE_SENDING);
                if (!whale_send(0x45, a.voice_identity, 24))
                    voice_page(VOICE_UNKNOWN);
            }
        }
    } else if (p == VOICE_SENDING) {
        return;
    } else if (p == VOICE_UNKNOWN) {
        if (k == BSP_BTN_UP)
            voice_page(VOICE_PREVIEW);
        if (k == BSP_BTN_OK && a.ready) {
            voice_page(VOICE_SENDING);
            if (!whale_send(0x46, a.voice_identity, 24))
                voice_page(VOICE_UNKNOWN);
        }
    } else if (p == VOICE_PREPARE || p == VOICE_FINISH) {
        if (k == BSP_BTN_UP)
            voice_cancel();
    } else if (p == VOICE_SENT || p == VOICE_QUEUED) {
        if (k == BSP_BTN_OK)
            voice_cancel();
    } else if (k == BSP_BTN_UP) {
        if (a.voice_text[0] && (p == VOICE_FAILED || p == VOICE_ARCHIVED || p == VOICE_QUESTION))
            voice_page(VOICE_PREVIEW);
        else
            voice_cancel();
    } else if (k == BSP_BTN_OK) {
        if (p == VOICE_ARCHIVED) {
            voice_cancel();
            page(LIST);
        } else if (p == VOICE_QUESTION) {
            bool found = false;
            for (unsigned i = 0; i < a.count; i++) {
                if (a.rows[i].session == a.voice_session) {
                    a.selected = i;
                    found = true;
                    break;
                }
            }
            voice_cancel();
            if (found)
                open_task();
            else
                page(LIST);
        } else if (a.voice_text[0] && (p == VOICE_FAILED || p == VOICE_DISCONNECTED))
            voice_page(VOICE_PREVIEW);
        else
            voice_prepare();
    }
}
static void snap(uint32_t cursor) {
    requested_cursor = cursor;
    stage.active = false;
    request_snap = true;
    uint8_t b[14] = {0};
    put64(b, a.epoch);
    put32(b + 8, cursor);
    b[12] = 8;
    b[13] = 2;
    whale_send(0x10, b, sizeof b);
    stage.deadline = a.now + 3000;
}
static bool question_view(void) { return a.view >= LOADING && a.view <= UNSURE; }
static void open_list(void) {
    a.list_settings = !a.count;
    priority_list = false;
    seek_to_list = false;
    seek_session = 0;
    if (a.ready && a.linked && a.attention_cap) {
        page(LIST_LOAD);
        if (a.global_cap && a.count) {
            /* The bridge locates the current task in the globally sorted roster,
             * including tasks beyond the first cached page. */
            uint8_t b[12];
            put64(b, a.epoch);
            seek_session = a.rows[a.selected].session;
            put32(b + 8, seek_session);
            seek_to_list = true;
            requested_cursor = a.cursor;
            stage.active = false;
            request_snap = true;
            stage.deadline = a.now + 3000;
            if (whale_send(0x52, b, sizeof b))
                return;
            seek_session = 0;
            seek_to_list = false;
        }
        snap(0);
    } else {
        page(LIST); /* cached list remains available offline / with old bridges */
    }
}
static void enter_companion(void) {
    a.companion_choice = 0;
    a.companion_action = 0;
    a.companion_at = a.now;
    page(COMPANION);
}
static void enter_desk(void) {
    ESP_LOGI("whale", "desk display on (clock %s)", a.clock_valid ? "synced" : "unknown");
    page(DESK);
}
/* Work started or needs attention while the companion or desk display is up:
 * go back to the task pages. */
static void companion_work(uint32_t session, bool attention) {
    if (a.view != COMPANION && a.view != DESK)
        return;
    if (attention) {
        seek_session = 0;
        seek_to_list = false;
        a.list_settings = false;
        priority_list = true;
        if (a.ready && a.linked && a.attention_cap) {
            page(LIST_LOAD);
            snap(0); /* Work interruptions open the highest-priority items. */
        } else {
            a.selected = 0;
            page(LIST);
        }
        return;
    }
    for (unsigned i = 0; i < a.count; i++)
        if (a.rows[i].session == session) {
            a.selected = i;
            page(HOME);
            return;
        }
    uint8_t b[12];
    put64(b, a.epoch);
    put32(b + 8, session);
    seek_session = session;
    page(LIST_LOAD);
    /* A targeted snapshot locates tasks outside the currently cached page. */
    stage.active = false;
    request_snap = true;
    stage.deadline = a.now + 3000;
    if (!whale_send(0x52, b, sizeof b)) {
        seek_session = 0;
        open_list();
    }
}
static void companion_tick(void) {
    if (a.view == COMPANION && a.companion_action && a.now - a.action_at >= 3000) {
        a.companion_action = 0;
        dirty = true;
    }
    bool idle =
        !a.linked || (a.ready && a.global_cap && a.global_valid && a.now - a.global_at < 30000 &&
                      !a.global_running && !a.global_waiting && !a.global_unread);
    if ((a.view == HOME || a.view == COMPANION) && a.now - last_input >= WHALE_DESK_IDLE_MS &&
        idle && !whale_voice_busy() && !a.companion_action)
        enter_desk();
    /* After an offline desk display reconnects to an old bridge, the cached
     * page cannot prove all sessions are idle. Return to the task home. */
    if (a.view == DESK && a.linked && a.ready && !a.global_cap)
        page(HOME);
    if (disconnect_pending && !a.linked && a.now - disconnected_at >= 5000) {
        disconnect_pending = false;
        whale_cues_update(&reminders, reminders.seq + 1, 0, disconnected_at, 4, 4, a.now);
    }
    bool recording = whale_voice_busy() || (a.view == VOICE && (a.voice_phase == VOICE_PREPARE ||
                                                                a.voice_phase == VOICE_RECORD ||
                                                                a.voice_phase == VOICE_FINISH));
    if (reminders.batching && !recording && !whale_audio_busy() &&
        a.now - reminders.since >= 1000) {
        whale_cues_t copy = reminders;
        uint8_t cue = whale_cues_take(&copy, a.now, false);
        if (!cue || queue_cue(cue))
            reminders = copy;
    }
    if (a.view == HOME && a.ready && a.count) {
        const whale_row_t *r = &a.rows[a.selected];
        if (a.title_session != r->session || a.title_run != r->run ||
            strcmp(title_prefix, r->title)) {
            a.title_session = r->session;
            a.title_run = r->run;
            a.full_title[0] = 0;
            snprintf(title_prefix, sizeof title_prefix, "%s", r->title);
            title_requested_at = a.now - 5000;
        }
        if ((r->flags & 4) && !a.full_title[0] && a.now - title_requested_at >= 5000) {
            uint8_t b[16];
            put64(b, a.epoch);
            put32(b + 8, r->session);
            put32(b + 12, r->run);
            if (whale_send(0x16, b, sizeof b))
                title_requested_at = a.now;
        }
    }
}
static void ask_page(uint8_t idx) {
    if (!a.ready || !a.linked) {
        page(LOAD_FAILED);
        return;
    }
    uint8_t b[21];
    memcpy(b, a.identity, 20);
    b[20] = idx;
    if (a.view != LOADING)
        load_started = a.now;
    page(LOADING);
    a.received = 0;
    a.parts = 0;
    load_progress = a.now;
    if (!whale_send(0x24, b, sizeof b))
        page(LOAD_FAILED);
}
static void open_task(void) {
    if (!a.count) {
        page(HOME);
        return;
    }
    whale_row_t *r = &a.rows[a.selected];
    if (!r->question || !a.ready) {
        page(HOME);
        return;
    }
    put64(a.identity, a.epoch);
    put32(a.identity + 8, r->session);
    put32(a.identity + 12, r->run);
    put32(a.identity + 16, r->question);
    a.meta = false;
    a.page = 0;
    a.focus = 0;
    a.unsupported = false;
    ask_page(255);
}
static void query(void) {
    page(CHECKING);
    queries = 0;
    if (a.ready && a.linked) {
        whale_send(0x25, a.identity, 20);
        queries = 1;
    }
}
static void submit(void) {
    if (!a.linked || !a.ready || !a.meta || a.focus >= a.options) {
        query();
        return;
    }
    uint8_t b[22];
    memcpy(b, a.identity, 20);
    put16(b + 20, a.option_ids[a.focus]);
    if (whale_send(0x22, b, sizeof b))
        page(SENDING);
    else
        query();
}
static bool has_reply(void) {
    return a.count && a.replies[a.selected].session == a.rows[a.selected].session &&
           a.replies[a.selected].revision;
}
static void reply_failed(void) {
    a.read_pending = false;
    a.read_error = true;
    if (!a.read_used)
        page(REPLY_FAILED);
    dirty = true;
}
static void accept_reply(const char *text, size_t size, uint32_t count) {
    size_t top = whale_line_offset(a.answer, a.scroll);
    if (!a.read_used) {
        memcpy(a.answer, text, size);
        a.answer[size] = 0;
        a.read_first = a.read_page;
        a.read_sizes[0] = size;
        a.read_used = 1;
        page(REPLY_READ);
        attention_read(1);
    } else if (a.read_back) {
        if (a.read_used == 3) {
            a.answer[strlen(a.answer) - a.read_sizes[2]] = 0;
            a.read_used--;
        }
        memmove(a.answer + size, a.answer, strlen(a.answer) + 1);
        memcpy(a.answer, text, size);
        memmove(a.read_sizes + 1, a.read_sizes, a.read_used * sizeof a.read_sizes[0]);
        a.read_sizes[0] = size;
        a.read_used++;
        a.read_first = a.read_page;
        a.scroll = whale_scroll_at(a.answer, top + size);
        if (a.scroll > 0)
            a.scroll--;
    } else {
        if (a.read_used == 3) {
            size_t removed = a.read_sizes[0];
            memmove(a.answer, a.answer + removed, strlen(a.answer) - removed + 1);
            top = top > removed ? top - removed : 0;
            memmove(a.read_sizes, a.read_sizes + 1, 2 * sizeof a.read_sizes[0]);
            a.read_used--;
            a.read_first++;
        }
        size_t length = strlen(a.answer);
        memcpy(a.answer + length, text, size);
        a.answer[length + size] = 0;
        a.read_sizes[a.read_used++] = size;
        a.scroll = whale_scroll_at(a.answer, top) + 1;
    }
    int max = whale_home_scroll_max(a.answer);
    if (a.scroll > max)
        a.scroll = max;
    a.read_count = count;
    a.read_pending = a.read_error = false;
    dirty = true;
}
static void read_part(uint32_t number, bool backwards) {
    a.read_page = number;
    a.read_back = backwards;
    char cached[WHALE_REPLY_BYTES + 1];
    uint32_t count;
    if (whale_reply_cache_get(a.epoch, a.read_session, a.read_revision, number, cached, &count)) {
        accept_reply(cached, strlen(cached), count);
        return;
    }
    if (!a.ready) {
        reply_failed();
        return;
    }
    uint8_t b[20];
    put64(b, a.epoch);
    put32(b + 8, a.read_session);
    put32(b + 12, a.read_revision);
    put32(b + 16, number);
    a.read_pending = whale_send(0x1a, b, sizeof b);
    a.read_error = false;
    a.read_started = a.now;
    if (!a.read_pending)
        reply_failed();
    else if (!a.read_used)
        page(REPLY_LOAD);
}
static void open_reply(void) {
    if (!has_reply())
        return;
    a.read_session = a.rows[a.selected].session;
    a.read_revision = a.replies[a.selected].revision;
    a.read_count = 0;
    a.read_used = 0;
    a.read_pending = a.read_error = false;
    a.answer[0] = 0;
    read_part(0, false);
}
static bool reply_home(void) {
    return a.view == REPLY_READ || a.view == REPLY_LOAD || a.view == REPLY_FAILED;
}
static void enter_home(void) { page(HOME); }
static bool valid_reply_text(const uint8_t *p, size_t n) {
    for (size_t start = 0, i = 0; i <= n; i++) {
        if (i == n || p[i] == '\n') {
            if (!whale_text(p + start, i - start))
                return false;
            start = i + 1;
        }
    }
    return true;
}
static bool voice_displayable(const uint8_t *p, size_t n) {
    if (!valid_reply_text(p, n))
        return false;
    for (size_t i = 0; i < n;) {
        if (p[i] == '\n') {
            i++;
            continue;
        }
        unsigned bytes = p[i] < 128 ? 1 : p[i] < 224 ? 2 : p[i] < 240 ? 3 : 4;
        char glyph[5] = {0};
        memcpy(glyph, p + i, bytes);
        if (!whale_displayable(glyph))
            return false;
        i += bytes;
    }
    return true;
}
static void key(uint32_t code) {
    unsigned k = code & 255;
    bool hold = (code >> 8) != 0;
    if (dim) {
        user_input();
        return;
    }
    user_input();
    if (a.view == DESK) { /* any key returns to the normal pages */
        page(HOME);
        return;
    }
    if (a.view == VOICE) {
        voice_key(k, hold);
        return;
    }
    if (a.view == PAIR) {
        if (k == BSP_BTN_OK) {
            whale_pair(!hold);
            page(HOME);
        }
        return;
    }
    if (a.view == COMPANION) {
        if (hold) {
            if (k == BSP_BTN_OK)
                page(HOME);
            else if (k == BSP_BTN_DOWN)
                open_list();
            return;
        }
        if (a.companion_action)
            return;
        if (k == BSP_BTN_UP)
            a.companion_choice = (a.companion_choice + 4) % 5;
        else if (k == BSP_BTN_DOWN)
            a.companion_choice = (a.companion_choice + 1) % 5;
        else if (k == BSP_BTN_OK) {
            if (a.companion_choice == 4)
                page(HOME);
            else {
                a.companion_action = a.companion_choice + 1;
                a.action_at = a.now;
            }
        }
        return;
    }
    if (a.view == SOUND && !hold) {
        if (k == BSP_BTN_UP)
            a.sound_choice = (a.sound_choice + 3) % 4;
        else if (k == BSP_BTN_DOWN)
            a.sound_choice = (a.sound_choice + 1) % 4;
        else if (k == BSP_BTN_OK) {
            if (a.sound_choice < 2) {
                whale_audio_interrupt();
                whale_audio_cmd_t cmd = {6, a.sound_choice, 0};
                if (cues && xQueueSend(cues, &cmd, 0) == pdTRUE) {
                    a.sound_mode = a.sound_choice;
                    a.sound_saved = 0;
                } else
                    a.sound_saved = -1;
            } else if (a.sound_choice == 2) {
                whale_audio_interrupt();
                queue_cue(3);
            } else
                page(MANAGE);
        }
        return;
    }
    if (k == BSP_BTN_OK && hold) {
        if (a.view == SENDING || a.view == UNBINDING)
            return;
        if (a.view == HOME || reply_home()) {
            page(MANAGE);
            return;
        }
        if (a.view == MANAGE) {
            page(a.settings_return);
            return;
        }
        if (a.view == SOUND || a.view == KEEP || a.view == REVOKE || a.view == VERSION ||
            a.view == UNBIND_FAILED) {
            page(MANAGE);
            return;
        }
        if (a.view == OPTION) {
            page(QUESTION);
            return;
        }
        page(HOME);
        return;
    }
    if (hold && k == BSP_BTN_UP && a.view == HOME) {
        if (!a.count)
            return;
        a.voice_session = a.rows[a.selected].session;
        snprintf(a.voice_title, sizeof a.voice_title, "%s", a.rows[a.selected].title);
        a.voice_text[0] = 0;
        a.voice_submitted = false;
        if (a.rows[a.selected].question || a.rows[a.selected].state == 4 ||
            a.rows[a.selected].state == 8)
            voice_page(VOICE_QUESTION);
        else
            voice_prepare();
        return;
    }
    if (hold && k == BSP_BTN_DOWN && (a.view == HOME || reply_home())) {
        open_list();
        return;
    }
    if (hold)
        return;
    if (k == BSP_BTN_OK) {
        switch (a.view) {
        case REPLY_READ:
        case REPLY_LOAD:
        case REPLY_FAILED:
            page(HOME);
            break;
        case HOME:
            if (a.count && a.rows[a.selected].question && a.ready)
                open_task();
            else
                open_list();
            break;
        case LIST:
            if (a.list_settings || !a.count)
                page(MANAGE);
            else {
                attention_read(2);
                enter_home();
            }
            break;
        case LIST_FAILED:
            page(LIST_LOAD);
            snap(requested_cursor);
            break;
        case MANAGE:
            if (a.menu_choice == 0) {
                a.sound_choice = a.sound_mode;
                page(SOUND);
            } else if (a.menu_choice == 1)
                page(KEEP);
            else if (a.menu_choice == 2)
                page(VERSION);
            else
                page(a.settings_return);
            break;
        case KEEP:
            page(MANAGE);
            break;
        case REVOKE:
        case UNBIND_FAILED: {
            uint8_t b[12];
            put64(b, a.epoch);
            put32(b + 8, a.now);
            unbind_at = a.now;
            if (a.linked) {
                whale_send(0x30, b, 12);
            }
            page(UNBINDING);
            break;
        }
        case QUESTION:
            if (whale_text_wide(a.labels[a.focus]))
                page(OPTION);
            else
                submit();
            break;
        case PROMPT:
            page(QUESTION);
            break;
        case OPTION:
            submit();
            break;
        case LOAD_FAILED:
            ask_page(a.meta ? a.page : 255);
            break;
        case UNSURE:
            query();
            break;
        case VERSION:
            page(MANAGE);
            break;
        case RESULT:
            page(HOME);
            break;
        default:
            break;
        }
        return;
    }
    if (k != BSP_BTN_DOWN && k != BSP_BTN_UP)
        return;
    int d = k == BSP_BTN_DOWN ? 1 : -1;
    if (a.view == HOME) {
        if (d > 0)
            open_reply();
        else
            enter_companion();
        return;
    }
    if (a.view == MANAGE) {
        a.menu_choice = (a.menu_choice + (d > 0 ? 1 : 3)) % 4;
        return;
    }
    if (a.view == KEEP || a.view == REVOKE) {
        page(a.view == KEEP ? REVOKE : KEEP);
        return;
    }
    if ((a.view == REPLY_LOAD || a.view == REPLY_FAILED) && d < 0) {
        page(HOME);
        return;
    }
    if (a.view == REPLY_FAILED && d > 0) {
        if (has_reply() && a.replies[a.selected].revision != a.read_revision)
            open_reply();
        else
            read_part(a.read_page, a.read_back);
        return;
    }
    if (a.view == REPLY_READ) {
        if (a.read_pending)
            return;
        if (a.read_error && d > 0) {
            read_part(a.read_page, a.read_back);
            return;
        }
        int max = whale_home_scroll_max(a.answer);
        if (d > 0 && a.scroll < max) {
            a.scroll++;
        } else if (d < 0 && a.scroll > 0) {
            a.scroll--;
        } else if (d > 0 && has_reply() && a.replies[a.selected].revision != a.read_revision)
            open_reply();
        else if (d > 0 && a.read_first + a.read_used < a.read_count)
            read_part(a.read_first + a.read_used, false);
        else if (d < 0 && a.read_first)
            read_part(a.read_first - 1, true);
        else if (d < 0)
            page(HOME);
        return;
    }
    if (a.view == PROMPT || a.view == OPTION) {
        a.scroll += d;
        int max = whale_scroll_max(a.view == PROMPT ? a.prompt : a.labels[a.focus]);
        if (a.scroll < 0)
            a.scroll = 0;
        if (a.scroll > max)
            a.scroll = max;
        return;
    }
    if (a.view == LIST) {
        if (a.list_settings) {
            if (d > 0 && a.count) {
                a.list_settings = false;
                a.selected = 0;
            } else if (d < 0 && !request_snap && a.ready && a.cursor) {
                a.list_settings = false;
                page(LIST_LOAD);
                snap(a.cursor >= 8 ? a.cursor - 8 : 0);
            }
            return;
        }
        int next = (int)a.selected + d;
        if (a.count && next >= 0 && next < a.count)
            a.selected = next;
        else if (!request_snap && a.ready && d == 1 && a.next != UINT32_MAX) {
            page(LIST_LOAD);
            snap(a.next);
        } else if (d == -1)
            a.list_settings = true;
    } else if (a.view == QUESTION) {
        int next = (int)a.focus + d;
        if (next >= 0 && next < a.options)
            a.focus = next;
        else if (d == 1 && a.page + 1 < a.pages) {
            a.page++;
            a.focus = 0;
            ask_page(a.page);
        } else if (d == -1 && a.page) {
            a.page--;
            a.focus = 2;
            ask_page(a.page);
        }
    }
}
static bool current(const uint8_t *p, size_t n) { return n >= 20 && !memcmp(p, a.identity, 20); }
static void apply_message(whale_msg_t msg) {
    const uint8_t *p = msg.bytes;
    size_t n = msg.size;
    if (msg.type == 1 && n == 10) {
        // Protocol 2.3: a bridge offering bit 12 gets minor 3; large frames are granted
        // only when both peers and the ATT MTU allow more than the legacy 20 bytes.
        uint32_t offered = w32(p + 2);
        uint16_t frame = whale_frame_grant(offered, w16(p + 6), w16(p + 8),
                                           whale_ble_frame_limit());
        uint8_t b[11] = {2, offered & WHALE_CAP_LARGE_FRAMES ? 3 : 2};
        put32(b + 2, (offered & 4095) | (frame > WHALE_FRAME_LEGACY ? WHALE_CAP_LARGE_FRAMES : 0));
        a.global_cap = (w32(p + 2) & 2048) != 0;
        a.global_valid = false;
        a.global_at = 0;
        a.global_seq = 0;
        memset(&reminders, 0, sizeof reminders);
        disconnect_pending = false;
        a.voice_cap = (w32(p + 2) & 512) != 0;
        a.attention_cap = (w32(p + 2) & 1024) != 0;
        if (p[0] != 2 || p[1] != 2 || (w32(p + 2) & 63) != 63 || w16(p + 6) < 20 || w16(p + 8) < 20) {
            b[10] = 1;
            frame = WHALE_FRAME_LEGACY;
            put32(b + 2, offered & 4095);
        }
        put16(b + 6, frame);
        put16(b + 8, frame);
        a.linked = true;
        a.ready = false;
        // The reply itself is legacy: the bridge switches only after reading it.
        whale_ble_set_frame(WHALE_FRAME_LEGACY);
        rx.frame = frame > WHALE_FRAME_LEGACY ? (uint8_t)frame : 0;
        whale_send(2, b, 11);
        whale_ble_set_frame(frame);
        if (!b[10])
            snap(0);
        else
            page(VERSION);
        return;
    }
    if (!a.linked)
        return;
    if (msg.type == 0x11 && n == 27) {
        stage.active = false;
        uint32_t c = w32(p + 16), next = w32(p + 20);
        uint16_t total = w16(p + 24);
        uint8_t count = p[26];
        if (count > 8 || count > total ||
            (!seek_session && c != requested_cursor &&
             !(requested_cursor >= total && c == (total ? (uint32_t)((total - 1) / 8) * 8 : 0))) ||
            (!total && next != UINT32_MAX) || (next != UINT32_MAX && (!count || next <= c))) {
            snap(0);
            return;
        }
        stage.epoch = w64(p);
        stage.snap = w32(p + 8);
        stage.base = w32(p + 12);
        stage.cursor = c;
        stage.next = next;
        stage.count = count;
        stage.total = total;
        stage.taken = 0;
        stage.crc = whale_crc(0, p, 27);
        stage.deadline = a.now + 3000;
        stage.active = true;
        return;
    }
    if ((msg.type == 0x12 || msg.type == 0x13) && stage.active) {
        if (n < 13 || w64(p) != stage.epoch || w32(p + 8) != stage.snap || p[12] != stage.taken) {
            snap(requested_cursor);
            return;
        }
        if (msg.type == 0x12) {
            whale_row_t *r = &stage.rows[stage.taken < 8 ? stage.taken : 0];
            if (stage.taken >= stage.count || !whale_parse_row(p + 13, n - 13, r, a.now)) {
                snap(requested_cursor);
                return;
            }
            for (int i = 0; i < stage.taken; i++)
                if (stage.rows[i].session == r->session) {
                    snap(requested_cursor);
                    return;
                }
            stage.taken++;
            stage.crc = whale_crc(stage.crc, p + 13, n - 13);
            stage.deadline = a.now + 3000;
            return;
        }
        if (n != 17 || stage.taken != stage.count || w32(p + 13) != stage.crc) {
            snap(requested_cursor);
            return;
        }
        uint32_t old = !seek_session && a.count ? a.rows[a.selected].session : 0;
        bool resumed = !a.ready;
        bool new_epoch = a.epoch && a.epoch != stage.epoch;
        if (new_epoch) {
            a.title_session = 0;
            a.full_title[0] = 0;
            whale_reply_cache_clear();
            memset(sounded, 0, sizeof sounded);
            sounded_next = 0;
        }
        /* Initial/reconnected snapshots establish a silent baseline. Only
         * an observed live transition in the same run can announce completion. */
        if (!resumed && !new_epoch)
            for (unsigned i = 0; i < a.count; i++)
                for (unsigned j = 0; j < stage.taken; j++)
                    if (a.rows[i].session == stage.rows[j].session)
                        completion_cue(&a.rows[i], &stage.rows[j]);
        for (unsigned i = 0; i < a.count; i++) {
            bool retained = false;
            for (unsigned j = 0; j < stage.taken; j++)
                retained |= a.rows[i].session == stage.rows[j].session;
            if (!retained)
                whale_reply_cache_forget(a.rows[i].session, 0);
        }
        memcpy(a.rows, stage.rows, stage.taken * sizeof(whale_row_t));
        if (!stage.taken)
            a.selected = 0;
        else if (a.selected >= stage.taken)
            a.selected = stage.taken - 1;
        bool kept = false;
        for (int i = 0; i < stage.taken; i++) {
            if (a.rows[i].session == old) {
                a.selected = i;
                kept = true;
            }
        }
        /* Preserve the current task when opening or refreshing the list. */
        if (a.view == LIST_LOAD && (!kept || priority_list))
            a.selected = 0;
        priority_list = false;
        a.seq = stage.base;
        a.epoch = stage.epoch;
        a.cursor = stage.cursor;
        a.total = stage.total;
        a.next = stage.next;
        a.count = stage.count;
        if (!a.count)
            a.list_settings = true;
        if (seek_session) {
            bool found = false;
            for (unsigned i = 0; i < a.count; i++)
                if (a.rows[i].session == seek_session) {
                    a.selected = i;
                    found = true;
                    break;
                }
            whale_page_t destination = found && !seek_to_list ? HOME : LIST;
            seek_session = 0;
            page(destination);
            seek_to_list = false;
        }
        a.ready = true;
        if (a.view == VOICE) {
            if (new_epoch) {
                whale_voice_stop(true);
                voice_page(a.voice_submitted ? VOICE_UNKNOWN : VOICE_ARCHIVED);
            }
        }
        stage.active = false;
        request_snap = false;
        memset(a.replies, 0, sizeof a.replies);
        if (old && (!kept || new_epoch) &&
            (a.view == HOME || question_view() || a.view == RESULT || a.view == REPLY_READ ||
             a.view == REPLY_LOAD || a.view == REPLY_FAILED)) {
            memset(a.identity, 0, sizeof a.identity);
            a.answer[0] = a.prompt[0] = 0;
            memset(a.labels, 0, sizeof a.labels);
            a.read_session = a.read_revision = 0;
            a.read_used = 0;
            a.read_pending = false;
            page(LIST);
        }
        if (new_epoch && (a.view == REPLY_READ || a.view == REPLY_LOAD || a.view == REPLY_FAILED))
            page(LIST);
        if (a.view == LIST_LOAD || a.view == LIST_FAILED)
            page(LIST);
        if (new_epoch && question_view()) {
            a.result = 3;
            a.unsupported = false;
            page(RESULT);
        } else if (resumed && (a.view == CHECKING || a.view == UNSURE))
            query();
        if (resumed && a.view == HOME && !new_epoch)
            a.recovered = a.now;
        return;
    }
    if (msg.type == 0x31 && n == 12 && a.view == UNBINDING && w64(p) == a.epoch &&
        w32(p + 8) == unbind_at) {
        unbind_at = a.now - 500;
        return;
    }
    if (msg.type == 0x32 && n == 5 && reply_home() && a.read_pending && p[0] == 0x1a) {
        reply_failed();
        return;
    }
    if (msg.type == 0x47 && n == 28 && !memcmp(p, a.voice_identity, 24)) {
        whale_voice_ack(p, w32(p + 24));
        return;
    }
    if (msg.type == 0x41 && a.view == VOICE && n >= 27 && n == 27u + w16(p + 25) &&
        w16(p + 25) <= 1800 && !memcmp(p, a.voice_identity, 24)) {
        static const uint8_t phases[] = {
            VOICE_FINISH,   VOICE_RECORD,  VOICE_PREVIEW,     VOICE_SENT,      VOICE_QUEUED,
            VOICE_FAILED,   VOICE_UNKNOWN, VOICE_UNSUPPORTED, VOICE_ASR_ERROR, VOICE_ARCHIVED,
            VOICE_QUESTION, VOICE_EXPIRED, VOICE_EMPTY,       VOICE_BROKEN};
        unsigned code = p[24];
        if (!code || code >= sizeof phases)
            return;
        if (code == 1) {
            if (a.voice_phase == VOICE_PREPARE && !whale_voice_begin(a.voice_identity))
                voice_page(VOICE_MIC_ERROR);
            return;
        }
        if (code == 2) {
            if (!voice_displayable(p + 27, n - 27)) {
                a.voice_text[0] = 0;
                voice_page(VOICE_ASR_ERROR);
                return;
            }
            memcpy(a.voice_text, p + 27, n - 27);
            a.voice_text[n - 27] = 0;
        }
        if (code == 3 || code == 4)
            a.voice_submitted = false;
        if (code == 5)
            a.voice_submitted = false;
        if (code >= 7)
            whale_voice_stop(true);
        voice_page(phases[code]);
        return;
    }
    if (!a.ready || n < 8 || w64(p) != a.epoch)
        return;
    if (msg.type == 0x53 && n == 18) {
        /* Wall clock: epoch, Unix seconds (int64), local UTC offset minutes. */
        if (whale_clock_sync(whale_sleep_clock(), rtc_seconds(), (int64_t)w64(p + 8),
                             (int16_t)w16(p + 16)))
            clock_step();
        return;
    }
    if (msg.type == 0x17 && n >= 18 && n == 18u + w16(p + 16) && n - 18 <= 512 &&
        w32(p + 8) == a.title_session && w32(p + 12) == a.title_run && whale_text(p + 18, n - 18)) {
        memcpy(a.full_title, p + 18, n - 18);
        a.full_title[n - 18] = 0;
        return;
    }
    if (msg.type == 0x50 && a.global_cap && n == 25 && p[24] <= 1 && w16(p + 14) <= w16(p + 12) &&
        w16(p + 16) <= w16(p + 12) && w16(p + 18) <= w16(p + 12)) {
        if (w32(p + 8) < a.global_seq)
            return;
        bool baseline = !a.global_valid;
        a.global_seq = w32(p + 8);
        a.global_at = a.now;
        a.global_valid = p[24] != 0;
        a.global_running = w16(p + 14);
        a.global_waiting = w16(p + 16);
        a.global_unread = w16(p + 18);
        a.global_focus = w32(p + 20);
        /* The desk display leaves on any summary that shows work; the
         * companion page only reacts to the first one after (re)connecting. */
        if ((a.view == DESK || (a.view == COMPANION && baseline)) && a.global_valid) {
            if (a.global_waiting || a.global_unread)
                companion_work(0, true);
            else if (a.global_running)
                companion_work(a.global_focus, false);
        }
        return;
    }
    if (msg.type == 0x51 && a.global_cap && n == 26 && p[24] <= 3 && (p[25] <= 3 || p[25] == 5)) {
        uint32_t seq = w32(p + 8);
        if (seq <= reminders.seq)
            return;
        /* A gap means a lost control event: discard pending cues and refresh
         * the list; never guess an event or replay an old notification. */
        if (reminders.seq && seq != reminders.seq + 1) {
            memset(&reminders, 0, sizeof reminders);
            a.global_valid = false;
        }
        whale_cues_update(&reminders, seq, w32(p + 12), w32(p + 20), p[24], p[25], a.now);
        if (p[25])
            companion_work(w32(p + 12), p[25] != 5);
        return;
    }
    if (msg.type == 0x14 && n >= 13) {
        uint32_t seq = w32(p + 8);
        if (seq <= a.seq)
            return;
        if (seq != a.seq + 1 || stage.active || p[12] != 0) {
            snap(a.cursor);
            return;
        }
        whale_row_t row;
        if (!whale_parse_row(p + 13, n - 13, &row, a.now)) {
            snap(a.cursor);
            return;
        }
        for (int i = 0; i < a.count; i++)
            if (a.rows[i].session == row.session) {
                completion_cue(&a.rows[i], &row);
                a.rows[i] = row;
                a.seq = seq;
                return;
            }
        snap(a.cursor);
    } else if (msg.type == 0x19 && n >= 17 && p[16] <= 80 && n == 17u + p[16] &&
               whale_text(p + 17, p[16])) {
        for (int i = 0; i < a.count; i++)
            if (a.rows[i].session == w32(p + 8)) {
                whale_reply_cache_forget(w32(p + 8), w32(p + 12));
                a.replies[i].session = w32(p + 8);
                a.replies[i].revision = w32(p + 12);
                memcpy(a.replies[i].preview, p + 17, p[16]);
                a.replies[i].preview[p[16]] = 0;
                if (i == a.selected && reply_home() && a.read_revision != w32(p + 12)) {
                    if (w32(p + 12))
                        open_reply();
                    else {
                        a.answer[0] = 0;
                        a.read_used = 0;
                        page(HOME);
                    }
                }
            }
    } else if (msg.type == 0x1b && reply_home() && a.read_pending && n >= 26 &&
               w32(p + 8) == a.read_session && w32(p + 12) == a.read_revision &&
               w32(p + 16) == a.read_page && w32(p + 20) > a.read_page && w16(p + 24) == n - 26 &&
               (!a.read_used || w32(p + 20) == a.read_count) && n - 26 <= 384 &&
               valid_reply_text(p + 26, n - 26)) {
        whale_reply_cache_put(a.epoch, a.read_session, a.read_revision, a.read_page, w32(p + 20),
                              p + 26, n - 26);
        accept_reply((const char *)p + 26, n - 26, w32(p + 20));
    } else if (msg.type == 0x18 && n == 17 && p[16] <= 5) {
        for (int i = 0; i < a.count; i++)
            if (a.rows[i].session == w32(p + 8) && a.rows[i].run == w32(p + 12))
                a.rows[i].hint = p[16];
    } else if (msg.type == 0x15 && n == 18 && p[8] <= 2 && p[9] <= 2) {
        a.freshness = p[8];
        a.currency = p[9];
        a.balance = w64(p + 10);
    } else if (msg.type == 0x23 && n == 23 && current(p, n) && question_view()) {
        if (p[20] < 4) {
            a.result = p[20];
            page(RESULT);
        }
    } else if (msg.type == 0x20 && a.view == LOADING && current(p, n) && n >= 24 && !a.meta) {
        if (p[20] < 2 || p[20] > 64 || p[21] != (p[20] + 2) / 3 || w16(p + 22) != n - 24 ||
            n - 24 > 512 || !whale_text(p + 24, n - 24)) {
            page(LOAD_FAILED);
            return;
        }
        memcpy(a.prompt, p + 24, n - 24);
        a.prompt[n - 24] = 0;
        a.meta = true;
        a.pages = p[21];
        a.all_options = p[20];
        if (!whale_displayable(a.prompt)) {
            a.unsupported = true;
            page(RESULT);
            return;
        }
        ask_page(a.page);
    } else if (msg.type == 0x21 && a.view == LOADING && current(p, n) && n >= 22 && a.meta &&
               p[20] == a.page) {
        unsigned opts = a.all_options - a.page * 3;
        if (opts > 3)
            opts = 3;
        if (p[21] != opts || !opts) {
            page(LOAD_FAILED);
            return;
        }
        size_t off = 22;
        for (unsigned j = 0; j < opts; j++) {
            if (off + 4 > n) {
                page(LOAD_FAILED);
                return;
            }
            uint16_t id = w16(p + off), l = w16(p + off + 2);
            off += 4;
            if (id == 65535 || !l || l > 256 || off + l > n || !whale_text(p + off, l)) {
                page(LOAD_FAILED);
                return;
            }
            for (unsigned k = 0; k < j; k++)
                if (a.option_ids[k] == id) {
                    page(LOAD_FAILED);
                    return;
                }
            a.option_ids[j] = id;
            memcpy(a.labels[j], p + off, l);
            a.labels[j][l] = 0;
            off += l;
            if (!whale_displayable(a.labels[j])) {
                a.unsupported = true;
                page(RESULT);
                return;
            }
        }
        if (off != n) {
            page(LOAD_FAILED);
            return;
        }
        a.options = opts;
        if (a.focus >= opts)
            a.focus = 0;
        page(!a.page && whale_text_wide(a.prompt) ? PROMPT : QUESTION);
    }
}
static bool main_view(void) {
    return a.view == HOME || a.view == LIST || a.view == COMPANION || a.view == DESK;
}
static void power_step(void) {
    bool hold = whale_voice_busy() || whale_audio_busy() || reminders.batching ||
                a.view == PAIR || a.view == UNBINDING;
    /* Nobody answers an offline page: once the screen would turn off, return
     * home so the device may sleep. Pairing and unbinding finish on their own. */
    if (!a.linked && !hold && !main_view() && a.now - last_input >= WHALE_POWER_OFF_MS) {
        if (a.view == VOICE)
            voice_cancel();
        else
            page(HOME);
    }
    bool busy = !main_view();
    /* Rows are stale while offline; only a live task keeps the screen lit. */
    for (int i = 0; a.linked && i < a.count; i++)
        if ((a.rows[i].state >= 2 && a.rows[i].state <= 4) || a.rows[i].question)
            busy = true;
    /* The desk display follows the normal dim / screen-off / sleep policy,
     * day and night. */
    if (a.view == COMPANION || a.view == DESK)
        busy = false;
    whale_power_plan_t plan = whale_power_tick(&power, a.now, busy, hold);
    apply_screen(plan.screen);
    whale_ble_set_slow(plan.slow_adv);
    power_flags = (a.linked ? WHALE_BATT_LINKED : 0) |
                  (plan.screen != WHALE_SCREEN_OFF ? WHALE_BATT_SCREEN : 0) |
                  (power.quiet ? WHALE_BATT_TIMER : 0) | (plan.slow_adv ? WHALE_BATT_SLOW_ADV : 0);
    if (plan.sleep && !sleep_requested && (int32_t)(a.now - sleep_retry_at) >= 0) {
        whale_audio_cmd_t cmd = {WHALE_CMD_SLEEP, 0, 0};
        sleep_timer_ms = plan.timer_ms;
        sleep_cycles = whale_power_next_cycles(&power);
        log_diag("sleep");
        if (cues && xQueueSend(cues, &cmd, 0) == pdTRUE)
            sleep_requested = true;
        else
            sleep_retry_at = a.now + 30000;
    }
}
static whale_batt_log_t batt_log;
static uint32_t rtc_seconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec;
}
static void clock_step(void) {
    whale_local_time_t t;
    a.clock_valid = whale_clock_local(whale_sleep_clock(), rtc_seconds(), &t);
    if (a.clock_valid)
        a.clock = t;
    a.night = a.clock_valid && whale_clock_night(&a.clock);
}
static void batt_dump(const char *name) {
    if (!whale_blob_load(name, &batt_log, sizeof batt_log) || !whale_batt_log_valid(&batt_log))
        whale_batt_log_reset(&batt_log);
    ESP_LOGI("whale_batt", "log=%s count=%u", name, (unsigned)batt_log.count);
    for (unsigned i = 0; i < batt_log.count; i++)
        ESP_LOGI("whale_batt", "log=%s i=%u t=%lu soc=%u flags=%u", name, i,
                 (unsigned long)batt_log.e[i].t, (unsigned)batt_log.e[i].soc,
                 (unsigned)batt_log.e[i].flags);
}
static void batt_record(int soc) {
    if (whale_batt_log_new_run(&batt_log, soc))
        whale_blob_save("batt_prev", &batt_log, sizeof batt_log);
    if (whale_batt_log_add(&batt_log, rtc_seconds(), soc, power_flags) != WHALE_BATT_SAME &&
        !whale_blob_save("batt_cur", &batt_log, sizeof batt_log))
        ESP_LOGW("whale_batt", "battery log save failed");
}
static void peripheral_task(void *unused) {
    (void)unused;
    bool initialized = false;
    uint32_t polled = ms() - 30000;
    /* Print the stored discharge runs once per boot for whale_battery.py. */
    batt_dump("batt_prev");
    batt_dump("batt_cur");
    for (;;) {
        if (ms() - polled >= 30000) {
            polled = ms();
            if (!initialized)
                initialized = bsp_battery_init() == ESP_OK;
            int soc = initialized ? bsp_battery_soc() : -1;
            batt_record(soc);
            periph_stack_min = (unsigned)uxTaskGetStackHighWaterMark(NULL);
            whale_post(&(whale_event_t){.kind = 99, .value = (uint32_t)soc});
        }
        /* Battery and codec share I2C. One worker serializes initialization;
         * no audio operation runs in the event dispatcher or under LVGL. */
        bool captured = whale_voice_pending();
        if (captured) {
            whale_voice_run();
            periph_stack_min = (unsigned)uxTaskGetStackHighWaterMark(NULL);
        }
        whale_audio_cmd_t cue;
        if (cues) {
            if (xQueueReceive(cues, &cue, pdMS_TO_TICKS(50)) == pdTRUE) {
                if (cue.kind == WHALE_CMD_SLEEP) {
                    if (!whale_sleep_enter(sleep_timer_ms, sleep_cycles))
                        whale_post(&(whale_event_t){.kind = 97});
                } else if (cue.kind == 6) {
                    bool saved = whale_sound_save(cue.mode);
                    whale_post(&(whale_event_t){.kind = 98, .value = saved ? 1 : 0});
                } else if (!whale_voice_busy()) {
                    bool played = whale_audio_play(cue);
                    ESP_LOGI("whale_diag", "cue kind=%u played=%d", (unsigned)cue.kind,
                             (int)played);
                    (void)played;
                }
                periph_stack_min = (unsigned)uxTaskGetStackHighWaterMark(NULL);
            }
        } else
            vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
static void service(whale_event_t e) {
    dirty = true;
    switch (e.kind) {
    case WE_FRAME: {
        whale_msg_t m;
        uint8_t previous_part = rx.slots[0].next;
        uint16_t previous_id = rx.slots[0].id;
        bool large = e.len > sizeof e.bytes;
        const uint8_t *frame = large ? whale_ble_frame(e.value) : e.bytes;
        int result = whale_receive(&rx, frame, e.len, a.now, &m);
        if (result == 1)
            apply_message(m);
        else if (result < 0 && a.ready && !question_view())
            snap(a.cursor);
        if (result == 0 && a.view == LOADING && rx.slots[0].active &&
            (frame[1] == 0x20 || frame[1] == 0x21) &&
            (rx.slots[0].id != previous_id || rx.slots[0].next != previous_part)) {
            a.received = rx.slots[0].next;
            a.parts = rx.slots[0].parts;
            load_progress = a.now;
        }
        if (large) // whale_receive copied the payload; return the slot to the BLE ring
            whale_ble_frame_release();
        break;
    }
    case WE_LINK:
        a.linked = true;
        whale_power_link(&power, a.now, true);
        log_diag("link-up");
        break;
    case WE_PAIR:
        a.passkey = e.value;
        page(PAIR);
        break;
    case WE_VOICE:
        if (a.view == VOICE && e.len == 16 && !memcmp(e.bytes, a.voice_identity + 8, 16)) {
            if (e.value == 1 && a.voice_phase == VOICE_PREPARE)
                voice_page(VOICE_RECORD);
            else if (e.value == 2 &&
                     (a.voice_phase == VOICE_RECORD || a.voice_phase == VOICE_FINISH))
                voice_page(VOICE_FINISH);
            else if (e.value == 3)
                voice_page(VOICE_MIC_ERROR);
            else if (e.value == 4)
                voice_page(VOICE_BROKEN);
        }
        break;
    case WE_DOWN:
        whale_power_link(&power, a.now, false);
        log_diag("link-down");
        rx.frame = 0;
        whale_audio_interrupt();
        if (a.linked) {
            disconnected_at = a.now;
            disconnect_pending = true;
        }
        a.global_valid = false;
        a.global_seq = 0;
        seek_session = 0;
        seek_to_list = false;
        memset(&reminders, 0, sizeof reminders);
        whale_voice_stop(true);
        if (a.view == VOICE)
            voice_page(a.voice_submitted ? VOICE_UNKNOWN : VOICE_DISCONNECTED);
        a.linked = a.ready = stage.active = request_snap = false;
        if (a.read_pending)
            reply_failed();
        memset(&rx, 0, sizeof rx);
        if (a.view == SENDING || a.view == CHECKING)
            query();
        else if (question_view())
            page(LOAD_FAILED);
        else if (a.view == PAIR)
            page(HOME);
        break;
    case WE_KEY: {
        unsigned k = e.value & 255, ev = e.value >> 8;
        if (k >= 3)
            break;
        if (ev == BSP_BTN_PRESS && a.view == DESK) {
            /* Even a press that only lights the screen leaves the desk display;
             * its release must not act on the page underneath. */
            armed[k] = false;
            user_input();
            page(HOME);
            break;
        }
        if (ev == BSP_BTN_PRESS) {
            /* After a key wake the waking press may still be held at boot. */
            armed[k] = !dim && (int32_t)(a.now - key_guard_until) >= 0;
            pressed_revision[k] = view_revision;
            if (k == BSP_BTN_OK && armed[k] && a.view == HOME) {
                a.confirm_holding = true;
                a.confirm_at = a.now;
                a.confirm_hold_ms = BSP_BTN_LONG_PRESS_MS;
            }
            user_input();
        } else if (armed[k] && (ev == BSP_BTN_LONG || ev == BSP_BTN_RELEASE)) {
            armed[k] = false;
            if (k == BSP_BTN_OK)
                a.confirm_holding = false;
            if (pressed_revision[k] == view_revision)
                key(k | ((ev == BSP_BTN_LONG) << 8));
        }
        break;
    }
    case 97: /* sleep refused (key held or BLE busy); try again later */
        sleep_requested = false;
        sleep_retry_at = a.now + 30000;
        break;
    case 98:
        a.sound_saved = e.value ? 1 : -1;
        break;
    case 99:
        if (e.value <= 100) {
            a.soc = e.value;
            a.battery_level = a.soc <= 5 || (a.battery_level == 2 && a.soc <= 7)     ? 2
                              : a.soc <= 20 || (a.battery_level >= 1 && a.soc <= 22) ? 1
                                                                                     : 0;
            a.soc_at = a.now;
            a.seen_battery = true;
            a.soc_stale = false;
        } else
            a.soc_stale = true;
        break;
    }
}
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    a.sound_mode = whale_sound_load();
    a.sound_saved = 1;
    events = xQueueCreate(40, sizeof(whale_event_t));
    assert(events);
    ESP_ERROR_CHECK(bsp_display_init());
    ESP_ERROR_CHECK(bsp_lvgl_init() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(bsp_lvgl_lock(0) ? ESP_OK : ESP_FAIL);
    whale_ui_init();
    bsp_lvgl_unlock();
    ESP_ERROR_CHECK(bsp_button_init(button, NULL));
    whale_ble_start();
    ESP_LOGI("whale",
             "v1.4.0 / protocol 2.3 large frames + companion cues + power saving + desk clock; "
             "minimum free heap: %lu",
             (unsigned long)esp_get_minimum_free_heap_size());
#ifdef WHALE_POWER_TEST
    ESP_LOGW("whale_power", "BENCH BUILD: short power thresholds (WHALE_POWER_TEST)");
#endif
    cues = xQueueCreate(8, sizeof(whale_audio_cmd_t));
    if (!cues)
        ESP_LOGW("whale", "Completion cue queue allocation failed");
    last_input = ms();
    a.now = last_input;
    boot_info = whale_sleep_boot_info();
    whale_power_init(&power, a.now, boot_info.boot == WHALE_BOOT_TIMER, boot_info.cycles);
    if (boot_info.boot == WHALE_BOOT_KEY)
        key_guard_until = a.now + 1000;
    clock_step();
    log_diag("boot");
    xTaskCreate(peripheral_task, "whale_periph", WHALE_PERIPHERAL_STACK_BYTES, NULL, 2, NULL);
    /* A timer wake looks for the computer with the screen dark. */
    screen_applied = WHALE_SCREEN_OFF;
    apply_screen(power.screen);
    uint32_t diag_at = ms();
    for (;;) {
        whale_event_t e;
        bool have = xQueueReceive(events, &e, pdMS_TO_TICKS(40)) == pdTRUE;
        a.now = ms();
        a.voice_ms = whale_voice_milliseconds();
        if (a.now - read_retry_at >= 1000) {
            read_retry_at = a.now;
            flush_reads();
        }
        if (have)
            service(e);
        if (a.now - diag_at >= WHALE_DIAG_MS) {
            diag_at = a.now;
            log_diag("periodic");
        }
        if (a.view == VOICE) {
            uint32_t age = a.now - a.voice_at;
            if (a.voice_phase == VOICE_SENDING && age >= 5000)
                voice_page(VOICE_UNKNOWN);
            if ((a.voice_phase == VOICE_PREPARE && age >= 10000) ||
                (a.voice_phase == VOICE_FINISH && age >= 30000)) {
                whale_voice_stop(true);
                whale_send(0x44, a.voice_identity, 24);
                voice_page(VOICE_ASR_ERROR);
            }
            if (!a.voice_submitted && age >= 600000) {
                whale_voice_stop(true);
                whale_send(0x44, a.voice_identity, 24);
                a.voice_text[0] = 0;
                voice_page(VOICE_EXPIRED);
            }
        }
        if (a.seen_battery && a.now - a.soc_at >= 90000) {
            a.soc = -1;
            a.battery_level = 0;
        }
        if (reply_home() && a.read_pending && a.now - a.read_started >= 8000)
            reply_failed();
        if (a.view == LOADING && (a.now - load_progress >= 2000 || a.now - load_started >= 8000))
            page(LOAD_FAILED);
        if (a.view == SENDING && a.now - a.page_at >= 5000)
            query();
        if (a.view == CHECKING && a.ready) {
            uint32_t t = a.now - a.page_at;
            if (t >= 10000)
                page(UNSURE);
            else if ((queries == 1 && t >= 2000) || (queries == 2 && t >= 6000)) {
                whale_send(0x25, a.identity, 20);
                queries++;
            }
        }
        if (a.view == UNBINDING && a.now - unbind_at >= 500) {
            if (whale_unbind()) {
                whale_reply_cache_clear();
                memset(sounded, 0, sizeof sounded);
                sounded_next = 0;
                uint8_t mode = a.sound_mode;
                memset(&a, 0, sizeof a);
                a.sound_mode = mode;
                memset(&reminders, 0, sizeof reminders);
                a.global_valid = false;
                a.soc = -1;
                a.next = UINT32_MAX;
                page(HOME);
            } else
                page(UNBIND_FAILED);
        }
        if ((stage.active || request_snap) && (int32_t)(a.now - stage.deadline) >= 0 && a.linked) {
            if (a.view == LIST_LOAD) {
                stage.active = request_snap = false;
                page(LIST_FAILED);
            } else
                snap(requested_cursor);
        }
        clock_step();
        companion_tick();
        power_step();
        /* With the backlight off nothing is visible: skip LVGL work. */
        if (screen_applied != WHALE_SCREEN_OFF &&
            ((dirty && a.now - last_draw >= 125) || a.now - last_draw >= 125)) {
            if (bsp_lvgl_lock(25)) {
                whale_ui_draw(&a);
                bsp_lvgl_unlock();
                dirty = false;
                last_draw = a.now;
            }
        }
    }
}
