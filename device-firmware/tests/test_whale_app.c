#include "../main/whale_app.c"

static uint8_t sent_type, sent_bytes[64];
static size_t sent_length;
static unsigned sends, answers, voice_starts;
static unsigned cue_count, audio_plays;
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
int xQueueSend(void *q, const void *in, int wait) {
    if (q == cues) {
        assert(wait == 0 && (*(const uint8_t *)in >= 1 && *(const uint8_t *)in <= 6));
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
void bsp_display_backlight(int percent) { (void)percent; }
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
    a = (whale_app_t){.soc = -1,
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
    requested_cursor = 0;
    sends = answers = 0;
    voice_starts = 0;
    memset(pending_reads, 0, sizeof pending_reads);
    pending_read_next = 0;
    send_ok = true;
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
    assert(a.view == HOME && sends == 0);
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
    assert(a.view == HOME);
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_READ && sends == before);
    key(BSP_BTN_OK);
    assert(a.view == HOME);
    reply(10, 30, 0, "late");
    assert(a.view == HOME);
}
static void test_navigation_and_questions(void) {
    setup();
    key(BSP_BTN_OK);
    assert(a.view == LIST);
    key(BSP_BTN_DOWN);
    assert(a.selected == 1);
    key(BSP_BTN_OK);
    assert(a.view == HOME && sends == 0);
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_LOAD && w32(sent_bytes + 8) == 11);
    key(BSP_BTN_OK);
    unsigned before = sends;
    key(BSP_BTN_UP);
    assert(a.view == COMPANION && sends == before);
    key(BSP_BTN_OK | 256);
    assert(a.view == HOME);
    a.rows[1].question = 40;
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.view == HOME && answers == 0);
    key(BSP_BTN_OK);
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
    assert(a.view == HOME && !a.answer[0]);
}
static void test_failure_empty_battery_and_key_release(void) {
    setup();
    a.replies[0].revision = 0;
    key(BSP_BTN_DOWN);
    assert(a.view == HOME && sends == 0);
    preview(10, 30);
    send_ok = false;
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_FAILED);
    send_ok = true;
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_LOAD);
    service((whale_event_t){.kind = WE_DOWN});
    key(BSP_BTN_OK);
    assert(a.view == HOME);
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
    assert(a.view == HOME && sends == before);
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
    key(BSP_BTN_UP | 256);
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
    assert(a.voice_phase == VOICE_PREVIEW && sends == before && !strcmp(a.voice_text, "hello"));
    key(BSP_BTN_OK);
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
    assert(a.view == HOME);
    setup();
    a.rows[0].flags = 24;
    a.rows[0].state = 6;
    page(LIST);
    key(BSP_BTN_OK);
    assert(a.view == HOME && !(a.rows[0].flags & 16) && a.rows[0].state == 6 &&
           (a.rows[0].flags & 8));
    preview(10, 30);
    key(BSP_BTN_DOWN);
    assert(a.rows[0].flags & 8);
    reply(10, 30, 0, "read this");
    assert(!(a.rows[0].flags & 8) && sent_type == 0x4a);
    a.rows[0].flags |= 8;
    page(HOME);
    a.ready = false;
    before = sends;
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_READ && !(a.rows[0].flags & 8) && sends == before);
    a.ready = true;
    flush_reads();
    assert(sends == before + 1 && sent_type == 0x4a);
}

/* BLE OTA glue (whale_ota.c) is replaced by recorders. */
static int ota_replies[2], ota_cancels, restarts;
void whale_ota_boot(void) {}
bool whale_ota_pending(void) { return false; }
void whale_ota_assets_checked(bool ok) { (void)ok; }
bool whale_assets_init(bool full) {
    (void)full;
    return true;
}
static int ota_healthy;
void whale_ota_healthy(void) { ota_healthy++; }
void whale_ota_reply(bool yes) { ota_replies[yes]++; }
void whale_ota_cancel(void) { ota_cancels++; }
void whale_ota_offer(char *version, size_t cap, uint32_t *size) {
    snprintf(version, cap, "%s", "whale-v1.4.0");
    *size = 2200000;
}
void esp_restart(void) { restarts++; }
static void ota_event(uint8_t state, uint8_t code, uint8_t percent) {
    service((whale_event_t){.kind = WE_OTA,
                            .value = WHALE_OTA_EVENT(state, code, percent)});
}
static void test_ota_pages(void) {
    setup();
    a.view = LIST;
    a.soc = 80;
    ota_event(WOTA_CONFIRM, WOTA_OK, 0);
    assert(a.view == OTA_CONFIRM && !strcmp(a.ota_version, "whale-v1.4.0"));
    assert(a.ota_size == 2200000);
    key(BSP_BTN_UP);
    assert(a.view == OTA_CONFIRM && !ota_replies[0] && !ota_replies[1]);
    key(BSP_BTN_OK);
    assert(ota_replies[1] == 1);
    ota_event(WOTA_RECEIVING, WOTA_OK, 0);
    assert(a.view == OTA_PROGRESS);
    ota_event(WOTA_RECEIVING, WOTA_OK, 42);
    assert(a.view == OTA_PROGRESS && a.ota_percent == 42);
    key(BSP_BTN_OK); /* a short press does not cancel */
    assert(!ota_cancels);
    key(BSP_BTN_OK | 256);
    assert(ota_cancels == 1);
    ota_event(WOTA_CANCELLED, WOTA_OK, 42);
    assert(a.view == OTA_RESULT);
    key(BSP_BTN_OK);
    assert(a.view == HOME);

    /* Long press declines; a decline elsewhere does not steal the screen. */
    ota_event(WOTA_CONFIRM, WOTA_OK, 0);
    key(BSP_BTN_OK | 256);
    assert(ota_replies[0] == 1);

    /* Low battery: refused without asking. */
    setup();
    a.soc = 12;
    ota_replies[0] = 0;
    ota_event(WOTA_CONFIRM, WOTA_OK, 0);
    assert(ota_replies[0] == 1 && a.view == OTA_RESULT && a.ota_low_battery);
    ota_event(WOTA_CANCELLED, WOTA_E_DECLINED, 0);
    assert(a.view == OTA_RESULT);

    /* Success: the result page restarts after 1.5 s; keys cannot leave it. */
    setup();
    a.view = OTA_PROGRESS;
    ota_event(WOTA_VERIFYING, WOTA_OK, 100);
    assert(a.view == OTA_PROGRESS);
    ota_event(WOTA_DONE, WOTA_OK, 100);
    assert(a.view == OTA_RESULT);
    key(BSP_BTN_OK);
    assert(a.view == OTA_RESULT);

    /* An authenticated link completes the self-test. */
    ota_healthy = 0;
    service((whale_event_t){.kind = WE_LINK});
    assert(ota_healthy == 1);
    puts("BLE OTA confirmation, progress, cancel, low battery and self-test PASS");
}
static void test_voice_direct_entry(void) {
    setup();
    a.voice_cap = true;
    service((whale_event_t){.kind = WE_KEY, .value = BSP_BTN_UP | (BSP_BTN_PRESS << 8)});
    service((whale_event_t){.kind = WE_KEY, .value = BSP_BTN_UP | (BSP_BTN_LONG << 8)});
    assert(a.view == VOICE && a.voice_phase == VOICE_PREPARE && sends == 1);
    service((whale_event_t){.kind = WE_KEY, .value = BSP_BTN_UP | (BSP_BTN_RELEASE << 8)});
    assert(a.view == VOICE && a.voice_phase == VOICE_PREPARE && sends == 1);
    uint8_t ready[27] = {0};
    memcpy(ready, a.voice_identity, 24);
    ready[24] = 1;
    key(BSP_BTN_UP); /* cancellation before readiness must never open the mic */
    apply_message((whale_msg_t){0x41, ready, sizeof ready});
    assert(a.view == HOME && voice_starts == 0);
    setup();
    a.ready = false;
    key(BSP_BTN_UP | 256);
    assert(a.voice_phase == VOICE_DISCONNECTED && voice_starts == 0 && sends == 0);
    setup();
    key(BSP_BTN_UP | 256);
    assert(a.voice_phase == VOICE_UNSUPPORTED && voice_starts == 0 && sends == 0);
    setup();
    a.voice_cap = true;
    a.rows[0].question = 40;
    key(BSP_BTN_UP | 256);
    assert(a.voice_phase == VOICE_QUESTION && voice_starts == 0 && sends == 0);
    setup();
    a.count = 0;
    key(BSP_BTN_UP | 256);
    assert(a.view == HOME && voice_starts == 0 && sends == 0);
    setup();
    a.attention_cap = true;
    key(BSP_BTN_OK);
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
    key(BSP_BTN_OK);
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
    assert(a.view == HOME); /* background session blocks auto */
    summary(0, 0, 1);
    companion_tick();
    assert(a.view == HOME);
    summary(0, 0, 0);
    companion_tick();
    assert(a.view == COMPANION && last_input == 0);
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
    key(BSP_BTN_UP);
    assert(a.view == COMPANION);
    cue_event(1, 10, 10, 0, 5);
    assert(a.view == HOME); /* run start interrupts */
    key(BSP_BTN_UP);
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
    assert(a.view == HOME); /* old bridge */
    a.linked = false;
    companion_tick();
    assert(a.view == COMPANION); /* offline allowed */
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
    key(BSP_BTN_OK | 256);
    assert(a.view == MANAGE);
    key(BSP_BTN_OK);
    assert(a.view == SOUND);
    a.sound_choice = 1;
    key(BSP_BTN_OK);
    assert(a.sound_mode == 1 && a.sound_saved == 0);
    service((whale_event_t){.kind = 98, .value = 1});
    assert(a.sound_saved == 1);
    key(BSP_BTN_OK | 256);
    assert(a.view == MANAGE);
    key(BSP_BTN_OK | 256);
    assert(a.view == HOME);
    puts("Companion actions, global idle, interruption, deferred/merged cues and "
         "settings PASS");
}
static void key_event(unsigned k, unsigned ev) {
    service((whale_event_t){.kind = WE_KEY, .value = k | (ev << 8)});
}
static void test_settings_navigation(void) {
    setup();
    a.selected = 1;
    key_event(BSP_BTN_OK, BSP_BTN_PRESS);
    assert(a.view == HOME && a.confirm_holding && a.confirm_hold_ms == 500 && sends == 0);
    a.now += 200;
    key_event(BSP_BTN_OK, BSP_BTN_RELEASE);
    assert(a.view == LIST && !a.confirm_holding && a.selected == 1 && !a.list_settings);
    key(BSP_BTN_UP);
    key(BSP_BTN_UP);
    assert(a.list_settings && a.selected == 0);
    a.rows[0].flags = 16;
    key(BSP_BTN_OK);
    assert(a.view == MANAGE && a.settings_return == LIST && a.rows[0].flags == 16);
    key(BSP_BTN_OK);
    assert(a.view == SOUND);
    key(BSP_BTN_OK | 256);
    assert(a.view == MANAGE && a.settings_return == LIST);
    a.menu_choice = 2;
    key(BSP_BTN_OK);
    assert(a.view == VERSION);
    key(BSP_BTN_OK);
    assert(a.view == MANAGE);
    a.menu_choice = 3;
    key(BSP_BTN_OK);
    assert(a.view == LIST && a.list_settings);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.view == HOME && !a.list_settings && !(a.rows[0].flags & 16));

    setup();
    key_event(BSP_BTN_OK, BSP_BTN_PRESS);
    a.now += 500;
    key_event(BSP_BTN_OK, BSP_BTN_LONG);
    assert(a.view == MANAGE && !a.confirm_holding && sends == 0);
    key_event(BSP_BTN_OK, BSP_BTN_RELEASE);
    assert(a.view == MANAGE); /* release must not open the sound submenu */
    key(BSP_BTN_OK | 256);
    assert(a.view == HOME);
    dim = true;
    key_event(BSP_BTN_OK, BSP_BTN_PRESS);
    assert(!a.confirm_holding && !dim);
    key_event(BSP_BTN_OK, BSP_BTN_LONG);
    key_event(BSP_BTN_OK, BSP_BTN_RELEASE);
    assert(a.view == HOME); /* waking consumes the whole gesture */
    key_event(BSP_BTN_OK, BSP_BTN_PRESS);
    page(PAIR);
    key_event(BSP_BTN_OK, BSP_BTN_RELEASE);
    assert(a.view == PAIR && !a.confirm_holding);

    setup();
    a.count = 0;
    a.linked = a.ready = false;
    key(BSP_BTN_OK);
    assert(a.view == LIST && a.list_settings);
    key(BSP_BTN_DOWN);
    key(BSP_BTN_OK);
    assert(a.view == MANAGE && a.settings_return == LIST);
    key(BSP_BTN_OK | 256);
    assert(a.view == LIST && a.list_settings);

    setup();
    a.attention_cap = a.global_cap = true;
    key(BSP_BTN_OK);
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
    assert(a.list_settings && a.cursor == 8);
    key(BSP_BTN_UP);
    assert(a.view == LIST_LOAD && requested_cursor == 0);
    puts("Settings entry, hold/release, return, offline and paged list "
         "navigation PASS");
}
int main(void) {
    test_settings_navigation();
    test_companion_and_global_cues();
    test_detail_and_reading();
    test_navigation_and_questions();
    test_new_reply_and_lifecycle();
    test_failure_empty_battery_and_key_release();
    test_continuous_cache_and_offline();
    test_completion_cues();
    test_voice_and_attention();
    test_voice_direct_entry();
    test_ota_pages();
    puts("Whale R11.1 application state, direct voice entry and completion cue "
         "tests PASS");
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

bool whale_audio_busy(void) { return false; }
