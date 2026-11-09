#include "lvgl.h"
#include "whale_app.h"
#include "whale_assets.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint8_t landscape_pixels[2 * 240 * 228 * 2] __asm__("_binary_room_rgb_start");
uint8_t poses[16 * 196 * 196 * 3] __asm__("_binary_poses_rgb_start");
uint8_t companion[WHALE_COMPANION_BYTES] __asm__("_binary_companion_rgb_start");
static uint16_t buffer[240 * 40];
static unsigned flushed_pixels;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    (void)data;
    flushed_pixels += lv_area_get_width(area) * lv_area_get_height(area);
    lv_display_flush_ready(display);
}
static unsigned draw(whale_app_t *a) {
    flushed_pixels = 0;
    whale_ui_draw(a);
    lv_refr_now(NULL);
    return flushed_pixels;
}
int main(void) {
    FILE *file = fopen(WHALE_POSES_PATH, "rb");
    assert(file && fread(poses, 1, sizeof poses, file) == sizeof poses);
    fclose(file);
    file = fopen(WHALE_COMPANION_PATH, "rb");
    assert(file && fread(companion, 1, sizeof companion, file) == sizeof companion);
    fclose(file);
    FILE *landscape = fopen(WHALE_ROOM_PATH, "rb");
    assert(landscape && fread(landscape_pixels, 1, sizeof landscape_pixels, landscape) ==
                            sizeof landscape_pixels);
    fclose(landscape);
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_buffers(display, buffer, NULL, sizeof buffer, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    whale_ui_init();
    whale_app_t a = {.view = TASK_DETAIL,
                     .soc = 94,
                     .linked = true,
                     .ready = true,
                     .count = 1,
                     .total = 1,
                     .now = 1000};
    a.rows[0] = (whale_row_t){.session = 1, .state = 5, .elapsed = 461, .at = 1000};
    strcpy(a.rows[0].title, "A completed task");
    a.replies[0].session = 1;
    a.replies[0].revision = 1;
    a.read_revision = a.read_count = 1;
    strcpy(a.answer, "Reply line one\nReply line two");
    const whale_page_t pages[] = {HOME, TASK_DETAIL, LIST, REPLY_READ, WARDROBE};
    unsigned errors = 0;
    for (unsigned i = 0; i < sizeof pages / sizeof pages[0]; i++) {
        a.view = pages[i];
        draw(&a);
        unsigned pixels = draw(&a);
        printf("same page %d: %u pixels redrawn (expected 0)\n", a.view, pixels);
        errors += pixels != 0;
    }
    a.view = LIST;
    draw(&a);
    strcpy(a.replies[0].preview, "PRIVATE REPLY MUST NOT APPEAR IN LIST");
    assert(draw(&a) == 0); /* reply content cannot change the list */
    a.rows[0].state = 6;
    assert(draw(&a) > 0); /* status changes remain visible */
    a.count = a.total = 2;
    a.rows[1] = a.rows[0];
    a.rows[1].flags = 0;
    draw(&a);
    for (int state = 0; state <= 8; state++) {
        a.rows[1].state = state;
        assert(draw(&a) > 0); /* A non-selected row changes without a new central pose. */
        assert(draw(&a) == 0);
    }
    a.rows[1].flags = 32;
    assert(draw(&a) > 0);
    a.rows[1].flags = 0;
    assert(draw(&a) > 0); /* Reading the question only removes the unread dot. */
    a.count = a.total = 1;
    a.rows[0].state = 5;
    a.view = TASK_DETAIL;
    draw(&a);
    a.rows[0].elapsed++;
    unsigned pixels = draw(&a);
    printf("clock-only update: %u pixels redrawn (limit 10000)\n", pixels);
    errors += !pixels || pixels >= 10000;
    a.soc = 41;
    pixels = draw(&a);
    printf("battery-only update: %u pixels redrawn (limit 5000)\n", pixels);
    errors += !pixels || pixels >= 5000;
    a.view = REPLY_READ;
    draw(&a);
    strcpy(a.rows[0].title, "A long task title that must be shortened in the reply reader");
    draw(&a);
    assert(draw(&a) == 0);
    a.title_session = a.rows[0].session;
    a.title_run = a.rows[0].run;
    /* Very narrow or unsupported glyphs can fit the pixel budget before the
     * byte budget. The reply header must leave space for its ellipsis. */
    memset(a.full_title, 1, sizeof a.full_title - 1);
    a.full_title[sizeof a.full_title - 1] = 0;
    draw(&a);
    a.full_title[0] = 0;
    strcpy(a.answer, "A newer reply");
    assert(draw(&a) > 0);
    a.view = TASK_DETAIL;
    a.rows[0].state = 2;
    draw(&a);
    a.now = 1800;
    assert(draw(&a) > 0); /* Character motion still refreshes. */
    for (unsigned i = 0; i < 300; i++) {
        a.view = i % 2 ? TASK_DETAIL : REPLY_READ;
        a.rows[0].state = i % 9;
        a.now += 100;
        draw(&a);
    }
    a.view = VOICE;
    strcpy(a.voice_title, "Voice session");
    strcpy(a.voice_text, "Synthetic transcript line one\nAnother line");
    for (unsigned phase = 0; phase <= VOICE_MIC_ERROR; phase++) {
        a.voice_phase = phase;
        draw(&a);
        assert(draw(&a) == 0);
    }
    a.view = COMPANION;
    a.companion_at = a.now;
    for (unsigned action = 0; action <= 4; action++) {
        a.companion_action = action;
        a.action_at = a.now;
        draw(&a);
        for (unsigned frame = 0; frame < 24; frame++) {
            a.now += 125;
            draw(&a);
        }
    }
    a.view = SOUND;
    a.sound_mode = 1;
    a.sound_saved = 1;
    draw(&a);
    assert(draw(&a) == 0);
    a.view = MANAGE;
    draw(&a);
    assert(draw(&a) == 0);
    a.view = LIST;
    a.list_back = true;
    draw(&a);
    assert(draw(&a) == 0);
    a.count = 0;
    draw(&a);
    assert(draw(&a) == 0);
    a.count = 1;
    a.list_back = false;
    a.view = TASK_DETAIL;
    a.rows[0].state = 5;
    a.view = HOME;
    a.dock = 1;
    a.global_unread = 5;
    a.worn = 6;
    for (unsigned frame = 0; frame < 8; frame++) {
        a.activity = frame;
        a.bond = 12 * frame;
        draw(&a);
        assert(draw(&a) == 0);
    }
    /* Desk display: still frames do not redraw; a minute change redraws only
     * the clock; the night clock and the return home repaint fully. */
    a.view = DESK;
    a.page_at = a.now;
    a.clock_valid = true;
    a.clock = (whale_local_time_t){
        .year = 2026, .month = 11, .day = 6, .wday = 5, .hour = 14, .minute = 25};
    draw(&a);
    a.now += 125;
    pixels = draw(&a);
    printf("desk still frame: %u pixels redrawn (expected 0)\n", pixels);
    errors += pixels != 0;
    a.clock.minute = 26;
    pixels = draw(&a);
    printf("desk minute update: %u pixels redrawn (limit 24000)\n", pixels);
    errors += !pixels || pixels >= 24000;
    a.night = true;
    assert(draw(&a) > 0);
    assert(draw(&a) == 0);
    a.clock_valid = false;
    assert(draw(&a) > 0);
    a.night = false;
    a.view = TASK_DETAIL;
    assert(draw(&a) > 0);
    assert(draw(&a) == 0);
    for (unsigned i = 0; i < 100; i++) {
        a.view = i % 3 == 0 ? DESK : i % 3 == 1 ? TASK_DETAIL : COMPANION;
        a.night = i % 2;
        a.now += 125;
        draw(&a);
    }
    /* Approval and multi-select reuse the label pool: no new LVGL objects. */
    a.view = QUESTION;
    a.pages = 1;
    a.options = a.all_options = 3;
    a.option_ids[0] = 0, a.option_ids[1] = 1, a.option_ids[2] = 2;
    a.question_mode = WHALE_MODE_MULTI;
    a.picked = 3;
    a.focus = 3;
    strcpy(a.prompt, "pick parts");
    strcpy(a.labels[0], "summary");
    strcpy(a.labels[1], "log");
    strcpy(a.labels[2], "images");
    draw(&a);
    assert(draw(&a) == 0);
    a.question_mode = WHALE_MODE_APPROVAL;
    a.options = 2;
    a.focus = 1;
    strcpy(a.prompt, "run npm test");
    strcpy(a.labels[0], "allow");
    strcpy(a.labels[1], "deny");
    draw(&a);
    assert(draw(&a) == 0);
    a.view = OPTION;
    a.focus = 0;
    assert(draw(&a) > 0);
    assert(draw(&a) == 0);
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    assert(memory.free_size > 4096 && lv_mem_test() == LV_RESULT_OK);
    printf("render regression: %s, peak pool %lu bytes\n", errors ? "FAIL" : "PASS",
           (unsigned long)memory.max_used);
    return errors ? 1 : 0;
}
