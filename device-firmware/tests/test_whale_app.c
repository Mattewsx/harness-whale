#include "../main/whale_app.c"

static uint8_t sent_type, sent_bytes[64];
static size_t sent_length;
static unsigned sends, answers, voice_starts;
static unsigned cue_count, audio_plays, sleep_commands;
static bool slow_adv_set;
static int backlight_level = -1;
static bool cue_ok, bad_snapshot_crc;
static bool send_ok = true;
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
        assert(wait == 0 && (*(const uint8_t *)in >= 1 && *(const uint8_t *)in <= 7));
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
    bond_taps = 0;
    bond_taps_day = 0;
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
static void snapshot(bool keep_first) {
    uint8_t begin[27] = {0}, row[37] = {0}, end[17] = {0};
    put64(begin, a.epoch);
    put32(begin + 8, 1);
    put32(begin + 20, UINT32_MAX);
    put16(begin + 24, 1);
    begin[26] = 1;
    uint32_t crc = whale_crc(0, begin, sizeof begin);
    apply_message((whale_msg_t){0x11, begin, sizeof begin});
    put64(row, a.epoch);
    put32(row + 8, 1);
    put32(row + 13, keep_first ? 10 : 11);
    put32(row + 17, keep_first ? 20 : 21);
    row[21] = 5;
    crc = whale_crc(crc, row + 13, 24);
    apply_message((whale_msg_t){0x12, row, sizeof row});
    put64(end, a.epoch);
    put32(end + 8, 1);
    end[12] = 1;
    put32(end + 13, bad_snapshot_crc ? crc ^ 1 : crc);
    apply_message((whale_msg_t){0x13, end, sizeof end});
}
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
    puts("Desk display entry, wall clock sync, exits, night and offline power PASS");
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
    assert(a.view == VERSION);
    key(BSP_BTN_OK);
    assert(a.view == MANAGE && a.menu_choice == 2);
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
    assert(a.worn == 2);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.worn == 6);
    key(BSP_BTN_OK);
    assert(a.worn == 2);
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
    /* Viewing the question clears its unread mark, but the session still
     * waits for an answer, so the home badge keeps counting it. */
    assert(a.global_unread == 1 && !(a.rows[0].flags & 32) && sent_type == 0x4a &&
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
    assert(tx_frame == WHALE_FRAME_LEGACY && rx.frame == 0);

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
    assert(sent.kind == WHALE_CMD_ROOM_SAVE && (uint8_t)(sent.generation >> 6) == 0x23);
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
    assert(a.bond == WHALE_BOND_MAX && a.bond_today == 2 && a.bond_day == today + 3);
    /* The save carries bond, the daily count and the day. */
    room_unsaved = true;
    companion_tick();
    last_cue_capture = NULL;
    assert(!room_unsaved && sent.kind == WHALE_CMD_ROOM_SAVE && sent.mode == WHALE_BOND_MAX);
    assert((uint8_t)(sent.generation >> 6) == 2 && (sent.generation >> 14) == today + 3);
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
    assert(a.worn == 2 && !a.locked_notice);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.locked_notice && a.worn == 2);
    assert(whale_unlocks_for(19) == 0 && whale_unlocks_for(20) == 2 &&
           whale_unlocks_for(49) == 2 && whale_unlocks_for(50) == 6);
    /* Reaching fifty unlocks the glasses as well. */
    a.bond = 48;
    a.bond_today = 0;
    answer_accepted();
    assert(a.unlocked == 6 && (a.unlock_new & 4));
    puts("Bond from work, daily caps and accessory unlocks PASS");
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
int main(void) {
    test_power();
    test_bond_daily_cap();
    test_bond_work_and_unlocks();
    test_voice_retake();
    test_wardrobe_screen_off();
    test_large_frame_negotiation();
    test_settings_navigation();
    test_companion_and_global_cues();
    test_desk_display();
    test_detail_and_reading();
    test_navigation_and_questions();
    test_new_reply_and_lifecycle();
    test_failure_empty_battery_and_key_release();
    test_continuous_cache_and_offline();
    test_unread_route_before_preview();
    test_completion_cues();
    test_voice_and_attention();
    test_voice_direct_entry();
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
bool whale_voice_busy(void) { return false; }
bool whale_voice_pending(void) { return false; }
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
bool whale_blob_load(const char *key, void *data, size_t size) {
    (void)key;
    (void)data;
    (void)size;
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
