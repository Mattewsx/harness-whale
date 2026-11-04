#include "whale_app.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "whale_reply_cache.h"
#include <stdio.h>
#include <string.h>

static whale_app_t a = {.soc = -1, .next = UINT32_MAX};
static whale_rx_t rx;
static QueueHandle_t events;
static uint32_t view_revision, pressed_revision[3];
static bool armed[3];
static uint32_t last_input, load_progress, last_draw, unbind_at;
static unsigned queries;
static bool dim, dirty = true, request_snap;
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
bool whale_post(const whale_event_t *e) { return events && xQueueSend(events, e, 0) == pdTRUE; }
static void button(bsp_btn_t key, bsp_btn_ev_t ev, void *unused) {
    (void)unused;
    if (ev == BSP_BTN_PRESS || ev == BSP_BTN_LONG || ev == BSP_BTN_RELEASE)
        whale_post(&(whale_event_t){.kind = WE_KEY, .value = (uint32_t)key | ((unsigned)ev << 8)});
}
static void page(whale_page_t v) {
    if (v != REPLY_READ && v != REPLY_LOAD && v != REPLY_FAILED)
        a.read_pending = false;
    view_revision++;
    a.view = v;
    a.page_at = a.now;
    a.scroll = 0;
    dirty = true;
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
static void open_title(void) {
    if (!a.count)
        return;
    whale_row_t *r = &a.rows[a.selected];
    uint8_t b[16];
    put64(b, a.epoch);
    put32(b + 8, r->session);
    put32(b + 12, r->run);
    snprintf(a.title, sizeof a.title, "%s", r->title);
    page(TITLE);
    if (a.ready)
        whale_send(0x16, b, sizeof b);
}
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
static void key(uint32_t code) {
    unsigned k = code & 255;
    bool hold = (code >> 8) != 0;
    last_input = a.now;
    if (dim) {
        dim = false;
        bsp_display_backlight(75);
        return;
    }
    if (a.view == PAIR) {
        if (k == BSP_BTN_OK) {
            whale_pair(!hold);
            page(HOME);
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
        if (a.view == OPTION) {
            page(QUESTION);
            return;
        }
        page(HOME);
        return;
    }
    if (hold && k == BSP_BTN_UP && a.view == HOME) {
        open_title();
        return;
    }
    if (hold && k == BSP_BTN_DOWN && (a.view == HOME || reply_home())) {
        page(LIST);
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
                page(LIST);
            break;
        case LIST:
            enter_home();
            break;
        case LIST_FAILED:
            page(LIST_LOAD);
            snap(requested_cursor);
            break;
        case MANAGE:
            page(KEEP);
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
        case RESULT:
        case TITLE:
        case VERSION:
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
            open_title();
        return;
    }
    if (a.view == MANAGE) {
        page(VERSION);
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
    if (a.view == TITLE || a.view == PROMPT || a.view == OPTION) {
        a.scroll += d;
        int max = whale_scroll_max(a.view == TITLE    ? a.title
                                   : a.view == PROMPT ? a.prompt
                                                      : a.labels[a.focus]);
        if (a.scroll < 0)
            a.scroll = 0;
        if (a.scroll > max)
            a.scroll = max;
        return;
    }
    if (a.view == LIST) {
        int next = (int)a.selected + d;
        if (a.count && next >= 0 && next < a.count)
            a.selected = next;
        else if (!request_snap && a.ready && d == 1 && a.next != UINT32_MAX) {
            page(LIST_LOAD);
            snap(a.next);
        } else if (!request_snap && a.ready && d == -1 && a.cursor) {
            page(LIST_LOAD);
            snap(a.cursor >= 8 ? a.cursor - 8 : 0);
        }
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
        uint8_t b[11] = {2, 2};
        put32(b + 2, w32(p + 2) & 511);
        put16(b + 6, 20);
        put16(b + 8, 20);
        if (p[0] != 2 || p[1] != 2 || (w32(p + 2) & 63) != 63 || w16(p + 6) < 20 || w16(p + 8) < 20)
            b[10] = 1;
        a.linked = true;
        a.ready = false;
        whale_send(2, b, 11);
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
            (c != requested_cursor &&
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
        uint32_t old = a.count ? a.rows[a.selected].session : 0;
        bool resumed = !a.ready;
        bool new_epoch = a.epoch && a.epoch != stage.epoch;
        if (new_epoch)
            whale_reply_cache_clear();
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
        a.seq = stage.base;
        a.epoch = stage.epoch;
        a.cursor = stage.cursor;
        a.total = stage.total;
        a.next = stage.next;
        a.count = stage.count;
        a.ready = true;
        stage.active = false;
        request_snap = false;
        memset(a.replies, 0, sizeof a.replies);
        if (old && (!kept || new_epoch) &&
            (a.view == HOME || question_view() || a.view == RESULT || a.view == TITLE ||
             a.view == REPLY_READ || a.view == REPLY_LOAD || a.view == REPLY_FAILED)) {
            memset(a.identity, 0, sizeof a.identity);
            a.answer[0] = a.prompt[0] = a.title[0] = 0;
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
    if (msg.type == 0x32 && n == 5 && a.view == TITLE && p[0] == 0x16) {
        snprintf(a.title, sizeof a.title, "请在电脑查看全文");
        return;
    }
    if (msg.type == 0x32 && n == 5 && reply_home() && a.read_pending && p[0] == 0x1a) {
        reply_failed();
        return;
    }
    if (!a.ready || n < 8 || w64(p) != a.epoch)
        return;
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
    } else if (msg.type == 0x17 && n >= 18 && a.view == TITLE && a.count &&
               w32(p + 8) == a.rows[a.selected].session && w32(p + 12) == a.rows[a.selected].run &&
               w16(p + 16) == n - 18 && n - 18 <= 512 && whale_text(p + 18, n - 18)) {
        memcpy(a.title, p + 18, n - 18);
        a.title[n - 18] = 0;
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
static void battery_task(void *unused) {
    (void)unused;
    bool initialized = false;
    for (;;) {
        if (!initialized)
            initialized = bsp_battery_init() == ESP_OK;
        int soc = initialized ? bsp_battery_soc() : -1;
        whale_post(&(whale_event_t){.kind = 99, .value = (uint32_t)soc});
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
static void service(whale_event_t e) {
    dirty = true;
    switch (e.kind) {
    case WE_FRAME: {
        whale_msg_t m;
        uint8_t previous_part = rx.slots[0].next;
        uint16_t previous_id = rx.slots[0].id;
        int result = whale_receive(&rx, e.bytes, e.len, a.now, &m);
        if (result == 1)
            apply_message(m);
        else if (result < 0 && a.ready && !question_view())
            snap(a.cursor);
        if (result == 0 && a.view == LOADING && rx.slots[0].active &&
            (e.bytes[1] == 0x20 || e.bytes[1] == 0x21) &&
            (rx.slots[0].id != previous_id || rx.slots[0].next != previous_part)) {
            a.received = rx.slots[0].next;
            a.parts = rx.slots[0].parts;
            load_progress = a.now;
        }
        break;
    }
    case WE_LINK:
        a.linked = true;
        break;
    case WE_PAIR:
        a.passkey = e.value;
        page(PAIR);
        break;
    case WE_DOWN:
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
        if (ev == BSP_BTN_PRESS) {
            armed[k] = !dim;
            pressed_revision[k] = view_revision;
            last_input = a.now;
            if (dim) {
                dim = false;
                bsp_display_backlight(75);
            }
        } else if (armed[k] && (ev == BSP_BTN_LONG || ev == BSP_BTN_RELEASE)) {
            armed[k] = false;
            if (pressed_revision[k] == view_revision)
                key(k | ((ev == BSP_BTN_LONG) << 8));
        }
        break;
    }
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
    events = xQueueCreate(40, sizeof(whale_event_t));
    assert(events);
    ESP_ERROR_CHECK(bsp_display_init());
    ESP_ERROR_CHECK(bsp_lvgl_init() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(bsp_lvgl_lock(0) ? ESP_OK : ESP_FAIL);
    whale_ui_init();
    bsp_lvgl_unlock();
    ESP_ERROR_CHECK(bsp_button_init(button, NULL));
    whale_ble_start();
    ESP_LOGI("whale", "R10.1 / protocol 2.2; minimum free heap: %lu",
             (unsigned long)esp_get_minimum_free_heap_size());
    xTaskCreate(battery_task, "whale_soc", 3072, NULL, 2, NULL);
    last_input = ms();
    bsp_display_backlight(75);
    for (;;) {
        whale_event_t e;
        bool have = xQueueReceive(events, &e, pdMS_TO_TICKS(40)) == pdTRUE;
        a.now = ms();
        if (have)
            service(e);
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
                memset(&a, 0, sizeof a);
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
        bool busy = false;
        for (int i = 0; i < a.count; i++)
            if ((a.rows[i].state >= 2 && a.rows[i].state <= 4) || a.rows[i].question)
                busy = true;
        if (a.view != HOME && a.view != LIST)
            busy = true;
        if (busy)
            last_input = a.now;
        if (busy && dim) {
            dim = false;
            bsp_display_backlight(75);
        }
        if (!busy && !dim && a.now - last_input >= 120000) {
            dim = true;
            bsp_display_backlight(18);
        }
        if ((dirty && a.now - last_draw >= 60) || a.now - last_draw >= 250) {
            if (bsp_lvgl_lock(25)) {
                whale_ui_draw(&a);
                bsp_lvgl_unlock();
                dirty = false;
                last_draw = a.now;
            }
        }
    }
}
