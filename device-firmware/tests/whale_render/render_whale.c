#include "lvgl.h"
#include "whale_app.h"
#include "whale_assets.h"
#include "whale_outfits.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint8_t landscape_pixels[2 * 240 * 228 * 2] __asm__("_binary_room_rgb_start");
uint8_t pose_bytes[16 * 196 * 196 * 3] __asm__("_binary_poses_rgb_start");
uint8_t companion_bytes[WHALE_COMPANION_BYTES] __asm__("_binary_companion_rgb_start");
uint8_t outfit_bytes[WHALE_OUTFIT_BYTES] __asm__("_binary_outfits_rgb_start");
static uint16_t pixels[240 * 320];
static uint16_t buffer[240 * 20];
static void check_sky_bar(void) {
    for (int x = 0; x < 240; x++) {
        assert(pixels[x] == pixels[24 * 240 + x]);
        assert(pixels[23 * 240 + x] == pixels[24 * 240 + x]);
    }
}
static void check_action_caption(void) {
    const uint16_t paper = pixels[229 * 240];
    for (int y = 229; y < 232; y++)
        for (int x = 8; x < 232; x++)
            assert(pixels[y * 240 + x] == paper);
    for (int y = 234; y < 246; y++)
        for (int x = 60; x < 180; x++)
            if (pixels[y * 240 + x] != paper)
                return;
    assert(0 && "Missing interaction caption");
}
static void memory(const char *label) {
    lv_mem_monitor_t stats;
    lv_mem_monitor(&stats);
    printf("%s: used=%lu peak=%lu largest=%lu\n", label,
           (unsigned long)(stats.total_size - stats.free_size), (unsigned long)stats.max_used,
           (unsigned long)stats.free_biggest_size);
    fflush(stdout);
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    uint16_t *p = (uint16_t *)data;
    for (int y = area->y1; y <= area->y2; y++)
        for (int x = area->x1; x <= area->x2; x++)
            pixels[y * 240 + x] = *p++;
    lv_display_flush_ready(display);
}
static void save(const char *name) {
    memory(name);
    lv_refr_now(NULL);
    char path[300];
    snprintf(path, sizeof path, "%s.ppm", name);
    FILE *out = fopen(path, "wb");
    assert(out);
    fputs("P6\n240 320\n255\n", out);
    for (int i = 0; i < 240 * 320; i++) {
        unsigned p = pixels[i];
        fputc(((p >> 11) & 31) * 255 / 31, out);
        fputc(((p >> 5) & 63) * 255 / 63, out);
        fputc((p & 31) * 255 / 31, out);
    }
    fclose(out);
}
int main(void) {
    FILE *f = fopen(WHALE_POSES_PATH, "rb");
    assert(f && fread(pose_bytes, 1, sizeof pose_bytes, f) == sizeof pose_bytes);
    fclose(f);
    f = fopen(WHALE_COMPANION_PATH, "rb");
    assert(f && fread(companion_bytes, 1, sizeof companion_bytes, f) == sizeof companion_bytes);
    fclose(f);
    f = fopen(WHALE_OUTFITS_PATH, "rb");
    assert(f && fread(outfit_bytes, 1, sizeof outfit_bytes, f) == sizeof outfit_bytes);
    fclose(f);
    FILE *landscape = fopen(WHALE_ROOM_PATH, "rb");
    assert(landscape && fread(landscape_pixels, 1, sizeof landscape_pixels, landscape) ==
                            sizeof landscape_pixels);
    fclose(landscape);
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_buffers(display, buffer, NULL, sizeof buffer, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    whale_ui_init();
    memory("UI init");
    whale_app_t a = {.soc = 96,
                     .linked = true,
                     .ready = true,
                     .count = 2,
                     .total = 2,
                     .now = 1000,
                     .view = TASK_DETAIL};
    a.rows[0] = (whale_row_t){.session = 1, .run = 2, .state = 2, .elapsed = 461, .at = 1000};
    strcpy(a.rows[0].title, "鲸鱼搭子界面改版");
    a.replies[0].session = 1;
    a.replies[0].revision = 3;
    strcpy(a.replies[0].preview, "已完成任务详情页的设计");
    whale_ui_draw(&a);
    save("detail");
    strcpy(a.rows[0].title, "鲸鱼工作台蓝牙多会话同步与任务详情界面改版");
    whale_ui_draw(&a);
    save("long-title");
    strcpy(a.rows[0].title, "鲸鱼搭子界面改版");
    a.view = REPLY_READ;
    a.read_count = 3;
    a.read_revision = 3;
    strcpy(a.answer, "已完成任务详情页的设计。\n鲸鱼娘放大到画面中央，\n标题、状态和用时保"
                     "留。\n\n按下键展开最近回复，\n确认键随时收回详情。");
    whale_ui_draw(&a);
    save("reply");
    a.view = TASK_DETAIL;
    a.rows[0].state = 5;
    whale_ui_draw(&a);
    save("done");
    a.rows[0].state = 3;
    a.rows[0].tool = 1;
    a.rows[0].flags = 1;
    a.rows[0].done = 2;
    a.rows[0].total = 5;
    whale_ui_draw(&a);
    save("tool");
    a.rows[0].state = 6;
    a.rows[0].flags = 0;
    a.soc = 4;
    a.battery_level = 2;
    whale_ui_draw(&a);
    save("error-low-battery");
    a.rows[0].state = 2;
    a.rows[0].elapsed = 12345;
    a.soc = -1;
    whale_ui_draw(&a);
    save("hours-unknown-battery");
    a.view = REPLY_FAILED;
    whale_ui_draw(&a);
    save("read-failed");
    a.view = LIST;
    a.rows[1] = a.rows[0];
    a.rows[1].session = 2;
    a.rows[1].state = 5;
    whale_ui_draw(&a);
    save("list");
    a.view = TASK_DETAIL;
    a.soc = 68;
    a.battery_level = 0;
    a.rows[0].elapsed = 461;
    for (int state = 0; state < 9; state++) {
        char name[40];
        snprintf(name, sizeof name, "state-%d", state);
        a.rows[0].state = state;
        whale_ui_draw(&a);
        save(name);
    }
    a.rows[0].state = 3;
    for (int hint = 1; hint <= 5; hint++) {
        char name[40];
        snprintf(name, sizeof name, "hint-%d", hint);
        a.rows[0].hint = hint;
        whale_ui_draw(&a);
        save(name);
    }
    a.rows[0].hint = 0;
    for (int tool = 0; tool < 8; tool++) {
        char name[40];
        snprintf(name, sizeof name, "tool-%d", tool);
        a.rows[0].tool = tool;
        whale_ui_draw(&a);
        save(name);
    }
    for (int i = 0; i < 300; i++) {
        a.view = (i % 2) ? TASK_DETAIL : REPLY_READ;
        a.rows[0].state = i % 9;
        a.now += 100;
        whale_ui_draw(&a);
        lv_refr_now(NULL);
    }
    a.view = VOICE;
    a.soc = 68;
    a.voice_ms = 12400;
    strcpy(a.voice_title, "整理蓝牙连接文档");
    strcpy(a.voice_text, "请把今天完成的修改整理成说明，先列出已经验证的功能，再"
                         "列出仍然需要实机测试的项目。\n暂时不要提交或发布。");
    for (int i = 0; i <= VOICE_NO_TARGET; i++) {
        a.voice_phase = i;
        char name[40];
        snprintf(name, sizeof name, "r11-voice-%02d", i);
        whale_ui_draw(&a);
        save(name);
    }
    /* Spoken new task from the home page: folder label, push-to-talk, result. */
    a.voice_new = a.voice_cap = a.voice_task_cap = true;
    strcpy(a.voice_title, "新任务 · harness-whale");
    strcpy(a.voice_text, "把登录页的 bug 修一下");
    static const int task_phases[] = {VOICE_PREPARE, VOICE_RECORD, VOICE_ACTIONS, VOICE_SENT,
                                      VOICE_NO_TARGET};
    for (unsigned i = 0; i < sizeof task_phases / sizeof task_phases[0]; i++) {
        a.voice_phase = task_phases[i];
        char name[40];
        snprintf(name, sizeof name, "voice-task-%02d", task_phases[i]);
        whale_ui_draw(&a);
        save(name);
    }
    a.voice_new = a.voice_cap = a.voice_task_cap = false;
    a.view = LIST;
    a.count = a.total = 3;
    for (int i = 0; i < 3; i++) {
        a.rows[i] = a.rows[0];
        a.rows[i].session = i + 1;
    }
    a.rows[0].state = 5;
    a.rows[0].flags = 8;
    strcpy(a.rows[0].title, "整理连接说明");
    a.rows[1].state = 8;
    a.rows[1].question = 1;
    a.rows[1].flags = 2;
    strcpy(a.rows[1].title, "等待选择导出格式");
    a.rows[2].state = 6;
    a.rows[2].flags = 16;
    strcpy(a.rows[2].title, "检查构建失败原因");
    a.view = HOME;
    a.activity = 5;
    a.dock = 1;
    a.bond = 49;
    a.global_unread = 3;
    whale_ui_draw(&a);
    save("room-home");
    check_sky_bar();
    int saved_soc = a.soc;
    a.soc = 93;
    whale_ui_draw(&a);
    save("room-battery-93");
    a.soc = 100;
    whale_ui_draw(&a);
    save("room-battery-full");
    a.soc_stale = true;
    whale_ui_draw(&a);
    save("room-battery-stale");
    a.soc_stale = false;
    a.soc = saved_soc;
    a.night = true;
    a.activity = 2;
    whale_ui_draw(&a);
    save("room-night");
    check_sky_bar();
    a.night = false;
    a.view = WARDROBE;
    whale_ui_draw(&a);
    save("room-wardrobe");
    a.locked_notice = true;
    whale_ui_draw(&a);
    save("room-wardrobe-locked");
    check_action_caption();
    a.locked_notice = false;
    a.unlocked = 14;
    a.worn = 2;
    a.outfit_choice = 2;
    whale_ui_draw(&a);
    save("room-wardrobe-witch");
    a.worn = 4;
    a.view = HOME;
    whale_ui_draw(&a);
    save("room-home-pajamas");
    a.worn = 8;
    whale_ui_draw(&a);
    save("room-home-lounge");
    a.view = WARDROBE;
    a.outfit_choice = 3;
    whale_ui_draw(&a);
    save("room-wardrobe-lounge");
    a.unlocked = 0;
    a.outfit_choice = 1;
    whale_ui_draw(&a);
    save("room-wardrobe-preview-locked");
    a.unlocked = 14;

    a.view = COMPANION;
    a.unlock_new = 4;
    whale_ui_draw(&a);
    save("room-companion-unlock");
    a.unlock_new = 0;
    a.worn = 0;
    a.unlocked = 0;
    a.view = LIST;
    a.total = 9;
    whale_row_t saved = a.rows[0];
    a.rows[0] = a.rows[1];
    a.rows[1] = a.rows[2];
    a.rows[2] = saved;
    whale_ui_draw(&a);
    save("r11-list");
    /* Sample inside the completed circle, clear of its rim, tick and unread dot. */
    uint16_t done_fill = pixels[289 * 240 + 21];
    assert(done_fill != pixels[289 * 240 + 36]);
    a.rows[2].flags &= ~8u;
    whale_ui_draw(&a);
    save("list-done-read");
    assert(pixels[289 * 240 + 21] == done_fill);
    a.rows[2].flags |= 8;
    a.view = COMPANION;
    a.soc = 76;
    a.companion_at = a.now;
    for (unsigned action = 0; action <= 4; action++) {
        a.companion_action = action;
        a.companion_choice = action ? action - 1 : 0;
        a.action_at = a.now;
        char name[40];
        snprintf(name, sizeof name, "companion-%u", action);
        whale_ui_draw(&a);
        save(name);
        if (action)
            check_action_caption();
    }
    a.now += 2400;
    whale_ui_draw(&a);
    save("coffee-finished");
    check_action_caption();
    a.companion_action = 0;
    a.linked = a.ready = false;
    whale_ui_draw(&a);
    save("offline-companion");
    a.linked = a.ready = true;
    a.view = SOUND;
    a.sound_mode = 1;
    a.sound_choice = 1;
    a.sound_saved = 1;
    whale_ui_draw(&a);
    save("sound-settings");
    a.view = MANAGE;
    whale_ui_draw(&a);
    save("manage");
    a.view = TASK_DETAIL;
    a.rows[0].state = 2;
    a.rows[0].run = 7;
    a.title_session = a.rows[0].session;
    a.title_run = 7;
    strcpy(a.full_title, "整理 DeepSeek Harness 明天的开发计划与语音回复交互验收清单");
    whale_ui_draw(&a);
    save("scroll-start");
    a.now += 6500;
    whale_ui_draw(&a);
    save("scroll-middle");
    a.title_session = 0;
    a.rows[0].state = 1;
    a.view = LIST;
    a.list_back = true;
    whale_ui_draw(&a);
    save("list-parent");
    a.count = 0;
    whale_ui_draw(&a);
    save("list-empty");
    /* Voice new-task row above the first session (first window, voice bridge). */
    a.voice_cap = true;
    a.count = 3;
    a.list_back = false;
    a.selected = 0;
    whale_ui_draw(&a);
    save("list-new-row");
    a.list_new = true;
    whale_ui_draw(&a);
    save("list-new-focus");
    a.count = 0;
    a.list_back = true;
    whale_ui_draw(&a);
    save("list-new-empty");
    a.list_new = a.voice_cap = false;
    a.list_back = false;
    a.count = 1;
    a.list_back = false;
    a.view = DESK;
    a.page_at = a.now;
    a.soc = 76;
    a.battery_level = 0;
    whale_ui_draw(&a);
    save("desk-unsynced");
    a.clock_valid = true;
    a.clock = (whale_local_time_t){
        .year = 2026, .month = 11, .day = 6, .wday = 5, .hour = 14, .minute = 25};
    whale_ui_draw(&a);
    save("desk-day");
    a.clock.hour = 23;
    a.clock.minute = 40;
    a.night = true;
    a.soc = 18;
    a.battery_level = 1;
    whale_ui_draw(&a);
    save("desk-night");
    a.night = false;
    a.view = TASK_DETAIL;
    whale_ui_draw(&a);
    save("desk-back-home");
    a.view = QUESTION;
    a.selected = a.focus = a.page = 0;
    a.pages = 1;
    a.options = a.all_options = 3;
    a.defer_question = false;
    strcpy(a.rows[0].title, "导出笔记");
    strcpy(a.prompt, "你想导出哪种格式？");
    strcpy(a.labels[0], "纯文本");
    strcpy(a.labels[1], "网页");
    strcpy(a.labels[2], "图片");
    whale_ui_draw(&a);
    save("question-list-entry");
    a.view = COMPANION;
    a.worn = 2;
    a.action_at = a.now;
    a.now += 1000;
    for (unsigned action = 1; action <= 4; action++) {
        a.companion_action = action;
        whale_ui_draw(&a);
        char name[40];
        snprintf(name, sizeof name, "witch-interaction-%u", action);
        save(name);
        check_action_caption();
    }
    a.now = a.action_at + 2400;
    whale_ui_draw(&a);
    save("witch-coffee-finished");
    check_action_caption();
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    printf("LVGL renderer PASS; peak pool used %lu / %lu bytes\n", (unsigned long)memory.max_used,
           (unsigned long)memory.total_size);
    return 0;
}
