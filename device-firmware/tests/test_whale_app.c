#include "../main/whale_app.c"

static uint8_t sent_type, sent_bytes[64];
static size_t sent_length;
static unsigned sends, answers;
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
int nvs_flash_init(void) { return 0; }
int64_t esp_timer_get_time(void) { return 0; }
void whale_pair(bool yes) { (void)yes; }
bool whale_unbind(void) { return true; }
void whale_ble_start(void) {}
void whale_ui_init(void) {}
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
    send_ok = true;
}
static void reply(uint32_t sid, uint32_t rev, uint32_t part, const char *text) {
    uint8_t p[410] = {0};
    put64(p, a.epoch);
    put32(p + 8, sid);
    put32(p + 12, rev);
    put32(p + 16, part);
    put32(p + 20, 2);
    put16(p + 24, strlen(text));
    memcpy(p + 26, text, strlen(text));
    apply_message((whale_msg_t){0x1b, p, 26 + strlen(text)});
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
    put32(end + 13, crc);
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
    key(BSP_BTN_DOWN);
    assert(a.scroll == 6);
    key(BSP_BTN_DOWN);
    assert(a.scroll == 7);
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_LOAD && a.read_page == 1);
    reply(10, 30, 1, "last");
    key(BSP_BTN_UP);
    assert(a.view == REPLY_LOAD && a.read_page == 0);
    reply(10, 30, 0, "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13");
    assert(a.scroll == 7);
    key(BSP_BTN_UP);
    assert(a.scroll == 1);
    key(BSP_BTN_UP);
    assert(a.scroll == 0);
    key(BSP_BTN_UP);
    assert(a.view == HOME);
    key(BSP_BTN_DOWN);
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
    key(BSP_BTN_UP);
    assert(a.view == TITLE && sent_type == 0x16 && w32(sent_bytes + 8) == 11);
    key(BSP_BTN_OK);
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
    assert(a.view == REPLY_READ && a.read_revision == 30 && !strcmp(a.answer, "reading"));
    key(BSP_BTN_DOWN);
    assert(a.view == REPLY_LOAD && a.read_revision == 32);
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
int main(void) {
    test_detail_and_reading();
    test_navigation_and_questions();
    test_new_reply_and_lifecycle();
    test_failure_empty_battery_and_key_release();
    puts("Whale R10 application state and input tests PASS");
    return 0;
}
