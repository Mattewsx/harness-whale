#include "../main/whale_app.c"
#include <stdlib.h>

static uint8_t sent_type, sent_bytes[64];
static size_t sent_length;
static unsigned sends, answers, voice_starts;
static bool fake_voice_busy, fake_voice_pending;
static unsigned cue_count, audio_plays, sleep_commands;
static bool slow_adv_set;
static int backlight_level = -1;
static bool cue_ok, bad_snapshot_crc;
static bool send_ok = true;
static const char *stored_room_key;
static uint8_t stored_room[11];
static size_t stored_room_size;
void *xQueueCreate(int x, int y) {
    (void)x;
    (void)y;
    return (void *)1;
}
int xQueueReceive(void *q, void *out, int wait) {
    (void)q;
    (void)out;
    (void)wait;
    return 0;
}
static whale_audio_cmd_t *last_cue_capture;
int xQueueSend(void *q, const void *in, int wait) {
    if (q == cues) {
        if (*(const uint8_t *)in == WHALE_CMD_ROOM_SAVE) {
            if (last_cue_capture)
                *last_cue_capture = *(const whale_audio_cmd_t *)in;
            return 1;
        }
        assert(wait == 0 && ((*(const uint8_t *)in >= 1 && *(const uint8_t *)in <= 7) ||
                             *(const uint8_t *)in == WHALE_CMD_STANDBY_SAVE));
        if (*(const uint8_t *)in == WHALE_CMD_SLEEP)
            sleep_commands++;
        if (!cue_ok)
            return 0;
        cue_count++;
    }
    (void)q;
    (void)in;
    (void)wait;
    return 1;
}
int bsp_battery_init(void) { return 0; }
int bsp_battery_soc(void) { return 96; }
int bsp_display_init(void) { return 0; }
void *bsp_lvgl_init(void) { return (void *)1; }
bool bsp_lvgl_lock(int wait) {
    (void)wait;
    return true;
}
void bsp_lvgl_unlock(void) {}
void bsp_display_backlight(int percent) { backlight_level = percent; }
int bsp_button_init(void (*cb)(int, int, void *), void *ctx) {
    (void)cb;
    (void)ctx;
    return 0;
}
void xTaskCreate(void (*fn)(void *), const char *name, int size, void *arg, int priority,
                 void *handle) {
    (void)fn;
    (void)name;
    (void)size;
    (void)arg;
    (void)priority;
    (void)handle;
}
void vTaskDelay(int ticks) { (void)ticks; }
TaskHandle_t xTaskGetHandle(const char *name) {
    (void)name;
    return NULL;
}
unsigned uxTaskGetStackHighWaterMark(void *task) {
    (void)task;
    return 4096;
}
int nvs_flash_init(void) { return 0; }
int64_t esp_timer_get_time(void) { return 0; }
void whale_pair(bool yes) { (void)yes; }
bool whale_unbind(void) { return true; }
void whale_ble_start(void) {}
static uint16_t frame_limit = WHALE_FRAME_LEGACY, tx_frame = WHALE_FRAME_LEGACY,
                hello_frame = WHALE_FRAME_LEGACY;
static uint8_t frame_ring[WHALE_FRAME_MAX];
static unsigned frame_releases;
static uint8_t hello_reply[11];
const uint8_t *whale_ble_frame(uint32_t index) {
    (void)index;
    return frame_ring;
}
void whale_ble_frame_release(void) { frame_releases++; }
uint16_t whale_ble_frame_limit(void) { return frame_limit; }
void whale_ble_set_frame(uint16_t frame) { tx_frame = frame; }
void whale_ui_init(void) {}
bool whale_audio_done(void) {
    audio_plays++;
    return true;
}
void whale_ui_draw(const whale_app_t *app) { (void)app; }
bool whale_displayable(const char *s) {
    (void)s;
    return true;
}
bool whale_text_wide(const char *s) { return strlen(s) > 30; }
int whale_scroll_max(const char *s) {
    (void)s;
    return 0;
}
int whale_home_scroll_max(const char *s) {
    int lines = *s != 0;
    while (*s)
        if (*s++ == '\n')
            lines++;
    return lines > HOME_REPLY_LINES ? lines - HOME_REPLY_LINES : 0;
}
size_t whale_line_offset(const char *s, int line) {
    const char *start = s;
    while (*s && line > 0)
        if (*s++ == '\n')
            line--;
    return s - start;
}
int whale_scroll_at(const char *s, size_t offset) {
    int line = 0;
    for (size_t i = 0; s[i] && i < offset; i++)
        if (s[i] == '\n')
            line++;
    return line;
}
bool whale_send(uint8_t type, const uint8_t *p, size_t n) {
    if (type == 2) {
        hello_frame = tx_frame; // the hello reply must leave in a legacy frame
        assert(n == sizeof hello_reply);
        memcpy(hello_reply, p, n);
    }
    sent_type = type;
    sent_length = n;
    sends++;
    if (type == 0x22)
        answers++;
    assert(n <= sizeof sent_bytes);
    memcpy(sent_bytes, p, n);
    return send_ok;
}
static void setup(void) {
    room_unsaved = previous_window = false;
    stored_room_key = NULL;
    memset(&reminders, 0, sizeof reminders);
    disconnect_pending = false;
    seek_session = 0;
    seek_to_list = priority_list = false;
    whale_reply_cache_clear();
    memset(sounded, 0, sizeof sounded);
    sounded_next = cue_count = audio_plays = 0;
    cues = (void *)2;
    cue_ok = true;
    bad_snapshot_crc = false;
    a = (whale_app_t){.dock = 1,
                      .bond = 12,
                      .soc = -1,
                      .next = UINT32_MAX,
                      .count = 2,
                      .total = 2,
                      .linked = true,
                      .ready = true,
                      .epoch = 123,
                      .now = 1000};
    a.rows[0] = (whale_row_t){.session = 10, .run = 20, .state = 2};
    a.rows[1] = (whale_row_t){.session = 11, .run = 21, .state = 5};
    strcpy(a.rows[0].title, "same name");
    strcpy(a.rows[1].title, "same name");
    a.replies[0].session = 10;
    a.replies[0].revision = 30;
    a.replies[1].session = 11;
    a.replies[1].revision = 31;
    memset(&stage, 0, sizeof stage);
    memset(&rx, 0, sizeof rx);
    memset(armed, 0, sizeof armed);
    dim = request_snap = false;
    screen_applied = WHALE_SCREEN_ON;
    key_guard_until = 0;
    whale_power_init(&power, 1000, false, 0);
    requested_cursor = 0;
    sends = answers = 0;
    voice_starts = 0;
    fake_voice_busy = fake_voice_pending = false;
    memset(pending_reads, 0, sizeof pending_reads);
    pending_read_next = 0;
    send_ok = true;
    *whale_sleep_clock() = (whale_clock_t){0};
}
static void reply_count(uint32_t sid, uint32_t rev, uint32_t part, uint32_t count,
                        const char *text) {
    uint8_t p[410] = {0};
    put64(p, a.epoch);
    put32(p + 8, sid);
    put32(p + 12, rev);
    put32(p + 16, part);
    put32(p + 20, count);
    put16(p + 24, strlen(text));
    memcpy(p + 26, text, strlen(text));
    apply_message((whale_msg_t){0x1b, p, 26 + strlen(text)});
}
static void reply(uint32_t sid, uint32_t rev, uint32_t part, const char *text) {
    reply_count(sid, rev, part, 2, text);
}
static void preview(uint32_t sid, uint32_t rev) {
    uint8_t p[20] = {0};
    put64(p, a.epoch);
    put32(p + 8, sid);
    put32(p + 12, rev);
    p[16] = 3;
    memcpy(p + 17, "new", 3);
    apply_message((whale_msg_t){0x19, p, sizeof p});
}
static void snapshot_epoch(bool keep_first, uint64_t epoch) {
    uint8_t begin[27] = {0}, row[37] = {0}, end[17] = {0};
    put64(begin, epoch);
    put32(begin + 8, 1);
    put32(begin + 20, UINT32_MAX);
    put16(begin + 24, 1);
    begin[26] = 1;
    uint32_t crc = whale_crc(0, begin, sizeof begin);
    apply_message((whale_msg_t){0x11, begin, sizeof begin});
    put64(row, epoch);
    put32(row + 8, 1);
    put32(row + 13, keep_first ? 10 : 11);
    put32(row + 17, keep_first ? 20 : 21);
    row[21] = 5;
    crc = whale_crc(crc, row + 13, 24);
    apply_message((whale_msg_t){0x12, row, sizeof row});
    put64(end, epoch);
    put32(end + 8, 1);
    end[12] = 1;
    put32(end + 13, bad_snapshot_crc ? crc ^ 1 : crc);
    apply_message((whale_msg_t){0x13, end, sizeof end});
}
static void snapshot(bool keep_first) { snapshot_epoch(keep_first, a.epoch); }
static void test_detail_and_reading(void) {
    setup();
    preview(10, 30);
    assert(a.view == TASK_DETAIL && sends == 0);
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_LOAD && sent_type == 0x1a && w32(sent_bytes + 8) == 10);
    reply(11, 30, 0, "wrong session");
    reply(10, 29, 0, "old reply");
    assert(a.view == REPLY_LOAD);
    reply(10, 30, 0, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13");
    assert(a.view == REPLY_READ && a.scroll == 0);
    for (unsigned i = 0; i < 7; i++)
        key(BSP_BTN_DOWN);
    assert(a.scroll == 7);
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_READ && a.read_pending && a.read_page == 1);
    reply(10, 30, 1, "last");
    assert(a.read_used == 2 && strstr(a.answer, "13last"));
    unsigned before = sends;
    while (a.scroll)
        key(BSP_BTN_UP);
    assert(a.scroll == 0);
    key(BSP_BTN_UP);
    assert(a.view == TASK_DETAIL);
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_READ && sends == before);
    key(BSP_BTN_OK);
    assert(a.view == TASK_DETAIL);
    reply(10, 30, 0, "late");
    assert(a.view == TASK_DETAIL);
}
static void test_navigation_and_questions(void) {
    setup();
    page(HOME);
    key(BSP_BTN_OK);
    assert(a.view == LIST);
    key(BSP_BTN_DOWN);
    assert(a.selected == 1);
    key(BSP_BTN_OK);
    assert(a.view == TASK_DETAIL && sends == 0);
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_LOAD && w32(sent_bytes + 8) == 11);
    key(BSP_BTN_OK);
    key(BSP_BTN_UP);
    assert(a.view == LIST && a.selected == 1);
    key(BSP_BTN_OK);
    a.rows[1].question = 40;
    key(BSP_BTN_DOWN | 256);
    assert(a.view == LOADING && sent_type == 0x24);
    key(BSP_BTN_OK);
    assert(answers == 0);
    a.meta = true;
    a.options = 2;
    a.option_ids[0] = 3;
    strcpy(a.labels[0], "yes");
    page(QUESTION);
    key(BSP_BTN_OK);
    assert(a.view == SENDING && answers == 1);
    key(BSP_BTN_OK);
    assert(answers == 1);
    service((whale_event_t){.kind = WE_DOWN});
    assert(a.view == CHECKING);
}
static void test_new_reply_and_lifecycle(void) {
    setup();
    key(BSP_BTN_DOWN);
    reply(10, 30, 0, "reading");
    preview(10, 32);
    assert(a.view == REPLY_LOAD && a.read_revision == 32);
    assert(!a.answer[0]);
    reply(10, 30, 0, "stale");
    assert(a.view == REPLY_LOAD);
    reply(10, 32, 0, "updated");
    assert(!strcmp(a.answer, "updated"));
    snapshot(true);
    assert(a.view == REPLY_READ && a.count == 1);
    preview(10, 32);
    assert(a.view == REPLY_READ);
    snapshot(false);
    assert(a.view == LIST && a.count == 1 && a.rows[0].session == 11 && !a.answer[0]);
    reply(10, 32, 1, "removed");
    assert(a.view == LIST);
    setup();
    snapshot(false);
    assert(a.view == LIST);
    setup();
    key(BSP_BTN_DOWN);
    reply(10, 30, 0, "latest");
    preview(10, 0);
    assert(a.view == TASK_DETAIL && !a.answer[0]);
}
static void test_failure_empty_battery_and_key_release(void) {
    setup();
    a.replies[0].revision = 0;
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_READ && !a.read_revision && sends == 0);
    key(BSP_BTN_OK);
    preview(10, 30);
    send_ok = false;
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_FAILED);
    send_ok = true;
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_LOAD);
    service((whale_event_t){.kind = WE_DOWN});
    key(BSP_BTN_OK);
    assert(a.view == TASK_DETAIL);
    service((whale_event_t){.kind = 99, .value = 96});
    assert(a.soc == 96 && !a.soc_stale);
    service((whale_event_t){.kind = 99, .value = 41});
    assert(a.soc == 41);
    service((whale_event_t){.kind = 99, .value = UINT32_MAX});
    assert(a.soc == 41 && a.soc_stale);
    service((whale_event_t){.kind = 99, .value = 4});
    assert(a.battery_level == 2);
    service((whale_event_t){.kind = 99, .value = 8});
    assert(a.battery_level == 1);
    service((whale_event_t){.kind = 99, .value = 25});
    assert(a.battery_level == 0);
    setup();
    service((whale_event_t){.kind = WE_KEY, .value = BSP_BTN_DOWN | (BSP_BTN_PRESS << 8)});
    page(LIST);
    service((whale_event_t){.kind = WE_KEY, .value = BSP_BTN_DOWN | (BSP_BTN_RELEASE << 8)});
    assert(a.selected == 0);
    dim = true;
    service((whale_event_t){.kind = WE_KEY, .value = BSP_BTN_DOWN | (BSP_BTN_PRESS << 8)});
    service((whale_event_t){.kind = WE_KEY, .value = BSP_BTN_DOWN | (BSP_BTN_RELEASE << 8)});
    assert(!dim && a.selected == 0);
}
static void test_continuous_cache_and_offline(void) {
    setup();
    key(BSP_BTN_DOWN);
    const char *text = "1\n2\n3\n4\n5\n6\n7\n8\n";
    reply_count(10, 30, 0, 5, text);
    for (unsigned part = 1; part < 5; part++) {
        for (unsigned i = 0; i < 40 && !a.read_pending; i++)
            key(BSP_BTN_DOWN);
        assert(a.read_pending && a.read_page == part && a.view == REPLY_READ);
        reply_count(10, 30, part, 5, text);
        assert(!a.read_pending && a.view == REPLY_READ);
    }
    assert(a.read_used == 3 && a.read_first == 2);
    unsigned before = sends;
    for (unsigned i = 0; i < 100 && a.view == REPLY_READ; i++)
        key(BSP_BTN_UP);
    assert(a.view == TASK_DETAIL && sends == before);
    service((whale_event_t){.kind = WE_DOWN});
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_READ && sends == before);
    /* Retain opening text even when a long reply fills the bounded cache. */
    for (unsigned i = 5; i < 80; i++)
        whale_reply_cache_put(a.epoch, 10, 30, i, 80, (const uint8_t *)"body", 4);
    char cached[385];
    uint32_t count;
    assert(whale_reply_cache_get(a.epoch, 10, 30, 0, cached, &count));
    assert(!whale_reply_cache_get(a.epoch + 1, 10, 30, 0, cached, &count));
    whale_reply_cache_forget(10, 31);
    assert(!whale_reply_cache_get(a.epoch, 10, 30, 0, cached, &count));
}
static void row_update(uint32_t sid, uint32_t run, uint8_t state, uint32_t seq) {
    uint8_t p[37] = {0};
    put64(p, a.epoch);
    put32(p + 8, seq);
    put32(p + 13, sid);
    put32(p + 17, run);
    p[21] = state;
    apply_message((whale_msg_t){0x14, p, sizeof p});
}
static void test_completion_cues(void) {
    setup();
    a.selected = 1; /* Completion of a background task still announces. */
    row_update(10, 20, 5, 1);
    assert(cue_count == 1 && audio_plays == 0 && a.rows[0].state == 5);
    row_update(10, 20, 5, 1); /* duplicate sequence */
    row_update(10, 20, 5, 2); /* repeat status */
    row_update(10, 20, 3, 3);
    row_update(10, 20, 5, 4); /* late state must not ring twice for one run */
    assert(cue_count == 1);
    row_update(10, 21, 2, 5);
    row_update(10, 21, 5, 6);
    assert(cue_count == 2);
    setup();
    row_update(10, 20, 6, 1);
    row_update(10, 20, 5, 2);
    assert(cue_count == 0);
    setup();
    row_update(10, 20, 7, 1);
    assert(cue_count == 0);
    setup();
    row_update(10, 21, 5, 1); /* unseen run/history restoration */
    assert(cue_count == 0);
    setup();
    row_update(10, 20, 5, 2); /* sequence gap requests snapshot */
    assert(cue_count == 0 && request_snap);
    setup();
    snapshot(true);
    assert(cue_count == 1);
    snapshot(true);
    assert(cue_count == 1);
    setup();
    a.ready = false; /* initial connection and reconnect are silent */
    snapshot(true);
    assert(cue_count == 0);
    setup();
    bad_snapshot_crc = true;
    snapshot(true);
    assert(cue_count == 0);
    setup();
    cue_ok = false;
    row_update(10, 20, 5, 1);
    assert(cue_count == 0 && a.rows[0].state == 5); /* failure does not block UI */
}
static void test_voice_and_attention(void) {
    setup();
    a.voice_cap = true;
    key(BSP_BTN_OK);
    assert(a.view == VOICE && a.voice_phase == VOICE_PREPARE && sent_type == 0x40);
    assert(voice_starts == 0);
    uint8_t ready[27] = {0};
    memcpy(ready, a.voice_identity, 24);
    ready[24] = 1;
    apply_message((whale_msg_t){0x41, ready, sizeof ready});
    assert(voice_starts == 1); /* no Confirm needed after long Up */
    whale_event_t e = {.kind = WE_VOICE, .len = 16, .value = 1};
    memcpy(e.bytes, a.voice_identity + 8, 16);
    service(e);
    assert(a.voice_phase == VOICE_RECORD);
    key(BSP_BTN_OK);
    assert(a.voice_phase == VOICE_FINISH);
    uint8_t b[32];
    memcpy(b, a.voice_identity, 24);
    b[24] = 2;
    put16(b + 25, 5);
    memcpy(b + 27, "hello", 5);
    unsigned before = sends;
    apply_message((whale_msg_t){0x41, b, 32});
    assert(a.voice_phase == VOICE_ACTIONS && sends == before && !strcmp(a.voice_text, "hello"));
    assert(a.voice_phase == VOICE_ACTIONS && sends == before);
    key(BSP_BTN_OK);
    assert(a.voice_phase == VOICE_SENDING && sent_type == 0x45);
    service((whale_event_t){.kind = WE_DOWN});
    assert(a.voice_phase == VOICE_UNKNOWN);
    a.ready = a.linked = true;
    key(BSP_BTN_OK);
    assert(sent_type == 0x46);
    b[24] = 3;
    put16(b + 25, 0);
    apply_message((whale_msg_t){0x41, b, 27});
    assert(a.voice_phase == VOICE_SENT);
    key(BSP_BTN_OK);
    assert(a.view == TASK_DETAIL);
    setup();
    a.rows[0].flags = 24;
    a.rows[0].state = 6;
    page(LIST);
    key(BSP_BTN_OK);
    assert(a.rows[0].flags == 24);
    acknowledge_visible();
    assert(a.view == REPLY_LOAD && a.rows[0].flags == 24);
    reply(10, 30, 0, "read this");
    acknowledge_visible();
    assert(a.rows[0].flags == 16 && sent_type == 0x4a && sent_bytes[16] == 1);
    key(BSP_BTN_OK);
    acknowledge_visible();
    assert(a.view == TASK_DETAIL && !a.rows[0].flags && sent_bytes[16] == 2);
    page(LIST);
    key(BSP_BTN_OK);
    assert(a.view == TASK_DETAIL);
    a.rows[0].flags |= 8;
    page(TASK_DETAIL);
    a.ready = false;
    before = sends;
    key(BSP_BTN_DOWN);
    acknowledge_visible();
    assert(a.view == REPLY_READ && !(a.rows[0].flags & 8) && sends == before);
    a.ready = true;
    flush_reads();
    assert(sends == before + 1 && sent_type == 0x4a);
}
static void test_unread_route_before_preview(void) {
    setup();
    uint8_t update[37] = {0};
    put64(update, a.epoch);
    put32(update + 8, 1);
    put32(update + 13, 10);
    put32(update + 17, 20);
    update[21] = 5;
    update[23] = 8;
    apply_message((whale_msg_t){0x14, update, sizeof update});
    assert(a.rows[0].flags == 8 && !a.replies[0].revision);
    page(LIST);
    key(BSP_BTN_OK);
    assert(a.view == REPLY_LOAD && a.read_revision == 0 && sends == 0);
    acknowledge_visible();
    assert(a.rows[0].flags == 8);
    preview(11, 31);
    assert(sends == 0);
    reply_count(10, 0, 0, 1, "stale");
    assert(a.view == REPLY_LOAD && !a.read_used);
    preview(10, 33);
    assert(a.view == REPLY_LOAD && sent_type == 0x1a && a.read_revision == 33);
    key(BSP_BTN_OK);
    reply_count(10, 33, 0, 1, "late");
    acknowledge_visible();
    assert(a.view == TASK_DETAIL && a.rows[0].flags == 8);
    a.rows[0].flags |= 32;
    page(LIST);
    key(BSP_BTN_OK);
    assert(a.view == TASK_DETAIL && !answers); /* Computer-only question. */
    a.rows[0].flags = 8;
    a.rows[0].question = 42;
    page(LIST);
    key(BSP_BTN_OK);
    assert(a.view == LOADING && sent_type == 0x24 && !answers);
}
static void test_voice_direct_entry(void) {
    setup();
    a.voice_cap = true;
    service((whale_event_t){.kind = WE_KEY, .value = BSP_BTN_OK | (BSP_BTN_PRESS << 8)});
    assert(a.view == TASK_DETAIL && sends == 0);
    service((whale_event_t){.kind = WE_KEY, .value = BSP_BTN_OK | (BSP_BTN_RELEASE << 8)});
    assert(a.view == VOICE && a.voice_phase == VOICE_PREPARE && sends == 1);
    service((whale_event_t){.kind = WE_KEY, .value = BSP_BTN_OK | (BSP_BTN_RELEASE << 8)});
    assert(a.view == VOICE && a.voice_phase == VOICE_PREPARE && sends == 1);
    uint8_t ready[27] = {0};
    memcpy(ready, a.voice_identity, 24);
    ready[24] = 1;
    key(BSP_BTN_UP); /* cancellation before readiness must never open the mic */
    apply_message((whale_msg_t){0x41, ready, sizeof ready});
    assert(a.view == TASK_DETAIL && voice_starts == 0);
    setup();
    a.ready = false;
    key(BSP_BTN_OK);
    assert(a.voice_phase == VOICE_DISCONNECTED && voice_starts == 0 && sends == 0);
    setup();
    key(BSP_BTN_OK);
    assert(a.voice_phase == VOICE_UNSUPPORTED && voice_starts == 0 && sends == 0);
    setup();
    a.voice_cap = true;
    a.rows[0].question = 40;
    key(BSP_BTN_OK);
    assert(a.voice_phase == VOICE_PREPARE && voice_starts == 0 && sends == 1);
    setup();
    a.count = 0;
    key(BSP_BTN_OK);
    assert(a.view == TASK_DETAIL && voice_starts == 0 && sends == 0);
    setup();
    a.attention_cap = true;
    key(BSP_BTN_UP);
    assert(a.view == LIST_LOAD && sent_type == 0x10 && w32(sent_bytes + 8) == 0);
    key(BSP_BTN_OK); /* old cached rows cannot be selected during a priority
                        refresh */
    assert(a.view == LIST_LOAD);
    /* Keep priority order while highlighting the current task by identity. */
    memset(&stage, 0, sizeof stage);
    stage.active = true;
    stage.epoch = a.epoch;
    stage.snap = stage.count = stage.taken = stage.total = 2;
    stage.next = UINT32_MAX;
    stage.rows[0] = a.rows[1];
    stage.rows[1] = a.rows[0];
    uint8_t end[17] = {0};
    put64(end, a.epoch);
    put32(end + 8, 2);
    end[12] = 2;
    apply_message((whale_msg_t){0x13, end, sizeof end});
    assert(a.view == LIST && a.selected == 1 && a.rows[0].session == 11);
    setup();
    a.attention_cap = true;
    a.linked = false;
    key(BSP_BTN_UP);
    assert(a.view == LIST && sends == 0);
}
static void summary(unsigned running, unsigned waiting, unsigned unread) {
    uint8_t p[25] = {0};
    put64(p, a.epoch);
    put32(p + 8, reminders.seq);
    put16(p + 12, 12);
    put16(p + 14, running);
    put16(p + 16, waiting);
    put16(p + 18, unread);
    put32(p + 20, 10);
    p[24] = 1;
    apply_message((whale_msg_t){0x50, p, sizeof p});
}
static void cue_event(unsigned seq, unsigned sid, unsigned token, unsigned valid,
                      unsigned trigger) {
    uint8_t p[26] = {0};
    put64(p, a.epoch);
    put32(p + 8, seq);
    put32(p + 12, sid);
    put32(p + 16, 20);
    put32(p + 20, token);
    p[24] = valid;
    p[25] = trigger;
    apply_message((whale_msg_t){0x51, p, sizeof p});
}
static void test_companion_and_global_cues(void) {
    setup();
    a.global_cap = true;
    last_input = 0;
    a.now = 120001;
    summary(1, 0, 0);
    companion_tick();
    assert(a.view == TASK_DETAIL); /* background session blocks auto */
    summary(0, 0, 1);
    companion_tick();
    assert(a.view == TASK_DETAIL);
    summary(0, 0, 0);
    companion_tick();
    assert(a.view == DESK && last_input == 0); /* idle home becomes the desk display */
    key(BSP_BTN_UP);
    assert(a.view == HOME);
    enter_companion();
    for (unsigned i = 0; i < 4; i++) {
        a.companion_choice = i;
        key(BSP_BTN_OK);
        assert(a.companion_action == i + 1);
        uint32_t start = a.action_at;
        key(BSP_BTN_OK);
        assert(a.action_at == start);
        a.now += 3000;
        companion_tick();
        assert(!a.companion_action);
    }
    a.companion_choice = 3;
    key(BSP_BTN_OK);
    key(BSP_BTN_OK | 256);
    assert(a.view == HOME && !a.companion_action);
    a.dock = 0;
    key(BSP_BTN_OK);
    assert(a.view == COMPANION);
    cue_event(1, 10, 10, 0, 5);
    assert(a.view == COMPANION); /* manual room interaction stays open */
    enter_desk();
    cue_event(2, 10, 20, 1, 1);
    assert(a.view == LIST); /* no unread cleared */
    assert(reminders.pending[15].kind == 1);
    a.now += 1000;
    companion_tick();
    assert(cue_count == 1);
    cue_event(2, 10, 20, 1, 1);
    a.now += 1000;
    companion_tick();
    assert(cue_count == 1);
    cue_event(3, 10, 21, 3, 3);
    cue_event(4, 11, 22, 2, 2);
    cue_event(5, 12, 23, 1, 1);
    a.now += 1000;
    companion_tick();
    assert(cue_count == 2); /* one merged cue */
    a.view = VOICE;
    a.voice_phase = VOICE_RECORD;
    cue_event(6, 10, 24, 3, 3);
    a.now += 2000;
    companion_tick();
    assert(cue_count == 2);
    cue_event(7, 10, 0, 0, 0);
    a.voice_phase = VOICE_PREVIEW;
    companion_tick();
    assert(cue_count == 2);
    cue_event(8, 11, 25, 2, 2);
    a.voice_phase = VOICE_RECORD;
    a.now += 1000;
    companion_tick();
    assert(cue_count == 2);
    a.voice_phase = VOICE_PREVIEW;
    companion_tick();
    assert(cue_count == 3);
    setup();
    a.now = 120001;
    last_input = 0;
    companion_tick();
    assert(a.view == TASK_DETAIL); /* old bridge */
    a.linked = false;
    companion_tick();
    assert(a.view == DESK); /* offline allowed */
    a.view = REPLY_READ;
    a.now += 120000;
    companion_tick();
    assert(a.view == REPLY_READ);
    setup();
    service((whale_event_t){.kind = WE_DOWN});
    a.now += 4999;
    companion_tick();
    assert(cue_count == 0);
    a.now++;
    companion_tick();
    assert(cue_count == 0);
    a.now += 1000;
    companion_tick();
    assert(cue_count == 1);
    a.now += 20000;
    companion_tick();
    assert(cue_count == 1);
    setup();
    page(HOME);
    a.dock = 2;
    key(BSP_BTN_OK);
    assert(a.view == MANAGE);
    key(BSP_BTN_OK);
    assert(a.view == SOUND);
    a.sound_choice = 1;
    key(BSP_BTN_OK);
    assert(a.sound_mode == 1 && a.sound_saved == 0);
    service((whale_event_t){.kind = 98, .value = 1});
    assert(a.sound_saved == 1);
    assert(a.view == MANAGE);
    key(BSP_BTN_UP | 256);
    assert(a.view == HOME && a.dock == 1);
    puts("Companion actions, global idle, interruption, deferred/merged cues and "
         "settings PASS");
}
static void invitation_room(bool linked) {
    setup();
    page(HOME);
    a.linked = linked;
    a.global_cap = true;
    a.global_valid = true;
    a.global_at = last_input = a.now;
}
static void start_random_invitation(void) {
    companion_tick();
    assert(a.invitation_scheduled && !a.invitation);
    uint32_t delay = a.invitation_due - a.now;
    assert(delay >= 45000 && delay <= 75000);
    a.now = a.invitation_due;
    a.global_at = a.now;
    companion_tick();
    assert(a.invitation >= 1 && a.invitation <= 4 && !a.invitation_settling);
}
static void test_random_invitations(void) {
    invitation_room(true);
    uint32_t input = last_input;
    start_random_invitation();
    uint8_t first = a.invitation;
    uint32_t cooldown = a.invitation_next - a.now;
    assert(cooldown >= 480000 && cooldown <= 900000);
    assert(a.dock == 1 && a.bond == 12 && last_input == input);
    a.now += WHALE_INVITATION_MS;
    a.global_at = a.now;
    companion_tick();
    assert(a.invitation_settling && !a.bond_today && !a.bond_taps);
    a.now += WHALE_INVITATION_END_MS;
    a.global_at = a.now;
    companion_tick();
    assert(!a.invitation && a.bond == 12 && last_input == input && !cue_count);
    companion_tick();
    assert(!a.invitation_scheduled); /* quiet cooldown, including after ignore */
    a.now = a.invitation_next;
    last_input = a.global_at = a.now;
    start_random_invitation();
    assert(a.invitation != first);
    invitation_room(true);
    a.mood = 2; /* work completion can leave her happy without blocking a later offer */
    a.mood_at = a.now;
    companion_tick();
    assert(a.invitation_scheduled);
    invitation_room(false);
    enter_companion();
    key(BSP_BTN_OK);
    page(HOME);
    companion_tick();
    assert(a.invitation_cooling && !a.invitation_scheduled);
    a.now += 480000;
    last_input = a.now;
    start_random_invitation();
    for (unsigned action = 1; action <= 4; action++) {
        invitation_room(false); /* manual acceptance works offline too */
        a.invitation = action;
        a.invitation_at = a.now;
        key(BSP_BTN_UP);
        assert(a.view == HOME && a.dock == 0);
        key(BSP_BTN_OK);
        assert(a.view == COMPANION && a.companion_choice == action - 1 && a.invitation);
        assert(a.bond == 12);
        key(BSP_BTN_OK);
        assert(!a.invitation && a.companion_invited && a.companion_action == action);
        assert(a.bond == 13 && a.bond_taps == 1 && !cue_count);
        key(BSP_BTN_OK);
        assert(a.bond == 13); /* repeated confirm cannot stack the action or reward */
        a.now += 3000;
        companion_tick();
        assert(!a.companion_invited && !a.companion_action);
    }
    invitation_room(true);
    a.invitation = 4;
    a.invitation_at = a.now;
    enter_companion();
    a.companion_choice = 1;
    key(BSP_BTN_OK);
    assert(!a.invitation && !a.companion_invited && a.companion_action == 2);
    /* Work anywhere, including outside the current list page, cancels the invitation. */
    for (unsigned kind = 0; kind < 3; kind++) {
        invitation_room(true);
        a.invitation = 1;
        summary(kind == 0, kind == 1, kind == 2);
        assert(!a.invitation && a.view == HOME && a.global_unread == (kind == 2));
    }
    invitation_room(true);
    a.invitation = 2;
    cue_event(1, 10, 20, 1, 1);
    assert(!a.invitation);
    invitation_room(true);
    a.invitation = 3;
    apply_screen(WHALE_SCREEN_DIM);
    assert(!a.invitation);
    companion_tick();
    assert(!a.invitation_scheduled && dim);
    invitation_room(true);
    a.invitation = 4;
    fake_voice_busy = true;
    companion_tick();
    assert(!a.invitation && !a.invitation_scheduled);
    invitation_room(true);
    a.invitation = 4;
    fake_voice_pending = true;
    companion_tick();
    assert(!a.invitation);
    invitation_room(true);
    a.night = true;
    companion_tick();
    assert(!a.invitation_scheduled);
    invitation_room(true);
    a.global_cap = false; /* a connected old bridge cannot prove global idle */
    companion_tick();
    assert(!a.invitation_scheduled);
    invitation_room(true);
    a.global_at = a.now - 30000;
    companion_tick();
    assert(!a.invitation_scheduled);
    const whale_page_t protected[] = {REPLY_READ, QUESTION, VOICE, WARDROBE, MANAGE, COMPANION};
    for (unsigned i = 0; i < sizeof protected / sizeof protected[0]; i++) {
        invitation_room(true);
        page(protected[i]);
        companion_tick();
        assert(!a.invitation_scheduled && !a.invitation);
    }
    invitation_room(false);
    start_random_invitation();
    a.now = last_input + WHALE_DESK_IDLE_MS;
    companion_tick();
    assert(a.view == DESK && !a.invitation && last_input == 1000);
    invitation_room(true);
    a.now = UINT32_MAX - 30000;
    a.global_at = last_input = a.now;
    start_random_invitation(); /* deadlines also work across the millis wrap */
    assert(a.invitation);
    puts("Random invitations: acceptance, ignore, cooldown, work, power, voice and wrap PASS");
}
static void key_event(unsigned k, unsigned ev) {
    service((whale_event_t){.kind = WE_KEY, .value = k | (ev << 8)});
}
static void wall_clock(uint64_t epoch, int64_t unix_s, int tz_min) {
    uint8_t p[18];
    put64(p, epoch);
    put64(p + 8, (uint64_t)unix_s);
    put16(p + 16, (uint16_t)(int16_t)tz_min);
    apply_message((whale_msg_t){0x53, p, sizeof p});
}
static void power_reset(bool linked);
static void test_desk_display(void) {
    /* 2026-11-06 14:30:00 UTC: 22:30 in UTC+8, 23:30 in UTC+9. */
    const int64_t afternoon = 1793975400;
    setup();
    a.global_cap = true;
    a.rows[0].state = 1;
    wall_clock(a.epoch + 1, afternoon, 480); /* another bridge epoch */
    wall_clock(a.epoch, 1000, 480);          /* implausible */
    wall_clock(a.epoch, afternoon, 900);
    assert(!a.clock_valid);
    wall_clock(a.epoch, afternoon, 480);
    assert(a.clock_valid && a.clock.hour == 22 && a.clock.minute == 30 && a.clock.month == 11 &&
           a.clock.day == 6 && a.clock.wday == 5 && !a.night);
    /* Not before 120 s, and not while work is running, waiting or unread. */
    last_input = a.now;
    a.now += WHALE_DESK_IDLE_MS - 1;
    summary(0, 0, 0);
    companion_tick();
    assert(a.view == TASK_DETAIL);
    a.now++;
    summary(0, 1, 0);
    companion_tick();
    assert(a.view == TASK_DETAIL);
    summary(0, 0, 0);
    companion_tick();
    assert(a.view == DESK);
    /* Daytime and connected: the normal dim and screen-off still apply. */
    a.now += WHALE_POWER_DIM_MS;
    power_step();
    assert(screen_applied == WHALE_SCREEN_DIM && a.view == DESK);
    a.now += WHALE_POWER_OFF_MS;
    power_step();
    assert(screen_applied == WHALE_SCREEN_OFF && a.view == DESK && sleep_commands == 0);
    /* A session starting work returns to its task home. */
    cue_event(1, 11, 21, 0, 5);
    assert(a.view == TASK_DETAIL && a.selected == 1);
    /* Any summary that shows work also leaves, not only the first one. */
    enter_desk();
    summary(0, 0, 0);
    assert(a.view == DESK);
    summary(1, 0, 0);
    assert(a.view == TASK_DETAIL);
    enter_desk();
    summary(0, 1, 0);
    assert(a.view == LIST && a.selected == 0); /* waiting work opens the list */

    /* Any key returns home, even a press that only lights a dimmed screen;
     * its release does not act on the home page. */
    power_reset(true);
    a.rows[0].state = 1;
    enter_desk();
    apply_screen(WHALE_SCREEN_DIM);
    key_event(BSP_BTN_OK, BSP_BTN_PRESS);
    assert(a.view == HOME && !dim);
    key_event(BSP_BTN_OK, BSP_BTN_RELEASE);
    assert(a.view == HOME && sends == 0);
    enter_desk();
    key(BSP_BTN_DOWN);
    assert(a.view == HOME && sends == 0);

    /* Night: dark clock that dims and then turns off like any idle page. */
    power_reset(true);
    a.rows[0].state = 1;
    wall_clock(a.epoch, afternoon, 540);
    assert(a.clock.hour == 23 && a.night);
    enter_desk();
    a.now += WHALE_POWER_DIM_MS;
    power_step();
    assert(screen_applied == WHALE_SCREEN_DIM);
    a.now += WHALE_POWER_OFF_MS - WHALE_POWER_DIM_MS;
    power_step();
    assert(screen_applied == WHALE_SCREEN_OFF && sleep_commands == 0);

    /* Offline: chip-timed clock, then the normal screen-off and deep sleep. */
    power_reset(true);
    a.rows[0].state = 1;
    wall_clock(a.epoch, afternoon, 480);
    service((whale_event_t){.kind = WE_DOWN});
    wall_clock(a.epoch, afternoon + 3600, 480); /* ignored while offline */
    clock_step();
    assert(a.clock_valid && a.clock.hour == 22 && !a.linked);
    enter_desk();
    a.now += WHALE_POWER_OFF_MS;
    power_step();
    assert(a.view == DESK && screen_applied == WHALE_SCREEN_OFF);
    a.now += WHALE_POWER_OFFLINE_MS;
    power_step();
    assert(sleep_commands == 1);

    /* A reconnect to an old bridge cannot establish global idle from one
     * cached page, even if every visible task is idle. Manual companion stays. */
    setup();
    a.rows[0].state = 1;
    a.rows[1].state = 5;
    enter_desk();
    companion_tick();
    assert(a.view == HOME);
    key(BSP_BTN_UP);
    key(BSP_BTN_OK);
    companion_tick();
    assert(a.view == COMPANION);
    /* Daily summary (0x54): stored for the bridge epoch; stale after the
     * local work day (starting 05:00) moves on. */
    setup();
    wall_clock(a.epoch, afternoon, 480); /* 22:30 local */
    uint32_t today;
    assert(whale_clock_work_day(whale_sleep_clock(), rtc_seconds(), &today));
    uint8_t s[18];
    put64(s, a.epoch + 1);
    put32(s + 8, today);
    put16(s + 12, 5);
    put32(s + 14, 7800);
    apply_message((whale_msg_t){0x54, s, sizeof s}); /* another bridge epoch */
    assert(!a.summary_valid);
    put64(s, a.epoch);
    apply_message((whale_msg_t){0x54, s, 17}); /* wrong size */
    assert(!a.summary_valid);
    apply_message((whale_msg_t){0x54, s, sizeof s});
    assert(a.summary_valid && !a.summary_stale && a.summary_done == 5 &&
           a.summary_seconds == 7800 && a.summary_day == today);
    wall_clock(a.epoch, afternoon + 6 * 3600 + 1799, 480); /* 04:59:59 next day */
    assert(!a.summary_stale);
    wall_clock(a.epoch, afternoon + 6 * 3600 + 1800, 480); /* 05:00 */
    assert(a.summary_stale);
    /* A same-day reconnect to an older bridge must use the clock fallback
     * until this epoch supplies its own optional daily summary. */
    wall_clock(a.epoch, afternoon, 480);
    snapshot(true);
    assert(a.summary_valid && !a.summary_stale);
    uint64_t previous_epoch = a.epoch;
    a.ready = false;
    snapshot_epoch(true, previous_epoch + 1);
    assert(a.ready && a.epoch == previous_epoch + 1 && a.clock_valid && !a.summary_valid);
    apply_message((whale_msg_t){0x54, s, sizeof s}); /* old epoch stays invalid */
    assert(!a.summary_valid);
    put64(s, a.epoch);
    apply_message((whale_msg_t){0x54, s, sizeof s});
    assert(a.summary_valid && !a.summary_stale && a.summary_day == today);
    puts("Desk display entry, wall clock sync, exits, night, offline power and daily summary "
         "PASS");
}
static void test_settings_navigation(void) {
    setup();
    page(HOME);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.view == MANAGE);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.view == STANDBY && a.standby_choice == 0 && !a.back_focus);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK); /* plain clock */
    assert(a.view == MANAGE && a.standby_mode == 1 && a.sound_saved == 0);
    service((whale_event_t){.kind = 98, .value = 1});
    assert(a.sound_saved == 1);
    key(BSP_BTN_OK);
    assert(a.view == STANDBY && a.standby_choice == 1);
    key(BSP_BTN_UP);
    key(BSP_BTN_UP);
    assert(a.back_focus);
    key(BSP_BTN_OK); /* back without saving */
    assert(a.view == MANAGE && a.standby_mode == 1 && a.menu_choice == 2);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.view == VERSION);
    key(BSP_BTN_OK);
    assert(a.view == MANAGE && a.menu_choice == 3);
    key(BSP_BTN_DOWN);
    assert(a.back_focus);
    key(BSP_BTN_OK);
    assert(a.view == HOME && a.dock == 2);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.view == COMPANION);
    a.companion_choice = 4;
    key(BSP_BTN_OK);
    assert(a.view == WARDROBE);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.locked_notice && !a.worn);
    a.unlocked = 6;
    key(BSP_BTN_OK);
    assert(a.outfit_choice == 1 && a.worn == 4);
    /* Outfits replace each other instead of stacking. */
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.outfit_choice == 2 && a.worn == 2);
    key(BSP_BTN_OK);
    assert(!a.worn); /* Confirming the worn outfit returns to the default maid. */
    key(BSP_BTN_OK);
    assert(a.worn == 2);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.outfit_choice == 3 && a.locked_notice && a.worn == 2);
    a.unlocked = 14;
    key(BSP_BTN_OK);
    assert(a.worn == 8 && !a.back_focus && !a.locked_notice);
    a.outfit_choice = 0;
    key(BSP_BTN_OK);
    assert(!a.worn);
    key(BSP_BTN_UP);
    key(BSP_BTN_OK);
    assert(a.view == COMPANION && a.companion_choice == 4);
    key_event(BSP_BTN_UP, BSP_BTN_PRESS);
    key_event(BSP_BTN_UP, BSP_BTN_LONG);
    key_event(BSP_BTN_UP, BSP_BTN_RELEASE);
    assert(a.view == HOME && a.dock == 1);
    dim = true;
    key_event(BSP_BTN_OK, BSP_BTN_PRESS);
    key_event(BSP_BTN_OK, BSP_BTN_RELEASE);
    assert(!dim && a.view == HOME);
    key_event(BSP_BTN_OK, BSP_BTN_PRESS);
    page(PAIR);
    key_event(BSP_BTN_OK, BSP_BTN_RELEASE);
    assert(a.view == PAIR);
    setup();
    a.count = 0;
    a.linked = a.ready = false;
    page(HOME);
    key(BSP_BTN_OK);
    assert(a.view == LIST && a.list_back);
    key(BSP_BTN_OK);
    assert(a.view == HOME);
    setup();
    a.attention_cap = a.global_cap = true;
    key(BSP_BTN_UP);
    assert(a.view == LIST_LOAD && sent_type == 0x52 && seek_to_list && seek_session == 10);
    /* Targeted response can locate the current task beyond the first page. */
    uint8_t begin[27] = {0};
    put64(begin, a.epoch);
    put32(begin + 8, 2);
    put32(begin + 16, 8);
    put32(begin + 20, UINT32_MAX);
    put16(begin + 24, 10);
    begin[26] = 2;
    apply_message((whale_msg_t){0x11, begin, sizeof begin});
    assert(stage.active && stage.cursor == 8);
    stage.rows[0] = a.rows[1];
    stage.rows[1] = a.rows[0];
    stage.taken = 2;
    uint8_t end[17] = {0};
    put64(end, a.epoch);
    put32(end + 8, 2);
    end[12] = 2;
    put32(end + 13, stage.crc);
    apply_message((whale_msg_t){0x13, end, sizeof end});
    assert(a.view == LIST && a.cursor == 8 && a.selected == 1 && !seek_session);
    key(BSP_BTN_UP);
    key(BSP_BTN_UP);
    assert(a.view == LIST_LOAD && requested_cursor == 0);
    setup();
    a.rows[0].question = 42;
    a.rows[0].flags = 32;
    a.global_unread = 1;
    key(BSP_BTN_DOWN | 256);
    assert(a.view == LOADING);
    acknowledge_visible();
    assert(a.global_unread == 1);
    a.meta = true;
    page(QUESTION);
    screen_applied = WHALE_SCREEN_OFF;
    acknowledge_visible();
    assert(a.global_unread == 1);
    screen_applied = WHALE_SCREEN_ON;
    acknowledge_visible();
    /* Reading clears the badge, independently of submitting the answer. */
    assert(!a.global_unread && !(a.rows[0].flags & 32) && sent_type == 0x4a &&
           sent_bytes[16] == 3);
    key(BSP_BTN_UP);
    key(BSP_BTN_OK);
    assert(a.view == TASK_DETAIL && a.rows[0].question == 42 && answers == 0);
    puts("Settings entry, hold/release, return, offline and paged list "
         "navigation PASS");
}
static void power_reset(bool linked) {
    setup();
    a.linked = linked;
    whale_power_init(&power, a.now, false, 0);
    whale_power_link(&power, a.now, linked);
    if (!linked) /* the link went down at boot time in these scenarios */
        power.offline_since = a.now;
    last_input = a.now;
    screen_applied = WHALE_SCREEN_ON;
    sleep_requested = false;
    sleep_retry_at = 0;
    sleep_commands = 0;
    backlight_level = -1;
    key_guard_until = 0;
}
static void test_power(void) {
    /* Linked, idle rows: dim at 2 min, off at 5 min, never sleep. */
    power_reset(true);
    a.rows[0].state = 5;
    a.now += WHALE_POWER_DIM_MS;
    power_step();
    assert(dim && backlight_level == 18 && screen_applied == WHALE_SCREEN_DIM);
    a.now += WHALE_POWER_OFF_MS - WHALE_POWER_DIM_MS;
    power_step();
    assert(dim && backlight_level == 0 && screen_applied == WHALE_SCREEN_OFF);
    a.now += 10 * WHALE_POWER_OFFLINE_MS;
    power_step();
    assert(sleep_commands == 0 && !slow_adv_set);
    /* The first press only wakes the screen. */
    key_event(BSP_BTN_DOWN, BSP_BTN_PRESS);
    key_event(BSP_BTN_DOWN, BSP_BTN_RELEASE);
    assert(!dim && backlight_level == 75 && a.selected == 0);
    /* A running task keeps it lit; a question relights it. */
    power_reset(true);
    a.now += WHALE_POWER_OFF_MS;
    power_step();
    assert(screen_applied == WHALE_SCREEN_ON && !dim);
    a.rows[0].state = 5;
    power_step();
    assert(screen_applied == WHALE_SCREEN_OFF);
    a.rows[1].question = 1;
    power_step();
    assert(screen_applied == WHALE_SCREEN_ON);

    /* Offline: fast advertising first, then slow; stale running rows do not
     * hold the screen, an offline page falls back home, then deep sleep. */
    power_reset(false);
    a.view = REPLY_READ;
    power_step();
    assert(!slow_adv_set && screen_applied == WHALE_SCREEN_ON);
    a.now += WHALE_POWER_FAST_ADV_MS;
    power_step();
    assert(slow_adv_set);
    a.now += WHALE_POWER_OFF_MS - WHALE_POWER_FAST_ADV_MS;
    power_step();
    assert(a.view == HOME && screen_applied == WHALE_SCREEN_OFF && sleep_commands == 0);
    a.now += WHALE_POWER_OFFLINE_MS - WHALE_POWER_OFF_MS - 1;
    power_step();
    assert(sleep_commands == 0);
    a.now += 1;
    power_step();
    assert(sleep_commands == 1 && sleep_requested && sleep_timer_ms == WHALE_POWER_TIMER_MS &&
           sleep_cycles == 1);
    power_step();
    assert(sleep_commands == 1); /* one request in flight */
    /* Refused (key held): retry after 30 s. */
    service((whale_event_t){.kind = 97});
    power_step();
    assert(sleep_commands == 1);
    a.now += 30000;
    power_step();
    assert(sleep_commands == 2);
    /* A key press restarts fast advertising and the offline countdown. */
    power_reset(false);
    a.now += WHALE_POWER_OFFLINE_MS - 1000;
    power_step();
    key_event(BSP_BTN_DOWN, BSP_BTN_PRESS);
    key_event(BSP_BTN_DOWN, BSP_BTN_RELEASE);
    power_step();
    assert(!slow_adv_set && !dim);
    a.now += WHALE_POWER_OFF_MS;
    power_step();
    assert(sleep_commands == 0);
    /* Voice capture and pairing hold the device awake. */
    power_reset(false);
    a.view = PAIR;
    a.now += 2 * WHALE_POWER_OFFLINE_MS;
    power_step();
    assert(a.view == PAIR && sleep_commands == 0);

    /* Timer wake: dark screen, short window, then sleep with a growing cycle
     * count; the computer showing up cancels it. */
    power_reset(false);
    whale_power_init(&power, a.now, true, 3);
    screen_applied = WHALE_SCREEN_OFF;
    power_step();
    assert(screen_applied == WHALE_SCREEN_OFF && sleep_commands == 0);
    a.now += WHALE_POWER_WINDOW_MS;
    power_step();
    assert(sleep_commands == 1 && sleep_cycles == 4 && sleep_timer_ms == WHALE_POWER_TIMER_MS);
    power_reset(false);
    whale_power_init(&power, a.now, true, WHALE_POWER_TIMER_CYCLES);
    a.now += WHALE_POWER_WINDOW_MS;
    power_step();
    assert(sleep_commands == 1 && sleep_timer_ms == 0); /* key wake only */
    power_reset(false);
    whale_power_init(&power, a.now, true, 2);
    service((whale_event_t){.kind = WE_LINK});
    power_step();
    assert(screen_applied == WHALE_SCREEN_ON && power.cycles == 0);
    a.now += WHALE_POWER_WINDOW_MS;
    power_step();
    assert(sleep_commands == 0);
    /* A held waking key is ignored for a moment after a key wake. */
    power_reset(true);
    key_guard_until = a.now + 1000;
    key_event(BSP_BTN_DOWN, BSP_BTN_PRESS);
    key_event(BSP_BTN_DOWN, BSP_BTN_RELEASE);
    assert(a.selected == 0);
}
static void hello(uint32_t caps, uint16_t peer_rx, uint16_t peer_tx) {
    uint8_t p[10] = {2, 2};
    put32(p + 2, caps);
    put16(p + 6, peer_rx);
    put16(p + 8, peer_tx);
    apply_message((whale_msg_t){1, p, sizeof p});
}
static void test_large_frame_negotiation(void) {
    // An unchanged 2.2 bridge keeps 2.2, 20-byte frames and no bit 12.
    setup();
    frame_limit = WHALE_FRAME_MAX;
    hello(4095, 20, 20);
    assert(hello_reply[0] == 2 && hello_reply[1] == 2 && !hello_reply[10]);
    assert(w32(hello_reply + 2) == 4095 && w16(hello_reply + 6) == 20 &&
           w16(hello_reply + 8) == 20);
    assert(tx_frame == WHALE_FRAME_LEGACY && rx.frame == 0 && !rich_questions);
    // Bit 15 (approval and multi-select) is echoed only when offered.
    hello(4095 | WHALE_CAP_RICH_QUESTIONS, 20, 20);
    assert(w32(hello_reply + 2) == (4095 | WHALE_CAP_RICH_QUESTIONS) && rich_questions);
    hello(4095, 20, 20);
    assert(!rich_questions);

    // A 2.3 bridge at MTU 247 gets minor 3, bit 12 and the largest common frame.
    hello(4095 | WHALE_CAP_LARGE_FRAMES, 244, 200);
    assert(hello_reply[1] == 3 && !hello_reply[10]);
    assert(w32(hello_reply + 2) == (4095 | WHALE_CAP_LARGE_FRAMES));
    assert(w16(hello_reply + 6) == 200 && w16(hello_reply + 8) == 200);
    assert(hello_frame == WHALE_FRAME_LEGACY && tx_frame == 200 && rx.frame == 200);

    // A version-3 frame from the ring is reassembled and the slot is released.
    a.ready = true; // as after the snapshot that follows the hello
    uint8_t body[20] = {0};
    put64(body, a.epoch);
    put32(body + 8, 10);
    put32(body + 12, 40);
    body[16] = 3;
    memcpy(body + 17, "big", 3);
    uint8_t head[8] = {3, 0x19, 1, 0, 0, 1, sizeof body, 0};
    memcpy(frame_ring, head, sizeof head);
    memcpy(frame_ring + 8, body, sizeof body);
    unsigned releases = frame_releases;
    whale_event_t e = {.kind = WE_FRAME, .len = 8 + sizeof body, .value = 0};
    service(e);
    assert(frame_releases == releases + 1 && a.replies[0].revision == 40);

    // Disconnect forgets the grant; stale large frames are rejected but still released.
    service((whale_event_t){.kind = WE_DOWN});
    assert(rx.frame == 0);
    frame_ring[2] = 2;
    service(e);
    assert(frame_releases == releases + 2 && a.replies[0].revision == 40);

    // A 2.3 bridge on a 23-byte MTU still speaks 2.3, but without large frames.
    setup();
    frame_limit = WHALE_FRAME_LEGACY;
    hello(4095 | WHALE_CAP_LARGE_FRAMES, 244, 244);
    assert(hello_reply[1] == 3 && w32(hello_reply + 2) == 4095 && w16(hello_reply + 6) == 20);
    assert(tx_frame == WHALE_FRAME_LEGACY && rx.frame == 0);

    // A rejected hello never grants large frames.
    frame_limit = WHALE_FRAME_MAX;
    hello(4095 | WHALE_CAP_LARGE_FRAMES, 244, 19);
    assert(hello_reply[10] == 1 && w32(hello_reply + 2) == 4095 && w16(hello_reply + 6) == 20);
    assert(tx_frame == WHALE_FRAME_LEGACY && rx.frame == 0);
    frame_limit = WHALE_FRAME_LEGACY;
}
static void interact(void) {
    a.view = COMPANION;
    a.back_focus = false;
    a.companion_choice = 0;
    a.companion_action = 0;
    key(BSP_BTN_OK);
    assert(a.companion_action == 1);
}
static void test_bond_daily_cap(void) {
    /* Unsynced clock: the tap cap still holds and the day never rolls over. */
    setup();
    a.bond = 12;
    for (int i = 0; i < 8; i++)
        interact();
    assert(a.bond == 12 + WHALE_BOND_TAP_MAX && a.bond_today == WHALE_BOND_TAP_MAX);
    assert(room_unsaved && a.bond_day == 0);
    /* First sync dates the offline gains even without a further interaction. */
    int64_t day_start = 1793750400LL; /* 2026-11-04 00:00 UTC */
    uint32_t today = (uint32_t)(day_start / 86400);
    wall_clock(a.epoch, day_start + 3600, 0);
    assert(a.bond_day == today && a.bond_today == 5 && !a.bond_undated);
    interact();
    wall_clock(a.epoch, day_start - 3600, 0);
    interact();
    wall_clock(a.epoch, day_start + 3600, 0);
    interact();
    assert(a.bond == 17 && a.bond_day == today && a.bond_today == 5);
    /* A new local date from the UTC offset rolls once, including after clock loss. */
    wall_clock(a.epoch, day_start + 23 * 3600, 120);
    assert(a.bond_day == today + 1 && !a.bond_today);
    interact();
    *whale_sleep_clock() = (whale_clock_t){0};
    interact();
    interact();
    assert(a.bond_today == 3 && a.bond_undated == 2);
    whale_audio_cmd_t sent = {0};
    last_cue_capture = &sent;
    save_retry_at = a.now;
    companion_tick();
    uint8_t stored[11];
    room_profile(sent, stored);
    assert(sent.kind == WHALE_CMD_ROOM_SAVE && stored[3] == 3 && stored[4] == 2);
    wall_clock(a.epoch, day_start + 2 * 86400, 0);
    assert(a.bond_day == today + 2 && a.bond_today == 2 && !a.bond_undated);
    for (int i = 0; i < 6; i++)
        interact();
    assert(a.bond == 23 && a.bond_today == 5);
    /* Next local day: five more, never above 99. */
    wall_clock(a.epoch, day_start + 3 * 86400, 0);
    a.bond = 97;
    for (int i = 0; i < 4; i++)
        interact();
    /* At the top, taps still count as the day's activity. */
    assert(a.bond == WHALE_BOND_MAX && a.bond_today == 4 && a.bond_day == today + 3);
    /* The save carries bond, the daily count and the day. */
    room_unsaved = true;
    companion_tick();
    last_cue_capture = NULL;
    assert(!room_unsaved && sent.kind == WHALE_CMD_ROOM_SAVE && sent.mode == WHALE_BOND_MAX);
    room_profile(sent, stored);
    assert(stored[3] == 4 && w32(stored + 7) == today + 3);
}
static void test_wardrobe_screen_off(void) {
    const uint8_t states[] = {5, 2, 8}; /* finished, thinking, pending question */
    for (unsigned i = 0; i < sizeof states; i++) {
        power_reset(true);
        a.view = WARDROBE;
        a.rows[0].state = states[i];
        a.rows[0].question = states[i] == 8 ? 42 : 0;
        a.now += WHALE_POWER_DIM_MS;
        power_step();
        assert(screen_applied == WHALE_SCREEN_DIM);
        a.now += WHALE_POWER_OFF_MS - WHALE_POWER_DIM_MS;
        power_step();
        assert(a.view == WARDROBE && screen_applied == WHALE_SCREEN_OFF && !sleep_commands);
    }
    puts("Wardrobe screen dim/off with idle and active sessions PASS");
}
static void answer_accepted(void) {
    a.view = SENDING;
    uint8_t p[23] = {0};
    put64(a.identity, a.epoch);
    memcpy(p, a.identity, 20);
    p[20] = 0;
    apply_message((whale_msg_t){0x23, p, sizeof p});
    assert(a.view == RESULT && a.result == 0);
}
static void test_bond_work_and_unlocks(void) {
    setup();
    a.bond = 0;
    a.global_cap = true;
    int64_t day_start = 1793750400LL;
    wall_clock(a.epoch, day_start + 3600, 0);
    /* Work earns bond: an accepted device answer, a completion, a voice send. */
    answer_accepted();
    assert(a.bond == WHALE_BOND_ANSWER && a.bond_today == WHALE_BOND_ANSWER);
    cue_event(1, 10, 30, 3, 3);
    assert(a.bond == WHALE_BOND_ANSWER + WHALE_BOND_DONE);
    /* A cue with no live trigger, or an error, earns nothing. */
    cue_event(2, 10, 31, 3, 0);
    cue_event(3, 10, 32, 2, 2);
    assert(a.bond == WHALE_BOND_ANSWER + WHALE_BOND_DONE);
    uint8_t v[27] = {0};
    a.view = VOICE;
    a.voice_submitted = true;
    memcpy(v, a.voice_identity, 24);
    v[24] = 3;
    apply_message((whale_msg_t){0x41, v, sizeof v});
    assert(a.voice_phase == VOICE_SENT && a.bond == 7 && !a.voice_submitted);
    /* A repeated "sent" for the same recording earns nothing more. */
    apply_message((whale_msg_t){0x41, v, sizeof v});
    assert(a.bond == 7);
    /* Taps still add at most five; the whole day stops at ten. */
    for (int i = 0; i < 8; i++)
        interact();
    assert(a.bond == WHALE_BOND_DAILY_MAX && a.bond_today == WHALE_BOND_DAILY_MAX);
    answer_accepted();
    assert(a.bond == WHALE_BOND_DAILY_MAX);
    /* The next day: taps alone stop at five, work can add the rest. */
    wall_clock(a.epoch, day_start + 86400 + 3600, 0);
    for (int i = 0; i < 8; i++)
        interact();
    assert(a.bond == 15 && a.bond_today == 5 && !a.unlocked);
    answer_accepted();
    cue_event(4, 10, 33, 3, 3);
    assert(a.bond == 20 && a.unlocked == 2 && a.unlock_new == 2 && room_unsaved);
    /* Entering the wardrobe clears the notice and the clip can be worn. */
    a.view = COMPANION;
    a.back_focus = false;
    a.companion_choice = 4;
    key(BSP_BTN_OK);
    assert(a.view == WARDROBE && !a.unlock_new);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.outfit_choice == 1 && a.locked_notice && !a.worn);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.outfit_choice == 2 && a.worn == 2 && !a.locked_notice);
    page(COMPANION);
    key(BSP_BTN_OK);
    assert(a.view == WARDROBE && a.outfit_choice == 2 && a.worn == 2);
    key(BSP_BTN_UP);
    key(BSP_BTN_OK);
    assert(a.locked_notice && a.worn == 2);
    assert(whale_unlocks_for(19) == 0 && whale_unlocks_for(20) == 2 &&
           whale_unlocks_for(39) == 2 && whale_unlocks_for(40) == 6 &&
           whale_unlocks_for(59) == 6 && whale_unlocks_for(60) == 14);
    /* Forty unlocks only the pajamas; sixty separately unlocks loungewear. */
    a.bond = 38;
    a.bond_today = 0;
    answer_accepted();
    assert(a.unlocked == 6 && a.unlock_new == 4);
    a.bond = 58;
    a.bond_today = 0;
    answer_accepted();
    assert(a.unlocked == 14 && a.unlock_new == 8);
    puts("Bond from work, daily caps and accessory unlocks PASS");
}
static void test_bond_cooling(void) {
    setup();
    int64_t day0 = 1793750400LL;
    uint32_t today = (uint32_t)(day0 / 86400);
    a.bond = 30;
    a.unlocked = 0;
    wall_clock(a.epoch, day0 + 3600, 0);
    assert(a.bond_day == today);
    /* An active day keeps bond; the clock sync alone is not activity. */
    interact();
    wall_clock(a.epoch, day0 + 86400 + 3600, 0);
    assert(a.bond == 31 && a.bond_day == today + 1 && !a.bond_today);
    /* A synced but idle day costs one at the next day. */
    wall_clock(a.epoch, day0 + 2 * 86400 + 3600, 0);
    assert(a.bond == 30);
    /* Work alone counts as activity. */
    a.global_cap = true;
    cue_event(1, 10, 30, 3, 3);
    assert(a.bond == 32);
    wall_clock(a.epoch, day0 + 3 * 86400 + 3600, 0);
    assert(a.bond == 32);
    /* Three days away (the last synced day idle, two unseen): minus three. */
    wall_clock(a.epoch, day0 + 6 * 86400 + 3600, 0);
    assert(a.bond == 29);
    /* Never below the best unlocked accessory's threshold. */
    a.unlocked = 2;
    a.bond = 22;
    wall_clock(a.epoch, day0 + 16 * 86400 + 3600, 0);
    assert(a.bond == WHALE_UNLOCK_WITCH && a.unlocked == 2);
    a.unlocked = 6;
    a.bond = 43;
    wall_clock(a.epoch, day0 + 20 * 86400 + 3600, 0);
    assert(a.bond == WHALE_UNLOCK_PAJAMAS && a.unlocked == 6);
    a.unlocked = 14;
    a.bond = 63;
    wall_clock(a.epoch, day0 + 24 * 86400 + 3600, 0);
    assert(a.bond == WHALE_UNLOCK_LOUNGE && a.unlocked == 14);
    a.unlocked = 0;
    a.bond = 3;
    wall_clock(a.epoch, day0 + 30 * 86400 + 3600, 0);
    assert(a.bond == 0);
    /* Gains made while the clock was lost cover one day of the gap. */
    a.bond = 40;
    interact();
    *whale_sleep_clock() = (whale_clock_t){0};
    interact();
    assert(a.bond_undated == 1);
    wall_clock(a.epoch, day0 + 33 * 86400 + 3600, 0);
    assert(a.bond == 41); /* 42 minus two unseen days, one of them covered */
    puts("Bond cooling on idle days PASS");
}
static void test_separate_work_and_tap_quota(void) {
    const int64_t day = 1793750400LL;
    setup();
    a.bond = 0;
    wall_clock(a.epoch, day + 3600, 0);
    interact();
    wall_clock(a.epoch, day + 86400 + 3600, 0);
    answer_accepted(); /* Work arrives before the first interaction of a new day. */
    unsigned before = a.bond;
    for (int i = 0; i < 8; i++)
        interact();
    assert(a.bond == before + 5 && a.bond_today == 8 && a.bond_taps == 5);
    whale_audio_cmd_t saved = {0};
    last_cue_capture = &saved;
    save_retry_at = a.now;
    companion_tick();
    last_cue_capture = NULL;
    uint8_t blob[11];
    room_profile(saved, blob);
    unsigned bond = a.bond;
    setup();
    stored_room_key = "room_v3";
    memcpy(stored_room, blob, sizeof blob);
    stored_room_size = sizeof blob;
    room_load(); /* Exercise real restore, including a cold boot without a clock. */
    assert(a.bond == bond && a.bond_today == 8 && a.bond_taps == 5);
    interact();
    wall_clock(a.epoch, day + 86400 + 3600, 0);
    interact();
    assert(a.bond == bond);
    wall_clock(a.epoch, day + 2 * 86400 + 3600, 0);
    answer_accepted();
    for (int i = 0; i < 5; i++)
        interact();
    assert(a.bond_today == 8 && a.bond_taps == 5);
    /* Legacy v2 data keeps affection, ownership and a conservative old tap quota. */
    setup();
    stored_room_key = "room_v2";
    stored_room_size = 7;
    uint8_t old[] = {55, 6, 6, 0x23, 20, 81, 0};
    memcpy(stored_room, old, sizeof old);
    room_load();
    assert(a.bond == 55 && a.unlocked == 6 && a.worn == 4);
    assert(a.bond_today == 3 && a.bond_undated == 2 && a.bond_taps == 3 &&
           a.bond_taps_undated == 2 && room_unsaved);
    /* The old hoodie's stable ID now owns and equips the apprentice witch. */
    stored_room[0] = 25;
    stored_room[1] = stored_room[2] = 2;
    room_load();
    assert(a.bond == 25 && a.unlocked == 2 && a.worn == 2);
    page(COMPANION);
    a.companion_choice = 4;
    key(BSP_BTN_OK);
    assert(a.view == WARDROBE && a.outfit_choice == 2);
    /* Bad persisted counters are bounded before they can affect the daily cap. */
    memset(blob, 255, sizeof blob);
    blob[0] = 80;
    room_restore(blob);
    assert(a.bond_today == 10 && a.bond_undated == 10 && a.bond_taps == 5 &&
           a.bond_taps_undated == 5 && !a.bond_day);
    puts("Independent work/tap quotas, cold restart and legacy profile migration PASS");
}
static void test_voice_retake(void) {
    setup();
    a.voice_cap = true;
    key(BSP_BTN_OK);
    assert(a.view == VOICE && a.voice_phase == VOICE_PREPARE);
    uint8_t first[24];
    memcpy(first, a.voice_identity, 24);
    uint8_t b[32];
    memcpy(b, a.voice_identity, 24);
    b[24] = 2;
    put16(b + 25, 5);
    memcpy(b + 27, "hello", 5);
    a.voice_phase = VOICE_FINISH;
    apply_message((whale_msg_t){0x41, b, 32});
    assert(a.voice_phase == VOICE_ACTIONS && a.voice_choice == 0);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_DOWN);
    assert(a.voice_choice == 2);
    key(BSP_BTN_UP);
    assert(a.voice_choice == 1);
    unsigned before = sends;
    key(BSP_BTN_OK);
    /* Retake cancels the old recording, then prepares a new identity. */
    assert(sends == before + 2 && sent_type == 0x40);
    assert(a.view == VOICE && a.voice_phase == VOICE_PREPARE && !a.voice_text[0]);
    assert(!memcmp(sent_bytes, first, 8)); /* same epoch, fresh session request */
    /* Cancel moved to the third choice. */
    memcpy(b, a.voice_identity, 24);
    a.voice_phase = VOICE_FINISH;
    apply_message((whale_msg_t){0x41, b, 32});
    key(BSP_BTN_DOWN);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.view == TASK_DETAIL && sent_type == 0x44);
    puts("Voice retake PASS");
}
static void voice_status(uint8_t code) {
    uint8_t b[27] = {0};
    memcpy(b, a.voice_identity, 24);
    b[24] = code;
    apply_message((whale_msg_t){0x41, b, sizeof b});
}
static void home_voice(void) {
    /* Home -> 会话 list (first session focused) -> ↑ to the new-task row -> OK. */
    page(HOME);
    a.dock = 1;
    a.cursor = 0;
    key(BSP_BTN_OK);
    assert(a.view == LIST || a.view == LIST_LOAD);
    page(LIST);
    a.list_back = false;
    a.selected = 1;
    key(BSP_BTN_UP);
    assert(!a.list_new && a.selected == 0);
    key(BSP_BTN_UP);
    assert(a.list_new && !a.list_back);
    key(BSP_BTN_OK);
}
static void test_voice_new_task(void) {
    /* Old bridge (no bit 16): the dock entry explains instead of recording. */
    setup();
    hello(63 | 512, 20, 20);
    assert(a.voice_cap && !a.voice_task_cap && !(w32(hello_reply + 2) & WHALE_CAP_VOICE_TASK));
    a.ready = a.linked = true;
    sends = 0;
    home_voice();
    assert(a.view == VOICE && a.voice_phase == VOICE_UNSUPPORTED && a.voice_new && !sends);
    key(BSP_BTN_UP);
    assert(a.view == LIST && a.list_new && !a.voice_new);
    /* New bridge: the firmware echoes bit 16 only together with voice. */
    hello(63 | 512 | WHALE_CAP_VOICE_TASK, 20, 20);
    assert(a.voice_task_cap && (w32(hello_reply + 2) & WHALE_CAP_VOICE_TASK));
    hello(63 | WHALE_CAP_VOICE_TASK, 20, 20);
    assert(!a.voice_task_cap && !(w32(hello_reply + 2) & WHALE_CAP_VOICE_TASK));
    setup();
    a.voice_cap = a.voice_task_cap = true;
    a.rows[0].question = 7; /* a waiting question does not block a new task */
    a.selected = 1;
    home_voice();
    assert(a.view == VOICE && a.voice_phase == VOICE_PREPARE && sent_type == 0x48);
    assert(sent_length == 28 && w32(sent_bytes + 8) == 10); /* last highlighted, context only */
    assert(!strcmp(a.voice_title, "新任务"));
    /* The folder label arrives before recording; foreign identities are ignored. */
    uint8_t label[24 + 4];
    memcpy(label, a.voice_identity, 24);
    memcpy(label + 24, "shop", 4);
    label[0] ^= 1;
    apply_message((whale_msg_t){0x49, label, sizeof label});
    assert(!strcmp(a.voice_title, "新任务"));
    label[0] ^= 1;
    apply_message((whale_msg_t){0x49, label, sizeof label});
    assert(!strcmp(a.voice_title, "新任务 · shop"));
    voice_status(1);
    assert(voice_starts == 1);
    whale_event_t e = {.kind = WE_VOICE, .len = 16, .value = 1};
    memcpy(e.bytes, a.voice_identity + 8, 16);
    service(e);
    assert(a.voice_phase == VOICE_RECORD);
    /* Tap to stop, exactly like a voice reply; a key release does nothing. */
    key_event(BSP_BTN_OK, BSP_BTN_RELEASE);
    assert(a.voice_phase == VOICE_RECORD);
    key(BSP_BTN_OK);
    assert(a.voice_phase == VOICE_FINISH);
    uint8_t b[27 + 12];
    memcpy(b, a.voice_identity, 24);
    b[24] = 2;
    put16(b + 25, 12);
    memcpy(b + 27, "fix login", 9);
    memcpy(b + 36, "!!!", 3);
    apply_message((whale_msg_t){0x41, b, sizeof b});
    assert(a.voice_phase == VOICE_ACTIONS && !strcmp(a.voice_text, "fix login!!!"));
    /* Retake keeps the new-task target and asks again with 0x48. */
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(sent_type == 0x48 && a.voice_new && a.voice_phase == VOICE_PREPARE);
    voice_status(1);
    memcpy(e.bytes, a.voice_identity + 8, 16);
    service(e);
    assert(a.voice_phase == VOICE_RECORD);
    key(BSP_BTN_OK);
    assert(a.voice_phase == VOICE_FINISH);
    memcpy(b, a.voice_identity, 24);
    apply_message((whale_msg_t){0x41, b, sizeof b});
    key(BSP_BTN_OK);
    assert(a.voice_phase == VOICE_SENDING && sent_type == 0x45);
    voice_status(3);
    assert(a.voice_phase == VOICE_SENT);
    key(BSP_BTN_OK);
    assert(a.view == LIST && a.list_new && !a.voice_new && a.rows[0].question == 7);
    /* No folder on the computer; the status is ignored outside a new task. */
    home_voice();
    voice_status(14);
    assert(a.voice_phase == VOICE_NO_TARGET);
    key(BSP_BTN_OK);
    assert(a.view == LIST && a.list_new);
    a.voice_new = false;
    a.voice_phase = VOICE_PREPARE;
    page(VOICE);
    voice_status(14);
    assert(a.voice_phase == VOICE_PREPARE);
    /* Holding OK on home starts nothing; the dock keeps three entries. */
    setup();
    a.voice_cap = a.voice_task_cap = true;
    page(HOME);
    sends = 0;
    key_event(BSP_BTN_OK, BSP_BTN_PRESS);
    key_event(BSP_BTN_OK, BSP_BTN_LONG);
    assert(a.view == HOME && !sends);
    a.dock = 0;
    key(BSP_BTN_UP);
    assert(a.dock == 2);
    /* List focus order: home ‹ / new task / sessions, both ways. */
    page(LIST);
    a.selected = 0;
    a.list_back = true;
    key(BSP_BTN_DOWN);
    assert(a.list_new && !a.list_back);
    key(BSP_BTN_DOWN);
    assert(!a.list_new && a.selected == 0);
    key(BSP_BTN_UP);
    key(BSP_BTN_UP);
    assert(a.list_back && !a.list_new);
    key(BSP_BTN_OK);
    assert(a.view == HOME);
    /* No row without voice, nor beyond the first window. */
    page(LIST);
    a.list_back = false;
    a.voice_cap = false;
    key(BSP_BTN_UP);
    assert(a.list_back && !a.list_new);
    a.voice_cap = true;
    a.cursor = 8;
    a.ready = false; /* no window reload in this check */
    page(LIST);
    a.list_back = false;
    a.selected = 0;
    key(BSP_BTN_UP);
    assert(!a.list_new);
    /* Empty list: ↓ from the home arrow reaches the new-task row. */
    a.cursor = 0;
    a.count = 0;
    page(LIST);
    a.list_back = true;
    key(BSP_BTN_DOWN);
    assert(a.list_new);
    a.ready = true;
    key(BSP_BTN_OK);
    assert(a.view == VOICE && a.voice_new && sent_type == 0x48 && w32(sent_bytes + 8) == 0);
    puts("Voice new task: list entry, folder label, retake, compatibility PASS");
}
static void question_meta(uint8_t count, const char *prompt, int mode) {
    uint8_t p[600];
    memcpy(p, a.identity, 20);
    p[20] = count;
    p[21] = (count + 2) / 3;
    put16(p + 22, strlen(prompt));
    memcpy(p + 24, prompt, strlen(prompt));
    size_t n = 24 + strlen(prompt);
    if (mode >= 0)
        p[n++] = mode;
    apply_message((whale_msg_t){0x20, p, n});
}
static void question_page(uint8_t page_index, uint8_t first, uint8_t count, const char *const *names) {
    uint8_t p[200];
    memcpy(p, a.identity, 20);
    p[20] = page_index;
    p[21] = count;
    size_t n = 22;
    for (uint8_t j = 0; j < count; j++) {
        put16(p + n, first + j);
        put16(p + n + 2, strlen(names[j]));
        memcpy(p + n + 4, names[j], strlen(names[j]));
        n += 4 + strlen(names[j]);
    }
    apply_message((whale_msg_t){0x21, p, n});
}
static void test_rich_questions(void) {
    static const char *const first[] = {"alpha", "beta", "gamma"}, *const last[] = {"delta"};
    static const char *const verdict[] = {"allow", "deny"};
    /* Multi-select: toggle across pages, submit row only on the last page. */
    setup();
    rich_questions = true;
    a.rows[0].question = 50;
    page(TASK_DETAIL);
    open_task();
    assert(a.view == LOADING && sent_type == 0x24);
    question_meta(4, "pick", WHALE_MODE_MULTI);
    assert(a.meta && a.question_mode == WHALE_MODE_MULTI && a.view == LOADING);
    question_page(0, 0, 3, first);
    assert(a.view == QUESTION && a.options == 3 && !a.picked);
    key(BSP_BTN_OK);
    assert(a.picked == 1 && a.view == QUESTION && answers == 0);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.picked == 5);
    key(BSP_BTN_OK);
    assert(a.picked == 1);
    key(BSP_BTN_DOWN);
    assert(a.view == LOADING && a.page == 1);
    question_page(1, 3, 1, last);
    key(BSP_BTN_OK);
    assert(a.picked == 9);
    key(BSP_BTN_DOWN);
    assert(a.focus == 1 && submit_row());
    key(BSP_BTN_OK);
    assert(a.view == SENDING && sent_type == 0x26 && sent_length == 28 &&
           w64(sent_bytes + 20) == 9 && answers == 0);
    /* A nothing-picked submit row stays put. */
    a.picked = 0;
    page(QUESTION);
    sends = 0;
    key(BSP_BTN_OK);
    assert(a.view == QUESTION && !sends);

    /* Approval: rests on deny; allow re-reads the full request first. */
    setup();
    rich_questions = true;
    a.rows[0].question = 51;
    open_task();
    question_meta(2, "run npm test", WHALE_MODE_APPROVAL);
    assert(a.question_mode == WHALE_MODE_APPROVAL && a.focus == 1);
    question_page(0, 0, 2, verdict);
    assert(a.view == QUESTION && a.focus == 1);
    key(BSP_BTN_UP);
    key(BSP_BTN_OK);
    assert(a.view == OPTION && answers == 0);
    key(BSP_BTN_OK);
    assert(a.view == SENDING && sent_type == 0x22 && w16(sent_bytes + 20) == 0 && answers == 1);
    setup();
    rich_questions = true;
    a.rows[0].question = 52;
    open_task();
    question_meta(2, "rm -rf build", WHALE_MODE_APPROVAL);
    question_page(0, 0, 2, verdict);
    key(BSP_BTN_OK);
    assert(a.view == SENDING && w16(sent_bytes + 20) == 1);
    /* Approval with three options, or a mode byte without the capability, is rejected. */
    setup();
    rich_questions = true;
    a.rows[0].question = 53;
    open_task();
    question_meta(3, "bad", WHALE_MODE_APPROVAL);
    assert(a.view == LOAD_FAILED);
    setup();
    rich_questions = false;
    a.rows[0].question = 54;
    open_task();
    question_meta(2, "old", WHALE_MODE_MULTI);
    assert(a.view == LOAD_FAILED);
    open_task();
    question_meta(2, "old", -1);
    assert(a.meta && a.question_mode == WHALE_MODE_SINGLE);
    puts("Approval and multi-select questions PASS");
}
/* Deliver a two-row priority snapshot; row 0 carries question qid (0: none). */
static void queue_snapshot(uint32_t qid) {
    memset(&stage, 0, sizeof stage);
    stage.active = true;
    stage.epoch = a.epoch;
    stage.snap = stage.count = stage.taken = stage.total = 2;
    stage.next = UINT32_MAX;
    stage.rows[0] = (whale_row_t){.session = 11, .run = 21, .state = 8, .question = qid};
    stage.rows[1] = (whale_row_t){.session = 10, .run = 20, .state = 2};
    uint8_t end[17] = {0};
    put64(end, a.epoch);
    put32(end + 8, 2);
    end[12] = 2;
    apply_message((whale_msg_t){0x13, end, sizeof end});
}
static void test_pending_queue(void) {
    /* After an accepted answer, the next waiting question opens by itself. */
    setup();
    a.attention_cap = a.global_cap = true;
    answer_accepted();
    uint32_t answered_at = a.now;
    summary(0, 2, 0); /* stale order: arrives with the result, still counts it */
    a.global_at = answered_at - 1;
    a.now += WHALE_QUEUE_NEXT_MS;
    companion_tick();
    assert(a.view == RESULT);
    a.now = answered_at + 200;
    summary(0, 1, 0); /* newer summary: one more question waits */
    a.now = answered_at + WHALE_QUEUE_NEXT_MS - 1;
    companion_tick();
    assert(a.view == RESULT); /* the result shows first */
    a.now++;
    companion_tick();
    assert(a.view == LIST_LOAD && queue_next && sent_type == 0x10 && sent_bytes[13] == 2);
    queue_snapshot(77);
    assert(a.view == LOADING && sent_type == 0x24 && a.selected == 0 &&
           w32(a.identity + 8) == 11 && w32(a.identity + 16) == 77 && !queue_next);

    /* Confirm skips the wait; with nothing answerable left, land on the list. */
    setup();
    a.attention_cap = a.global_cap = true;
    answer_accepted();
    a.now += 100;
    summary(0, 1, 0);
    last_input = a.now;
    key(BSP_BTN_OK);
    assert(a.view == LIST_LOAD && queue_next);
    queue_snapshot(0);
    assert(a.view == LIST && !queue_next);

    /* Nothing more waiting: confirm goes back to the task as before. */
    setup();
    a.attention_cap = a.global_cap = true;
    answer_accepted();
    a.now += 100;
    summary(0, 0, 0);
    key(BSP_BTN_OK);
    assert(a.view == TASK_DETAIL);
    page(RESULT);
    a.now += WHALE_QUEUE_NEXT_MS;
    companion_tick();
    assert(a.view == RESULT); /* nothing waits: the result stays until confirmed */

    /* A question pops up on a home page left alone, but not on one in use. */
    setup();
    a.attention_cap = a.global_cap = true;
    page(HOME);
    last_input = a.now;
    a.now += WHALE_QUEUE_IDLE_MS - 1;
    cue_event(1, 11, 5, 1, 1);
    assert(a.view == HOME);
    a.now++;
    cue_event(2, 11, 6, 1, 1);
    assert(a.view == LIST_LOAD && queue_next && queue_from == HOME);
    queue_snapshot(0); /* only questions for the computer: stay home */
    assert(a.view == HOME && !queue_next);
    /* Recording and the list in use are never interrupted. */
    a.now += WHALE_QUEUE_IDLE_MS;
    fake_voice_busy = true;
    cue_event(4, 11, 8, 1, 1);
    assert(a.view == HOME);
    fake_voice_busy = false;
    page(LIST);
    cue_event(5, 11, 9, 1, 1);
    assert(a.view == LIST && !queue_next);
    /* The desk display opens the queue too. */
    page(DESK);
    cue_event(6, 11, 10, 1, 1);
    assert(a.view == LIST_LOAD && queue_next && queue_from == DESK);
    queue_snapshot(0);
    assert(a.view == LIST);
    puts("Pending queue: next question after an answer, quiet home pop-up, fallbacks PASS");
}

/* Seeded random frames, keys, links and time through the whole application.
 * Built with ASan/UBSan in validate.sh; it looks for crashes, out-of-range
 * state and broken screen-director invariants rather than exact behavior.
 * Reproduce: WHALE_APP_FUZZ_SEED=<seed>. */
static uint64_t app_rng;
static uint32_t app_random(void) {
    app_rng ^= app_rng << 13;
    app_rng ^= app_rng >> 7;
    app_rng ^= app_rng << 17;
    return (uint32_t)(app_rng >> 32);
}
static void app_fuzz_check(uint64_t seed, unsigned step, unsigned action) {
    bool ok = a.view <= STANDBY && a.count <= 8 && (!a.count || a.selected < a.count) &&
              a.scroll >= 0 && a.options <= 3 && a.focus <= 8 &&
              /* The queue only waits on its own priority snapshot. */
              (!queue_next || a.view == LIST_LOAD || a.view == LIST_FAILED);
    /* The desk display never hides work that needs the user. */
    if (ok && a.view == DESK && a.linked && a.ready && a.global_cap && a.global_valid &&
        a.now - a.global_at < WHALE_SUMMARY_FRESH_MS)
        ok = !a.global_waiting && !a.global_unread;
    if (!ok) {
        fprintf(stderr,
                "app fuzz: bad state seed=%llu step=%u action=%u view=%d count=%u selected=%u "
                "scroll=%d options=%u focus=%u waiting=%u unread=%u\n"
                "reproduce: WHALE_APP_FUZZ_SEED=%llu\n",
                (unsigned long long)seed, step, action, a.view, a.count, a.selected, a.scroll,
                a.options, a.focus, a.global_waiting, a.global_unread, (unsigned long long)seed);
        abort();
    }
}
static void app_fuzz_one(uint64_t seed, unsigned steps) {
    static const uint8_t types[] = {0x01, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x19,
                                    0x1a, 0x1b, 0x1c, 0x20, 0x21, 0x23, 0x31, 0x32, 0x41,
                                    0x47, 0x49, 0x50, 0x51, 0x53, 0x54};
    static uint8_t p[640];
    setup();
    a.global_cap = a.attention_cap = true;
    app_rng = seed * 0x9e3779b97f4a7c15ull + 7;
    for (unsigned step = 0; step < steps; step++) {
        unsigned action = app_random() % 8;
        switch (action) {
        case 0:
        case 1:
        case 2: {
            uint8_t type = types[app_random() % sizeof types];
            size_t n = app_random() % 4 ? app_random() % 48 : app_random() % sizeof p;
            for (size_t i = 0; i < n; i++)
                p[i] = (uint8_t)(app_random() % 3 ? app_random() % 6 : app_random());
            if (n >= 8 && app_random() % 4)
                put64(p, a.epoch);
            if (type == 0x50 && app_random() % 2) {
                /* Well-formed global summary: counts never exceed the total. */
                n = 25;
                put64(p, a.epoch);
                put32(p + 8, a.global_seq + 1);
                uint16_t total = (uint16_t)(app_random() % 5);
                put16(p + 12, total);
                for (unsigned i = 14; i <= 18; i += 2)
                    put16(p + i, (uint16_t)(total ? app_random() % (total + 1u) : 0));
                put32(p + 20, app_random() % 3 ? 10 : 11);
                p[24] = app_random() % 6 != 0;
            }
            if (type == 0x51 && app_random() % 2) {
                /* Well-formed live cue or trigger. */
                n = 26;
                put64(p, a.epoch);
                put32(p + 8, reminders.seq + 1);
                put32(p + 12, app_random() % 3 ? 10 : 11);
                put32(p + 16, 20);
                put32(p + 20, app_random());
                p[24] = (uint8_t)(app_random() % 4);
                p[25] = (uint8_t)(app_random() % 6);
                if (p[25] == 4)
                    p[25] = 5;
            }
            apply_message((whale_msg_t){type, p, n});
            break;
        }
        case 3:
        case 4: {
            unsigned k = app_random() % 3;
            unsigned ev = app_random() % 2 ? BSP_BTN_LONG : BSP_BTN_RELEASE;
            service((whale_event_t){.kind = WE_KEY, .value = k | (unsigned)BSP_BTN_PRESS << 8});
            service((whale_event_t){.kind = WE_KEY, .value = k | ev << 8});
            break;
        }
        case 5:
            a.linked = app_random() % 4 != 0;
            a.ready = a.linked;
            if (!a.linked)
                a.global_valid = false;
            else if (app_random() % 8 == 0)
                a.global_cap = !a.global_cap; /* an old bridge reconnects, or a new one */
            break;
        case 6:
            fake_voice_busy = app_random() % 4 == 0;
            break;
        default: {
            static const uint32_t jumps[] = {125, 3000, 30000, WHALE_DESK_IDLE_MS,
                                             WHALE_POWER_OFF_MS};
            a.now += jumps[app_random() % 5];
            break;
        }
        }
        companion_tick();
        power_step();
        app_fuzz_check(seed, step, action);
    }
}
static void test_random_app_events(void) {
    const char *one = getenv("WHALE_APP_FUZZ_SEED");
    if (one && *one) {
        app_fuzz_one(strtoull(one, NULL, 10), 3000);
        return;
    }
    for (uint64_t seed = 1; seed <= 200; seed++)
        app_fuzz_one(seed, 3000);
}

int main(void) {
    test_random_invitations();
    test_voice_new_task();
    test_power();
    test_bond_daily_cap();
    test_separate_work_and_tap_quota();
    test_bond_work_and_unlocks();
    test_bond_cooling();
    test_voice_retake();
    test_wardrobe_screen_off();
    test_large_frame_negotiation();
    test_settings_navigation();
    test_companion_and_global_cues();
    test_desk_display();
    test_detail_and_reading();
    test_navigation_and_questions();
    test_rich_questions();
    test_new_reply_and_lifecycle();
    test_failure_empty_battery_and_key_release();
    test_continuous_cache_and_offline();
    test_unread_route_before_preview();
    test_completion_cues();
    test_voice_and_attention();
    test_voice_direct_entry();
    test_pending_queue();
    test_random_app_events();
    puts("Whale R11.1 application state, direct voice entry, completion cue "
         "and power policy tests PASS");
    return 0;
}

void esp_fill_random(void *p, size_t n) { memset(p, 7, n); }
bool whale_ble_audio_ready(void) { return true; }
bool whale_ble_audio_send(const uint8_t *p, size_t n) {
    (void)p;
    (void)n;
    return true;
}
bool whale_voice_begin(const uint8_t id[24]) {
    (void)id;
    voice_starts++;
    return true;
}
void whale_voice_stop(bool discard) { (void)discard; }
void whale_voice_ack(const uint8_t id[24], uint32_t next) {
    (void)id;
    (void)next;
}
bool whale_voice_busy(void) { return fake_voice_busy; }
bool whale_voice_pending(void) { return fake_voice_pending; }
void whale_voice_run(void) {}
uint32_t whale_voice_milliseconds(void) { return 0; }

static uint32_t fake_audio_generation;
void whale_audio_interrupt(void) { fake_audio_generation++; }
uint32_t whale_audio_generation(void) { return fake_audio_generation; }
bool whale_audio_play(whale_audio_cmd_t c) {
    (void)c;
    return true;
}
uint8_t whale_sound_load(void) { return 1; }
bool whale_sound_save(uint8_t mode) { return mode < 2; }
uint8_t whale_standby_load(void) { return 0; }
bool whale_standby_save(uint8_t mode) { return mode < 2; }
bool whale_blob_load(const char *key, void *data, size_t size) {
    if (stored_room_key && !strcmp(key, stored_room_key) && size == stored_room_size) {
        memcpy(data, stored_room, size);
        return true;
    }
    return false;
}
bool whale_blob_save(const char *key, const void *data, size_t size) {
    (void)key;
    (void)data;
    (void)size;
    return true;
}
void whale_ble_set_slow(bool slow) { slow_adv_set = slow; }
whale_sleep_info_t whale_sleep_boot_info(void) { return (whale_sleep_info_t){0}; }
const char *whale_sleep_boot_name(whale_boot_t boot) { return boot ? "wake" : "power"; }
static whale_clock_t fake_rtc_clock;
whale_clock_t *whale_sleep_clock(void) { return &fake_rtc_clock; }
bool whale_sleep_enter(uint32_t timer_ms, uint32_t cycles) {
    (void)timer_ms;
    (void)cycles;
    return false;
}

bool whale_audio_busy(void) { return false; }
