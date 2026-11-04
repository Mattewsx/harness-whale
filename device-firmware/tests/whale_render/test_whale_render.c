#include "lvgl.h"
#include "whale_app.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint8_t poses[16 * 196 * 196 * 3] __asm__("_binary_poses_rgb_start");
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
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_buffers(display, buffer, NULL, sizeof buffer, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    whale_ui_init();
    whale_app_t a = {.view = HOME, .soc = 94, .linked = true, .ready = true,
                     .count = 1, .total = 1, .now = 1000};
    a.rows[0] = (whale_row_t){.session = 1, .state = 5, .elapsed = 461, .at = 1000};
    strcpy(a.rows[0].title, "A completed task");
    a.replies[0].session = 1;
    a.replies[0].revision = 1;
    a.read_revision = a.read_count = 1;
    strcpy(a.answer, "Reply line one\nReply line two");
    const whale_page_t pages[] = {HOME, LIST, REPLY_READ};
    unsigned errors = 0;
    for (unsigned i = 0; i < sizeof pages / sizeof pages[0]; i++) {
        a.view = pages[i];
        draw(&a);
        unsigned pixels = draw(&a);
        printf("same page %d: %u pixels redrawn (expected 0)\n", a.view, pixels);
        errors += pixels != 0;
    }
    a.view = HOME;
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
    strcpy(a.answer, "A newer reply");
    assert(draw(&a) > 0);
    a.view = HOME;
    a.rows[0].state = 2;
    draw(&a);
    a.now = 1800;
    assert(draw(&a) > 0); /* Character motion still refreshes. */
    for (unsigned i = 0; i < 300; i++) {
        a.view = i % 2 ? HOME : REPLY_READ;
        a.rows[0].state = i % 9;
        a.now += 100;
        draw(&a);
    }
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    assert(memory.free_size > 4096 && lv_mem_test() == LV_RESULT_OK);
    printf("render regression: %s, peak pool %lu bytes\n", errors ? "FAIL" : "PASS",
           (unsigned long)memory.max_used);
    return errors ? 1 : 0;
}
