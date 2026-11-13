#include "lvgl.h"
#include "whale_app.h"
#include "whale_assets.h"
#include "whale_outfits.h"
#include "whale_accessories.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint8_t landscape_pixels[2 * 240 * 228 * 2] __asm__("_binary_room_rgb_start");
uint8_t poses[16 * 196 * 196 * 3] __asm__("_binary_poses_rgb_start");
uint8_t companion[WHALE_COMPANION_BYTES] __asm__("_binary_companion_rgb_start");
uint8_t outfits[WHALE_OUTFIT_BYTES] __asm__("_binary_outfits_rgb_start");
uint8_t accessories[WHALE_ACC_BYTES] __asm__("_binary_accessories_rgb_start");
static uint16_t buffer[240 * 40];
static unsigned flushed_pixels;
static uint16_t screen[240 * 320];
static size_t min_free_observed = ~(size_t)0, min_largest_observed = ~(size_t)0;
static uint8_t max_frag_observed;
static unsigned redraw_checks;

static void record_pool(void) {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    if (m.free_size < min_free_observed)
        min_free_observed = m.free_size;
    if (m.free_biggest_size < min_largest_observed)
        min_largest_observed = m.free_biggest_size;
    if (m.frag_pct > max_frag_observed)
        max_frag_observed = m.frag_pct;
    assert(m.free_size >= 4096 && m.free_biggest_size >= 4096 &&
           m.total_size - m.max_used >= 4096);
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data) {
    const uint16_t *pixel = (const uint16_t *)data;
    for (int y = area->y1; y <= area->y2; y++)
        for (int x = area->x1; x <= area->x2; x++)
            screen[y * 240 + x] = *pixel++;
    flushed_pixels += lv_area_get_width(area) * lv_area_get_height(area);
    lv_display_flush_ready(display);
}
static unsigned draw(whale_app_t *a) {
    flushed_pixels = 0;
    whale_ui_draw(a);
    lv_refr_now(NULL);
    record_pool();
    return flushed_pixels;
}
/* Verify incremental dirty-rect partial redraw produces the exact same 240x320
 * framebuffer as a fresh full-screen invalidation, and a repeat draw flushes 0. */
static void draw_and_verify_redraw(whale_app_t *a) {
    static uint16_t incremental[240 * 320];
    draw(a);
    assert(draw(a) == 0);
    memcpy(incremental, screen, sizeof incremental);
    memset(screen, 0xa5, sizeof screen);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    record_pool();
    assert(memcmp(incremental, screen, sizeof screen) == 0);
    assert(draw(a) == 0);
    redraw_checks++;
}
static uint32_t pose_checksum(void) {
    uint32_t value = 2166136261u;
    for (int y = 64; y < 224; y++)
        for (int x = 22; x < 218; x++)
            value = (value ^ screen[y * 240 + x]) * 16777619u;
    return value;
}
static void summary_stays_inside_card(const whale_app_t *a) {
    int top = a->night ? 196 : 250;
    uint16_t background = screen[(top - 1) * 240];
    for (int y = top; y < top + 58; y++)
        for (int x = 0; x < 4; x++) {
            assert(screen[y * 240 + x] == background);
            assert(screen[y * 240 + 239 - x] == background);
        }
}
/* Check the real visible label, not a duplicate of the caption-selection logic. */
static void desk_caption(const char *expected) {
    lv_obj_t *root = lv_screen_active(), *caption = NULL;
    for (unsigned i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *obj = lv_obj_get_child(root, i);
        if (lv_obj_check_type(obj, &lv_label_class) &&
            !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) &&
            !strcmp(lv_label_get_text(obj), expected)) {
            assert(!caption);
            caption = obj;
        }
    }
    assert(caption && "Expected standby caption is missing or stale");
    lv_point_t size;
    lv_text_get_size(&size, expected, lv_obj_get_style_text_font(caption, 0), 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    assert(size.x <= lv_obj_get_width(caption) && size.y <= lv_obj_get_height(caption));
    assert(lv_obj_get_x(caption) >= 0 &&
           lv_obj_get_x(caption) + lv_obj_get_width(caption) <= 240);
}
static void desk_pose_fits(int top, int bottom) {
    lv_obj_t *root = lv_screen_active();
    unsigned found = 0;
    for (unsigned i = 0; i < lv_obj_get_child_count(root); i++) {
        lv_obj_t *obj = lv_obj_get_child(root, i);
        if (!lv_obj_check_type(obj, &lv_image_class) ||
            lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN))
            continue;
        const lv_image_dsc_t *src = lv_image_get_src(obj);
        if (!src || src->header.w < 100)
            continue;
        found++;
        assert(lv_obj_get_x(obj) >= 0);
        assert(lv_obj_get_x(obj) + (src->header.w * lv_image_get_scale_x(obj) + 255) / 256 <= 240);
        assert(lv_obj_get_y(obj) >= top);
        assert(lv_obj_get_y(obj) + (src->header.h * lv_image_get_scale_y(obj) + 255) / 256 <= bottom);
    }
    assert(found == 1);
}
static void dynamic_question_glyphs(void) {
    LV_FONT_DECLARE(lv_font_cn_12);
    LV_FONT_DECLARE(lv_font_cn_14);
    LV_FONT_DECLARE(lv_font_cn_16);
    LV_FONT_DECLARE(lv_font_cn_18);
    const lv_font_t *fonts[] = {&lv_font_cn_12, &lv_font_cn_14, &lv_font_cn_16, &lv_font_cn_18};
    const uint32_t symbols[] = {0x2190, 0x2192, 0x2194, 0x00d7, 0x00f7,
                               0x00b1, 0x2264, 0x2265, 0x2260, 0x2212};
    for (unsigned f = 0; f < sizeof fonts / sizeof *fonts; f++)
        for (unsigned c = 0; c < sizeof symbols / sizeof *symbols; c++) {
            lv_font_glyph_dsc_t glyph;
            assert(lv_font_get_glyph_dsc(fonts[f], &glyph, symbols[c], 0));
            assert(!glyph.is_placeholder && glyph.adv_w && glyph.box_w && glyph.box_h);
        }
    assert(whale_displayable("暴击率 20% → 25%（暴击伤害 2.5 倍），平均单发伤害提升约多少？"));
    assert(whale_displayable("0.25 × 2.0 + 0.75 × 1.0"));
    assert(whale_displayable("解锁 2 枚新徽章"));
    assert(!whale_displayable("😀")); /* Unsupported text still falls back safely. */
}
static void question_back_button_stays_visible(whale_app_t *a) {
    const char *titles[] = {"选择题测试", "A very long session title that fills the heading"};
    a->view = QUESTION;
    a->page = a->focus = 0;
    a->pages = 2;
    a->options = a->all_options = 3;
    strcpy(a->prompt, "先选一个选项");
    strcpy(a->labels[0], "A");
    strcpy(a->labels[1], "B");
    strcpy(a->labels[2], "C");
    for (unsigned mode = WHALE_MODE_SINGLE; mode <= WHALE_MODE_APPROVAL; mode++) {
        a->question_mode = mode;
        if (mode == WHALE_MODE_APPROVAL)
            a->options = a->all_options = 2;
        for (unsigned title = 0; title < sizeof titles / sizeof *titles; title++) {
            strcpy(a->rows[0].title, titles[title]);
            for (unsigned focused = 0; focused < 2; focused++) {
                a->defer_question = focused;
                draw(a);
                /* Both lower and right portions of the return chip must stay
                 * visibly above the page background, even under a long title. */
                uint16_t paper = screen[60 * 240 + 43];
                assert(screen[55 * 240 + 27] != paper);
                assert(screen[44 * 240 + 43] != paper);
                assert(draw(a) == 0);
            }
        }
    }
    a->defer_question = false;
}
int main(void) {
    FILE *file = fopen(WHALE_POSES_PATH, "rb");
    assert(file && fread(poses, 1, sizeof poses, file) == sizeof poses);
    fclose(file);
    file = fopen(WHALE_COMPANION_PATH, "rb");
    assert(file && fread(companion, 1, sizeof companion, file) == sizeof companion);
    fclose(file);
    file = fopen(WHALE_OUTFITS_PATH, "rb");
    assert(file && fread(outfits, 1, sizeof outfits, file) == sizeof outfits);
    fclose(file);
    file = fopen(WHALE_ACCESSORIES_PATH, "rb");
    assert(file && fread(accessories, 1, sizeof accessories, file) == sizeof accessories);
    fclose(file);
    FILE *landscape = fopen(WHALE_ROOM_PATH, "rb");
    assert(landscape && fread(landscape_pixels, 1, sizeof landscape_pixels, landscape) ==
                            sizeof landscape_pixels);
    fclose(landscape);
    lv_init();
    dynamic_question_glyphs();
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
    const whale_page_t pages[] = {HOME, TASK_DETAIL, LIST, REPLY_READ, WARDROBE, BADGES};
    unsigned errors = 0;
    for (unsigned i = 0; i < sizeof pages / sizeof pages[0]; i++) {
        a.view = pages[i];
        draw(&a);
        unsigned pixels = draw(&a);
        printf("same page %d: %u pixels redrawn (expected 0)\n", a.view, pixels);
        errors += pixels != 0;
    }
    /* Each outfit swaps the home and wardrobe poses; a session page keeps
     * the plain work pose. */
    a.unlocked = 14;
    for (unsigned worn = 2; worn <= 8; worn <<= 1) {
        a.worn = worn;
        a.view = HOME;
        assert(draw(&a) > 0);
        assert(draw(&a) == 0);
        a.view = WARDROBE;
        assert(draw(&a) > 0);
    }
    /* Feelings change the home and list whale in every outfit, and a
     * repeated frame redraws nothing. */
    for (unsigned worn = 0; worn <= 8; worn = worn ? worn << 1 : 2) {
        a.worn = worn;
        a.global_waiting = 2;
        a.view = HOME;
        a.feel = WHALE_FEEL_CALM;
        draw(&a);
        uint32_t calm = pose_checksum(), seen[3];
        for (unsigned feel = WHALE_FEEL_CELEBRATE; feel <= WHALE_FEEL_VERY_ANXIOUS; feel++) {
            a.feel = feel;
            assert(draw(&a) > 0);
            assert(draw(&a) == 0);
            seen[feel - 1] = pose_checksum();
            assert(seen[feel - 1] != calm);
        }
        assert(seen[0] != seen[1]);
        a.view = LIST;
        a.feel = WHALE_FEEL_CALM;
        draw(&a);
        a.feel = WHALE_FEEL_ANXIOUS;
        assert(draw(&a) > 0);
        assert(draw(&a) == 0);
        a.feel = WHALE_FEEL_CALM;
        a.global_waiting = 0;
    }
    a.worn = 0;
    a.unlocked = 0;
    a.view = WARDROBE;
    uint32_t preview[4];
    for (unsigned i = 0; i < 4; i++) {
        a.outfit_choice = i;
        draw(&a);
        preview[i] = pose_checksum();
        for (unsigned j = 0; j < i; j++)
            assert(preview[i] != preview[j]); /* Locked selection previews its actual skin. */
        assert(a.worn == 0); /* Preview never equips it. */
    }
    a.back_focus = true; /* Give every clothing icon the same background. */
    a.unlocked = 14;
    draw(&a);
    uint32_t clothing[4] = {0};
    for (int i = 0; i < 4; i++) {
        int left = 110 + (2 * i - 3) * 55 / 2;
        for (int y = 263; y < 283; y++)
            for (int x = left; x < left + 20; x++)
                clothing[i] = (clothing[i] ^ screen[y * 240 + x]) * 16777619u;
        for (int j = 0; j < i; j++)
            assert(clothing[i] != clothing[j]); /* Four actual wardrobe symbols must differ. */
    }
    a.back_focus = false;
    a.view = TASK_DETAIL;
    draw(&a);
    uint32_t task_pose = pose_checksum();
    a.worn = 8;
    draw(&a);
    assert(pose_checksum() == task_pose); /* Work states always use the default art. */
    a.worn = 0;
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
    for (unsigned fresh = 0; fresh < 2; fresh++) {
        /* Spoken new task reuses the same widgets with its own copy. */
        a.voice_new = fresh;
        a.voice_task_cap = !fresh;
        a.voice_cap = true;
        if (fresh)
            strcpy(a.voice_title, "新任务 · shop");
        for (unsigned phase = 0; phase <= VOICE_NO_TARGET; phase++) {
            a.voice_phase = phase;
            draw(&a);
            assert(draw(&a) == 0);
        }
    }
    a.voice_new = false;
    /* The list's new-task row fits the same pool. */
    a.view = LIST;
    a.list_new = true;
    draw(&a);
    assert(draw(&a) == 0);
    a.list_new = a.voice_cap = a.voice_task_cap = false;
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
    /* All invitation poses/feedback retain the native skin offsets and do not
     * churn LVGL allocations when a still frame is redrawn. */
    static const uint8_t invitation_skins[] = {0, 2, 4, 8};
    a.global_unread = 0;
    for (unsigned skin = 0; skin < sizeof invitation_skins; skin++) {
        a.worn = invitation_skins[skin];
        for (unsigned action = 1; action <= 4; action++) {
            a.view = HOME;
            a.invitation = action;
            a.invitation_variant = action % 3;
            a.invitation_settling = false;
            draw(&a);
            assert(draw(&a) == 0);
            a.invitation_settling = true;
            draw(&a);
            assert(draw(&a) == 0);
            a.invitation_settling = false;
            a.view = COMPANION;
            a.companion_choice = action - 1;
            draw(&a);
            assert(draw(&a) == 0);
            a.invitation = 0;
            a.companion_invited = true;
            a.companion_action = action;
            a.action_at = a.now - 1000;
            draw(&a);
            assert(draw(&a) == 0);
        }
    }
    a.companion_invited = false;
    a.companion_action = 0;
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
    /* Real raster margins catch clipped statistics, including the 16-bit count
     * boundary. Run both single-line and stacked layouts in day/night themes. */
    a.view = DESK;
    a.clock_valid = a.summary_valid = true;
    a.standby_mode = 0;
    static const uint16_t done_cases[] = {0, 1, 2, 5, 5, 100, 65535};
    static const uint32_t time_cases[] = {0, 60, 2400, 600, 7800, 46740, 3599940};
    for (unsigned night = 0; night < 2; night++) {
        a.night = night;
        for (unsigned i = 0; i < sizeof done_cases / sizeof done_cases[0]; i++) {
            a.summary_done = done_cases[i];
            a.summary_seconds = time_cases[i];
            a.clock.hour = i == 0 ? 9 : 14;
            draw(&a);
            if (done_cases[i] || time_cases[i])
                summary_stays_inside_card(&a);
            assert(draw(&a) == 0);
        }
    }
    /* Exercise noon, workload thresholds, stale totals and updates on the same
     * page. A changed caption must replace the previous visible text in full. */
    static const struct {
        uint16_t done;
        uint32_t seconds;
        uint8_t hour;
        bool stale;
        const char *caption;
    } captions[] = {
        {0, 0, 11, false, "新的一天，慢慢来"},
        {0, 0, 12, false, "今天还没开工"},
        {1, 1799, 14, false, "今天开了个好头"},
        {1, 1800, 14, false, "陪你一起慢慢忙"},
        {4, 7199, 14, false, "陪你一起慢慢忙"},
        {5, 7199, 14, false, "今天完成了好多事呀"},
        {5, 7200, 14, false, "忙了好一会儿，歇一下吧"},
        {0, 7200, 14, false, "忙了好一会儿，歇一下吧"},
        {5, 7200, 9, true, "新的一天，慢慢来"},
        {5, 7200, 14, true, "今天还没开工"},
    };
    a.night = false;
    for (unsigned i = 0; i < sizeof captions / sizeof captions[0]; i++) {
        a.summary_done = captions[i].done;
        a.summary_seconds = captions[i].seconds;
        a.summary_stale = captions[i].stale;
        a.clock.hour = captions[i].hour;
        draw(&a);
        desk_caption(captions[i].caption);
        assert(draw(&a) == 0);
    }
    a.summary_stale = false;
    /* Desk display honors every outfit and blink/yawn pose across summary,
     * idle and plain clock; no sprite may overlap the caption or statistics. */
    static const uint32_t desk_frames[] = {1000, 4875, 21000};
    a.page_at = 0;
    for (unsigned mode = 0; mode < 3; mode++) {
        a.standby_mode = mode == 2 ? 1 : 0;
        a.summary_done = mode == 1 ? 3 : 0;
        a.summary_seconds = mode == 1 ? 3600 : 0;
        for (unsigned frame = 0; frame < sizeof desk_frames / sizeof desk_frames[0]; frame++) {
            a.now = desk_frames[frame];
            uint32_t desk_poses[4];
            for (unsigned s = 0; s < sizeof invitation_skins; s++) {
                a.worn = invitation_skins[s];
                draw(&a);
                assert(draw(&a) == 0);
                desk_pose_fits(mode == 2 ? 75 : 86, mode == 1 ? 226 : mode == 2 ? 290 : 254);
                desk_poses[s] = pose_checksum();
                for (unsigned j = 0; j < s; j++)
                    assert(desk_poses[s] != desk_poses[j]);
            }
        }
    }
    a.worn = 0;
    a.standby_mode = 0;
    a.summary_valid = false;
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
    question_back_button_stays_visible(&a);
    /* Achievement badges & 4x4x10 orthogonal cross-skin wearables:
     * 1. Partial redraw == full-screen redraw (0 pixel diff) across skin switches,
     *    accessory switches, unequip, interactions, day/night, and page transitions.
     * 2. Blink (17) and yawn (21) remain distinct from open-eye (16) even with W1/W2.
     * 3. All 8 HOME life[] activities, HOME feelings, COMPANION actions 1..4, and DESK
     *    scales (172/196/256) follow the 4x4x10 wearable occlusion & sleeping/hand rules.
     * 4. Night clock and work pages (TASK_DETAIL/LIST) have 0 stray accessory pixels. */
    a.badges = 0x57;
    a.badge_unread = 0x40;
    a.view = COMPANION;
    a.companion_choice = 5;
    a.companion_action = 0;
    draw_and_verify_redraw(&a);
    a.badge_unread = 0;
    for (unsigned s = 0; s < sizeof invitation_skins; s++) {
        a.worn = invitation_skins[s];
        a.worn_acc = 0;
        a.badge_equipped = 0;
        a.view = BADGES;
        a.now = 1000; /* pose 16 */
        a.back_focus = true;
        draw_and_verify_redraw(&a);
        uint32_t base_pose = pose_checksum();
        a.back_focus = false;
        static const uint8_t wear_slots[4] = {1, 2, 4, 0}; /* wear 1, 2, 3, 4 */
        uint32_t wear_sums[4];
        for (unsigned w = 0; w < 4; w++) {
            a.badge_choice = wear_slots[w];
            a.now = 1000; /* open-eye 16 */
            draw_and_verify_redraw(&a);
            wear_sums[w] = pose_checksum();
            assert(wear_sums[w] != base_pose);
            for (unsigned prev = 0; prev < w; prev++)
                assert(wear_sums[w] != wear_sums[prev]);
            /* Blink (17) must visibly close eyes even through hollow W1 glasses & W2! */
            a.now = 4875; /* blink 17 */
            draw_and_verify_redraw(&a);
            assert(pose_checksum() != wear_sums[w]);
        }
        /* Slide across all 8 badge slots including locked notices & unread arrows. */
        a.now = 1000;
        a.badge_unread = 0x81;
        for (unsigned b = 0; b < WHALE_BADGE_COUNT; b++) {
            a.badge_choice = b;
            a.locked_notice = !(a.badges & (1u << b));
            draw_and_verify_redraw(&a);
        }
        a.locked_notice = false;
        a.badge_unread = 0;
    }
    /* Equipping a wearable stays visible across skin switches in WARDROBE, HOME, COMPANION, DESK. */
    a.unlocked = 14;
    a.badges = 0xff;
    a.badge_equipped = 5;
    a.worn_acc = 3;
    uint32_t wardrobe_acc[4];
    a.view = WARDROBE;
    a.now = 1000;
    for (unsigned i = 0; i < 4; i++) {
        a.outfit_choice = i;
        draw_and_verify_redraw(&a);
        wardrobe_acc[i] = pose_checksum();
        assert(wardrobe_acc[i] != preview[i]);
        for (unsigned j = 0; j < i; j++)
            assert(wardrobe_acc[i] != wardrobe_acc[j]);
    }
    /* Full 4 skins x (0..4 wearables) across HOME life[] (poses 20,19,15,3,18,19,10,1),
     * COMPANION idle/blink/yawn + actions 1..4, and DESK scales 172/196/256 + night. */
    static const int home_life[8] = {20, 19, 15, 3, 18, 19, 10, 1};
    for (unsigned s = 0; s < sizeof invitation_skins; s++) {
        a.worn = invitation_skins[s];
        /* 1. HOME life[] activities 0..7 */
        a.view = HOME;
        a.feel = WHALE_FEEL_CALM;
        a.invitation = 0;
        a.now = 1000;
        for (unsigned act = 0; act < 8; act++) {
            a.activity = act;
            a.night = (act == 2);
            a.worn_acc = 0;
            draw_and_verify_redraw(&a);
            uint32_t plain_act = pose_checksum();
            int pid = home_life[act];
            for (unsigned w = 1; w <= 4; w++) {
                a.worn_acc = w;
                draw_and_verify_redraw(&a);
                uint32_t acc_act = pose_checksum();
                bool expect_visible = true;
                if (w == 1 && (pid == 15 || (pid == 1 && s == 0)))
                    expect_visible = false; /* W1 glasses taken off when resting/sleeping */
                if (expect_visible)
                    assert(acc_act != plain_act);
                else
                    assert(acc_act == plain_act);
            }
            /* Unequip back to 0: partial redraw must cleanly erase accessory! */
            a.worn_acc = 0;
            draw_and_verify_redraw(&a);
            assert(pose_checksum() == plain_act);
        }
        a.night = false;

        /* 2. COMPANION idle (16), blink (17), yawn (21), and interactions 1..4 */
        a.view = COMPANION;
        a.companion_at = 0;
        static const uint32_t comp_times[3] = {1000, 4875, 21000}; /* 16, 17, 21 */
        for (unsigned ti = 0; ti < 3; ti++) {
            a.companion_action = 0;
            a.now = comp_times[ti];
            a.worn_acc = 0;
            draw_and_verify_redraw(&a);
            uint32_t plain_comp = pose_checksum();
            for (unsigned w = 1; w <= 4; w++) {
                a.worn_acc = w;
                draw_and_verify_redraw(&a);
                assert(pose_checksum() != plain_comp);
            }
        }
        /* Interaction round-trip (action 1..4 and back to idle) */
        for (unsigned action = 1; action <= 4; action++) {
            a.companion_action = action;
            a.action_at = 1000;
            a.now = 1500;
            a.worn_acc = 0;
            draw_and_verify_redraw(&a);
            uint32_t plain_act = pose_checksum();
            for (unsigned w = 1; w <= 3; w++) {
                a.worn_acc = w;
                draw_and_verify_redraw(&a);
                assert(pose_checksum() != plain_act);
            }
            a.worn_acc = 4;
            draw_and_verify_redraw(&a);
            assert(pose_checksum() != plain_act); /* hands busy -> plush rests beside character */
            a.companion_action = 0;
            a.now = 1000;
            draw_and_verify_redraw(&a);
        }

        /* 3. DESK scales 172 (summary), 196 (idle summary), 256 (plain clock) + yawn 21 + night */
        a.view = DESK;
        a.clock_valid = true;
        a.page_at = 0;
        for (unsigned mode = 0; mode < 3; mode++) {
            a.night = false;
            a.summary_valid = mode < 2;
            a.standby_mode = mode == 2 ? 1 : 0;
            a.summary_done = mode == 0 ? 3 : 0;
            a.summary_seconds = mode == 0 ? 3600 : 0;
            a.now = 21000; /* yawn 21! */
            a.worn_acc = 0;
            draw_and_verify_redraw(&a);
            uint32_t plain_yawn = pose_checksum();
            a.worn_acc = 2;
            draw_and_verify_redraw(&a);
            assert(pose_checksum() != plain_yawn);
            a.now = 1000; /* pose 16 */
            a.worn_acc = 0;
            draw_and_verify_redraw(&a);
            uint32_t plain_desk = pose_checksum();
            a.worn_acc = 4;
            draw_and_verify_redraw(&a);
            assert(pose_checksum() != plain_desk);
        }
        /* Night DESK has no whale/accessory regardless of worn_acc */
        a.night = true;
        a.worn_acc = 0;
        draw_and_verify_redraw(&a);
        static uint16_t night_plain[240 * 320];
        memcpy(night_plain, screen, sizeof night_plain);
        a.worn_acc = 3;
        draw_and_verify_redraw(&a);
        assert(memcmp(night_plain, screen, sizeof night_plain) == 0);
        a.night = false;
    }
    /* HOME feelings (anxious, single badge, merged "解锁 2 枚新徽章") wear accessories cleanly. */
    a.view = HOME;
    a.worn = 2;
    a.global_waiting = 2;
    a.feel = WHALE_FEEL_ANXIOUS;
    a.worn_acc = 0;
    draw_and_verify_redraw(&a);
    uint32_t anx_plain = pose_checksum();
    a.worn_acc = 1;
    draw_and_verify_redraw(&a);
    assert(pose_checksum() != anx_plain);
    a.feel = WHALE_FEEL_CELEBRATE;
    a.badge_cheer = 7;
    draw_and_verify_redraw(&a);
    a.badge_cheer = (uint8_t)(0x80u | 2u);
    draw_and_verify_redraw(&a);
    a.feel = WHALE_FEEL_CALM;
    a.badge_cheer = 0;
    a.global_waiting = 0;

    /* Transition from HOME/BADGES with wearable to TASK_DETAIL & LIST leaves 0 stray pixels. */
    a.view = TASK_DETAIL;
    a.rows[0].state = 5;
    a.worn = 0;
    a.worn_acc = 0;
    draw_and_verify_redraw(&a);
    static uint16_t td_plain_screen[240 * 320];
    memcpy(td_plain_screen, screen, sizeof td_plain_screen);
    a.worn = 8;
    a.worn_acc = 1;
    assert(draw(&a) == 0);
    assert(memcmp(td_plain_screen, screen, sizeof td_plain_screen) == 0);

    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    printf("pool summary: total=%lu max_used=%lu peak_headroom=%lu min_free=%lu "
           "min_largest=%lu max_frag=%u%% redraw_checks=%u\n",
           (unsigned long)memory.total_size, (unsigned long)memory.max_used,
           (unsigned long)(memory.total_size - memory.max_used),
           (unsigned long)min_free_observed, (unsigned long)min_largest_observed,
           (unsigned)max_frag_observed, redraw_checks);
    assert(min_free_observed >= 4096 && min_largest_observed >= 4096 &&
           memory.total_size - memory.max_used >= 4096 && lv_mem_test() == LV_RESULT_OK);
    printf("render regression: %s, peak pool %lu bytes\n", errors ? "FAIL" : "PASS",
           (unsigned long)memory.max_used);
    return errors ? 1 : 0;
}
