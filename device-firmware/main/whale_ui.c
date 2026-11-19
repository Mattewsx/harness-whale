#include "lvgl.h"
#include "lvgl_private.h"
#include "whale_app.h"
#include "whale_assets.h"
#include "whale_outfits.h"
#include "whale_accessories.h"
#include "room_icons.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Match the tested native renderer. LVGL 9.6 ignores the deprecated KB
 * setting when its byte-sized setting exists, otherwise silently using 64 KiB.
 */
_Static_assert(LV_MEM_SIZE == 32768, "Whale LVGL pool must match the tested 32 KiB budget");

LV_FONT_DECLARE(lv_font_cn_12);
LV_FONT_DECLARE(lv_font_cn_14);
LV_FONT_DECLARE(lv_font_cn_16);
LV_FONT_DECLARE(lv_font_cn_18);
LV_FONT_DECLARE(lv_font_cn_21);
LV_FONT_DECLARE(lv_font_cn_64);
unsigned whale_glyph_advance(unsigned cp);
extern const uint8_t whale_poses[] asm("_binary_poses_rgb_start");
extern const uint8_t companion_poses[] asm("_binary_companion_rgb_start");
/* All skins include every pose used by the room and interactions. */
extern const uint8_t outfit_poses[] asm("_binary_outfits_rgb_start");
extern const uint8_t accessory_patches[] asm("_binary_accessories_rgb_start");
static const uint8_t outfit_pose_ids[] = {1, 3, 10, 15, 16, 17, 18, 19, 20, 21};
enum { OUTFIT_SETS = WHALE_OUTFIT_SETS, OUTFIT_POSES = sizeof outfit_pose_ids, OUTFIT_BASE = 22 };
_Static_assert(OUTFIT_POSES == WHALE_OUTFIT_POSES, "Skin sheet pose order must match the UI");
static lv_image_dsc_t sprites[OUTFIT_BASE + OUTFIT_SETS * OUTFIT_POSES], props[2];
static const lv_image_dsc_t acc_sprites[WHALE_ACC_PATCH_COUNT] =
    WHALE_ACC_SPRITES_INIT(accessory_patches);
static uint8_t acc_skin, acc_wear, acc_pose, next_acc_skin, next_acc_wear, next_acc_pose;
static unsigned acc_scale = 256;
static lv_obj_t *coffee_cup, *coffee_steam, *title_clip, *title_text;
static uint32_t title_started, title_session;
static uint64_t title_epoch;
static char shown_title[513];
static bool title_visible;
enum { LABEL_COUNT = 14 };
static lv_obj_t *labels[LABEL_COUNT], *image;
static char label_bufs[LABEL_COUNT][264];
static lv_obj_t *sea, *sheet, *top_bar, *action_bar, *status_box, *menu_box;
static lv_obj_t **const panels[] = {&sea, &sheet, &top_bar, &action_bar, &status_box, &menu_box};
static unsigned visible_panels;
static unsigned used;
static bool painted_pose;
static lv_obj_t *battery_box, *battery_fill;
static int previous_pose = -1;
#define NAV 0x253853
#define TEAL 0x396f67
#define PAPER 0xfffaf1
#define BAR 0xe1ece3
#define BAR_DUSK 0xbfcfca
#define MUT 0x62738a
#define RED 0x963f46
#define GOLD 0xe9b64b

static uint32_t decode(const char **s) {
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = *p++;
    if (c >= 128) {
        int n = c < 224 ? 1 : c < 240 ? 2 : 3;
        c &= (1 << (6 - n)) - 1;
        while (n--)
            c = (c << 6) | (*p++ & 63);
    }
    *s = (const char *)p;
    return c;
}
bool whale_text_wide(const char *s) {
    int w = 0;
    while (*s) {
        uint32_t cp = decode(&s);
        w += whale_glyph_advance(cp);
        if (w > 206)
            return true;
    }
    return false;
}
bool whale_displayable(const char *s) {
    while (*s) {
        uint32_t cp = decode(&s);
        if (!whale_glyph_advance(cp))
            return false;
    }
    return true;
}
static void text_at_mode(int x, int y, int w, int h, const lv_font_t *font, const char *s, int fg,
                         int bg, lv_text_align_t align, lv_label_long_mode_t mode) {
    if (used >= LABEL_COUNT) {
#ifdef WHALE_ROOM_PATH
        fprintf(stderr, "whale_ui label pool exhausted (%u/%u)\n", used, (unsigned)LABEL_COUNT);
        __builtin_trap();
#endif
        return;
    }
    unsigned idx = used++;
    lv_obj_t *o = labels[idx];
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (lv_label_get_long_mode(o) != mode)
        lv_label_set_long_mode(o, mode);
    if (lv_obj_get_style_text_font(o, 0) != font)
        lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_height(o, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    if (lv_obj_get_style_text_align(o, 0) != align)
        lv_obj_set_style_text_align(o, align, 0);
    if (!lv_color_eq(lv_obj_get_style_text_color(o, 0), lv_color_hex(fg)))
        lv_obj_set_style_text_color(o, lv_color_hex(fg), 0);
    if (bg >= 0 && !lv_color_eq(lv_obj_get_style_bg_color(o, 0), lv_color_hex(bg)))
        lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_opa_t opacity = bg < 0 ? LV_OPA_TRANSP : LV_OPA_COVER;
    if (lv_obj_get_style_bg_opa(o, 0) != opacity)
        lv_obj_set_style_bg_opa(o, opacity, 0);
    if (strcmp(label_bufs[idx], s)) {
        snprintf(label_bufs[idx], sizeof label_bufs[idx], "%s", s);
        lv_label_set_text_static(o, label_bufs[idx]);
    }
}
static void text_at(int x, int y, int w, int h, const lv_font_t *font, const char *s, int fg,
                    int bg, lv_text_align_t align) {
    text_at_mode(x, y, w, h, font, s, fg, bg, align, LV_LABEL_LONG_CLIP);
}
static void paragraph_at(int x, int y, int w, int h, const char *s, int fg, lv_text_align_t align) {
    text_at_mode(x, y, w, h, &lv_font_cn_14, s, fg, -1, align, LV_LABEL_LONG_WRAP);
}
static void label(int x, int y, int w, const char *s, int fg, int bg, lv_text_align_t align) {
    const lv_font_t *font = (y == 3 || y == 292 || y == 30) ? &lv_font_cn_14 : &lv_font_cn_16;
    if (y == 3 && x >= 110)
        font = &lv_font_cn_12;
    if (y == 40)
        font = &lv_font_cn_18;
    text_at(x, y, w, y == 40 ? 26 : 20, font, s, fg, bg, align);
}
static void panel(lv_obj_t *obj, int x, int y, int w, int h, int color, int radius) {
    for (unsigned i = 0; i < sizeof panels / sizeof panels[0]; i++)
        if (obj == *panels[i])
            visible_panels |= 1u << i;
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    if (!lv_color_eq(lv_obj_get_style_bg_color(obj, 0), lv_color_hex(color)))
        lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    if (lv_obj_get_style_bg_opa(obj, 0) != LV_OPA_COVER)
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    if (lv_obj_get_style_radius(obj, 0) != radius)
        lv_obj_set_style_radius(obj, radius, 0);
}
static void center(int y, const char *s, int fg, int bg) {
    label(12, y, 216, s, fg, bg, LV_TEXT_ALIGN_CENTER);
}
static void pose_at(int index, int x, int y, unsigned scale) {
    painted_pose = true;
    lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_pivot(image, 0, 0);
    lv_image_set_scale(image, scale);
    if (index >= OUTFIT_BASE) {
        const whale_outfit_frame_t *frame = &whale_outfit_frames[index - OUTFIT_BASE];
        x += frame->x * scale / 256;
        y += frame->y * scale / 256;
    }
    lv_obj_set_pos(image, x, y);
    if (previous_pose != index) {
        lv_image_set_src(image, sprites + index);
        previous_pose = index;
    }
    uint8_t ns = next_acc_skin, nw = next_acc_wear, np = next_acc_pose;
    next_acc_skin = next_acc_wear = next_acc_pose = 0;
    if (acc_skin != ns || acc_wear != nw || acc_pose != np || (nw && acc_scale != scale)) {
        if (acc_wear)
            lv_obj_invalidate(image);
        acc_skin = ns;
        acc_wear = nw;
        acc_pose = np;
        acc_scale = scale;
        lv_obj_refresh_ext_draw_size(image);
        lv_obj_invalidate(image);
    } else {
        acc_scale = scale;
    }
}
static void pose(int index, int y) { pose_at(index, 64, y, 146); }
/* Each slot is an LVGL image object in the 32 KB pool; 14 slots left under
 * 4 KB free in the render regression, so keep 10 (the wardrobe uses all). */
static lv_obj_t *icon_slots[10];
static lv_image_dsc_t masks[sizeof icon_pixels / sizeof icon_pixels[0]];
static unsigned icon_count;
static bool room_visible, room_night;
static void icon_at(enum room_icon id, int x, int y, int color) {
    if (icon_count >= sizeof icon_slots / sizeof icon_slots[0])
        return;
    lv_obj_t *o = icon_slots[icon_count++];
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(o, x, y);
    if (!lv_color_eq(lv_obj_get_style_image_recolor(o, 0), lv_color_hex(color)))
        lv_obj_set_style_image_recolor(o, lv_color_hex(color), 0);
    if (lv_image_get_src(o) != &masks[id])
        lv_image_set_src(o, &masks[id]);
}
static void parent_icon(enum room_icon id, bool focused) {
    panel(menu_box, 9, 32, 37, 25, focused ? 0xbad9c9 : 0xeaf1e8, 7);
    text_at(11, 35, 10, 18, &lv_font_cn_14, "‹", TEAL, -1, LV_TEXT_ALIGN_CENTER);
    icon_at(id, 22, 34, TEAL);
}
#define CARD 0xf4ede0
static int text_width(const lv_font_t *font, const char *s) {
    int w = 0;
    while (*s) {
        uint32_t cp = decode(&s);
        w += lv_font_get_glyph_width(font, cp, 0);
    }
    return w;
}
/* One line of mixed 21 px numbers and 14 px words on a shared baseline. */
typedef struct {
    const char *text;
    bool big;
    bool strong;
} run_part_t;
static int run_width(const run_part_t *parts, int count) {
    int w = 0;
    for (int i = 0; i < count; i++)
        w += text_width(parts[i].big ? &lv_font_cn_21 : &lv_font_cn_14, parts[i].text) +
             (i ? 2 : 0);
    return w;
}
static void run_at(const run_part_t *parts, int count, int x, int base, int strong, int weak) {
    for (int i = 0; i < count; i++) {
        const lv_font_t *font = parts[i].big ? &lv_font_cn_21 : &lv_font_cn_14;
        int w = text_width(font, parts[i].text);
        int ascent = font->line_height - font->base_line;
        text_at(x, base - ascent, w + 1, font->line_height, font, parts[i].text,
                parts[i].strong ? strong : weak, -1, LV_TEXT_ALIGN_LEFT);
        x += w + 2;
    }
}
/* "已完成 5 个工作 | 已工作 2h 10m" on a rounded card. */
static void summary_card(const whale_app_t *a, int y, int card, int rule, int strong, int weak) {
    char done[6], hours[6], minutes[4];
    unsigned total = a->summary_seconds / 60;
    snprintf(done, sizeof done, "%u", (unsigned)a->summary_done);
    snprintf(hours, sizeof hours, "%u", total / 60 > 999 ? 999 : total / 60);
    snprintf(minutes, sizeof minutes, "%u", total % 60);
    run_part_t left[] = {{"已完成", false, false}, {done, true, true}, {"个工作", false, false}};
    run_part_t long_time[] = {{"已工作", false, false}, {hours, true, true}, {"h", false, true},
                              {minutes, true, true},   {"m", false, true}};
    run_part_t short_time[] = {{"已工作", false, false}, {minutes, true, true}, {"m", false, true}};
    run_part_t *time = total >= 60 ? long_time : short_time; /* under an hour: "已工作 45m" */
    int time_count = total >= 60 ? 5 : 3;
    int lw = run_width(left, 3), rw = run_width(time, time_count);
    int gap = 15;
    if (lw + gap + rw <= 204) {
        int x = 120 - (lw + gap + rw) / 2, base = y + 34;
        panel(sheet, 6, y, 228, 50, card, 14);
        run_at(left, 3, x, base, strong, weak);
        panel(status_box, x + lw + gap / 2, y + 14, 1, 22, rule, 0);
        run_at(time, time_count, x + lw + gap, base, strong, weak);
    } else {
        /* Preserve readable digits and units instead of clipping wide totals.
         * Even the maximum count and duration fit on their own centered row. */
        panel(sheet, 6, y, 228, 58, card, 14);
        run_at(left, 3, 120 - lw / 2, y + 23, strong, weak);
        run_at(time, time_count, 120 - rw / 2, y + 49, strong, weak);
    }
}
static void strip(const enum room_icon *icons, const char *const *names, int count, int selected,
                  int active, unsigned locked, bool home) {
    int step = home ? (count == 4 ? 60 : 66) : count >= 7 ? 32 : count == 6 ? 38 : count == 5 ? 43 : 55;
    panel(action_bar, 0, 252, 240, 68, home && room_night ? BAR_DUSK : BAR, 0);
    for (int i = 0; i < count; ++i) {
        int x = 120 + (2 * i - count + 1) * step / 2;
        if (i == selected)
            panel(status_box, x - 18, 257, 36, 32, home && room_night ? 0x9dbcb2 : 0xb9d5c6, 10);
        icon_at(icons[i], x - 10, 263, i == selected ? NAV : TEAL);
        if (locked & (1u << i))
            icon_at(ICON_lock, x + 8, 246, MUT);
        if (i == active)
            text_at(x + 9, 251, 15, 16, &lv_font_cn_12, "●", TEAL, -1, LV_TEXT_ALIGN_CENTER);
        if (i == selected)
            text_at(home ? x - 30 : 10, 295, home ? 60 : 220, 20, &lv_font_cn_14, names[i], NAV, -1,
                    LV_TEXT_ALIGN_CENTER);
    }
}
static void dressed_acc(uint8_t worn, uint8_t acc, int pose_id, int x, int y, unsigned scale) {
    int set = whale_skin_id(worn);
    int slot = -1;
    for (unsigned i = 0; i < OUTFIT_POSES; i++)
        if (outfit_pose_ids[i] == pose_id) {
            slot = (int)i;
            break;
        }
    if (acc > WHALE_ACC_SETS || slot < 0)
        acc = 0;
    next_acc_skin = acc ? (uint8_t)set : 0;
    next_acc_wear = acc;
    next_acc_pose = acc ? (uint8_t)slot : 0;
    if (set && slot >= 0)
        pose_id = OUTFIT_BASE + (set - 1) * OUTFIT_POSES + slot;
    pose_at(pose_id, x, y, scale);
}
static const char *desk_line(const whale_app_t *a, bool idle_day) {
    if (idle_day)
        return a->clock_valid && a->clock.hour < 12 ? "新的一天，慢慢来" : "今天还没开工";
    if (a->summary_seconds >= 7200)
        return "忙了好一会儿，歇一下吧";
    if (a->summary_done >= 5)
        return "今天完成了好多事呀";
    if (a->summary_done <= 1 && a->summary_seconds < 1800)
        return "今天开了个好头";
    return "陪你一起慢慢忙";
}
static const char *invitation_line(const whale_app_t *a) {
    static const char *const offers[][3] = {
        {"嗨，看看我呀。", "歇一下，打个招呼？", "想跟你挥挥手。"},
        {"可以摸摸头吗？", "脑袋准备好啦。", "再摸摸我，好不好？"},
        {"想分我一条小鱼吗？", "小鱼时间到了吗？", "今天也想吃小鱼。"},
        {"一起喝杯咖啡？", "碰个杯，再慢慢忙？", "要陪我喝咖啡吗？"}};
    static const char *const endings[] = {"我继续陪着你。", "我在这里歇会儿。",
                                          "小鱼先收好。", "我先喝一口啦。"};
    if (!a->invitation || a->invitation > 4)
        return NULL;
    return a->invitation_settling ? endings[a->invitation - 1]
                                  : offers[a->invitation - 1][a->invitation_variant % 3];
}
static void show_coffee(int x, int y, unsigned scale, unsigned elapsed, bool animate) {
    lv_obj_remove_flag(coffee_cup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(coffee_steam, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_pivot(coffee_cup, 0, 0);
    lv_image_set_pivot(coffee_steam, 0, 0);
    lv_image_set_scale(coffee_cup, scale);
    lv_image_set_scale(coffee_steam, scale);
    lv_obj_set_pos(coffee_cup, x + 78 * scale / 256, y + 104 * scale / 256);
    lv_obj_set_pos(coffee_steam, x + 121 * scale / 256,
                   y + (72 - (animate ? (elapsed / 250) % 4 : 0)) * scale / 256);
}
/* Static RGB565 landscapes in flash. LVGL clips them to the invalidated
 * rectangle; the display never allocates a full scene buffer in RAM. */
extern const uint8_t room_scenes[] asm("_binary_room_rgb_start");
enum { ROOM_SCENE_BYTES = 240 * 228 * 2 };
static lv_image_dsc_t landscapes[2], sky_rows[2];
static void draw_landscape(lv_event_t *event) {
    if (!room_visible)
        return;
    lv_draw_image_dsc_t draw;
    lv_draw_image_dsc_init(&draw);
    draw.src = &sky_rows[room_night];
    draw.tile = true;
    lv_area_t sky = {0, 0, 239, 23};
    lv_draw_image(lv_event_get_layer(event), &draw, &sky);
    draw.src = &landscapes[room_night];
    draw.tile = false;
    lv_area_t bounds = {0, 24, 239, 251};
    lv_draw_image(lv_event_get_layer(event), &draw, &bounds);
}
static void accessory_event(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        if (acc_wear)
            /* The floor plush can extend left of a tightly cropped skin. */
            lv_event_set_ext_draw_size(event, acc_wear == 4 ? 24 : 12);
        return;
    }
    if (code != LV_EVENT_DRAW_MAIN_END || !acc_wear || acc_wear > WHALE_ACC_SETS ||
        acc_skin >= WHALE_ACC_SKINS || acc_pose >= WHALE_ACC_POSES || previous_pose < 0)
        return;
    int base_x = 0, base_y = 0;
    if (previous_pose >= OUTFIT_BASE) {
        const whale_outfit_frame_t *frame = &whale_outfit_frames[previous_pose - OUTFIT_BASE];
        base_x = frame->x;
        base_y = frame->y;
    }
    lv_area_t img_coords;
    lv_obj_get_coords(image, &img_coords);
    const whale_acc_entry_t *entry = &whale_acc_map[acc_skin][acc_wear - 1][acc_pose];
    lv_layer_t *layer = lv_event_get_layer(event);
    for (unsigned i = 0; i < entry->count; i++) {
        const whale_acc_draw_t *d = &whale_acc_draws[entry->first + i];
        const whale_acc_patch_t *p = &whale_acc_patches[d->patch];
        int rel_x = (int)d->x - base_x;
        int rel_y = (int)d->y - base_y;
        lv_draw_image_dsc_t draw;
        lv_draw_image_dsc_init(&draw);
        draw.src = &acc_sprites[d->patch];
        draw.scale_x = acc_scale;
        draw.scale_y = acc_scale;
        draw.pivot.x = -rel_x;
        draw.pivot.y = -rel_y;
        lv_area_t coords = {img_coords.x1 + rel_x, img_coords.y1 + rel_y,
                            img_coords.x1 + rel_x + (int)p->width - 1,
                            img_coords.y1 + rel_y + (int)p->height - 1};
        lv_draw_image(layer, &draw, &coords);
    }
}
/* Zero-heap LVGL draw unit for DROM-backed I8 (256-entry BGRA palette + 1B/px)
 * outfit sprites. Renders directly into RGB565 layers at native 1:1 or smooth
 * premultiplied bilinear scale without touching LVGL's 32 KiB pool. */
static inline uint16_t blend_rgb565_rgba(uint16_t dst, uint32_t r, uint32_t g, uint32_t b,
                                         uint32_t a) {
    if (a >= 255u)
        return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    uint32_t dr = ((dst >> 11) & 31u) * 255u / 31u;
    uint32_t dg = ((dst >> 5) & 63u) * 255u / 63u;
    uint32_t db = (dst & 31u) * 255u / 31u;
    uint32_t inv = 255u - a;
    uint32_t or_ = (r * a + dr * inv + 127u) / 255u;
    uint32_t og = (g * a + dg * inv + 127u) / 255u;
    uint32_t ob = (b * a + db * inv + 127u) / 255u;
    return (uint16_t)(((or_ >> 3) << 11) | ((og >> 2) << 5) | (ob >> 3));
}
static inline void i8_sample(const uint8_t *pal, const uint8_t *pix, int32_t w, int32_t h,
                             int32_t sx, int32_t sy, uint32_t *r, uint32_t *g, uint32_t *b,
                             uint32_t *a) {
    if (sx < 0 || sy < 0 || sx >= w || sy >= h) {
        *r = *g = *b = *a = 0;
        return;
    }
    const uint8_t *c = pal + (uint32_t)pix[sy * w + sx] * 4u;
    *b = c[0];
    *g = c[1];
    *r = c[2];
    *a = c[3];
}
static void whale_i8_draw_task(lv_draw_task_t *t, lv_layer_t *layer) {
    const lv_draw_image_dsc_t *dsc = (const lv_draw_image_dsc_t *)t->draw_dsc;
    const lv_image_dsc_t *img = (const lv_image_dsc_t *)dsc->src;
    if (!img || !img->data || !layer->draw_buf || layer->color_format != LV_COLOR_FORMAT_RGB565)
        return;
    lv_area_t out;
    if (!lv_area_intersect(&out, &t->_real_area, &t->clip_area))
        return;
    if (!lv_area_intersect(&out, &out, &layer->buf_area))
        return;
    const uint8_t *pal = img->data;
    const uint8_t *pix = img->data + WHALE_OUTFIT_PALETTE_BYTES;
    int32_t sw = (int32_t)img->header.w;
    int32_t sh = (int32_t)img->header.h;
    int32_t sx_scale = dsc->scale_x ? dsc->scale_x : 256;
    int32_t sy_scale = dsc->scale_y ? dsc->scale_y : 256;
    int32_t ox = t->area.x1;
    int32_t oy = t->area.y1;
    int32_t bx = layer->buf_area.x1;
    int32_t by = layer->buf_area.y1;
    int32_t dst_stride = (int32_t)(layer->draw_buf->header.stride / 2u);
    uint16_t *dst_base = (uint16_t *)layer->draw_buf->data;
    if (sx_scale == 256 && sy_scale == 256) {
        for (int32_t y = out.y1; y <= out.y2; y++) {
            int32_t sy = y - oy;
            if (sy < 0 || sy >= sh)
                continue;
            uint16_t *row = dst_base + (y - by) * dst_stride;
            const uint8_t *srow = pix + sy * sw;
            for (int32_t x = out.x1; x <= out.x2; x++) {
                int32_t sx = x - ox;
                if (sx < 0 || sx >= sw)
                    continue;
                const uint8_t *c = pal + (uint32_t)srow[sx] * 4u;
                uint32_t a = c[3];
                if (!a)
                    continue;
                uint16_t *dp = &row[x - bx];
                *dp = blend_rgb565_rgba(*dp, c[2], c[1], c[0], a);
            }
        }
        return;
    }
    int32_t piv_x = dsc->pivot.x, piv_y = dsc->pivot.y;
    for (int32_t y = out.y1; y <= out.y2; y++) {
        int32_t v_fp = ((y - oy - piv_y) * 256 + 128) * 256 / sy_scale + piv_y * 256 - 128;
        int32_t y0 = v_fp >= 0 ? (v_fp >> 8) : -((-v_fp + 255) >> 8);
        uint32_t fy = (uint32_t)(v_fp - y0 * 256);
        if (y0 < -1 || y0 >= sh)
            continue;
        uint16_t *row = dst_base + (y - by) * dst_stride;
        for (int32_t x = out.x1; x <= out.x2; x++) {
            int32_t u_fp = ((x - ox - piv_x) * 256 + 128) * 256 / sx_scale + piv_x * 256 - 128;
            int32_t x0 = u_fp >= 0 ? (u_fp >> 8) : -((-u_fp + 255) >> 8);
            uint32_t fx = (uint32_t)(u_fp - x0 * 256);
            if (x0 < -1 || x0 >= sw)
                continue;
            uint32_t r00, g00, b00, a00, r10, g10, b10, a10;
            uint32_t r01, g01, b01, a01, r11, g11, b11, a11;
            i8_sample(pal, pix, sw, sh, x0, y0, &r00, &g00, &b00, &a00);
            i8_sample(pal, pix, sw, sh, x0 + 1, y0, &r10, &g10, &b10, &a10);
            i8_sample(pal, pix, sw, sh, x0, y0 + 1, &r01, &g01, &b01, &a01);
            i8_sample(pal, pix, sw, sh, x0 + 1, y0 + 1, &r11, &g11, &b11, &a11);
            uint32_t w00 = ((256u - fx) * (256u - fy)) >> 8;
            uint32_t w10 = (fx * (256u - fy)) >> 8;
            uint32_t w01 = ((256u - fx) * fy) >> 8;
            uint32_t w11 = 256u - w00 - w10 - w01;
            uint32_t a_sum = w00 * a00 + w10 * a10 + w01 * a01 + w11 * a11;
            if (a_sum < 256u)
                continue;
            uint32_t r_pm = w00 * r00 * a00 + w10 * r10 * a10 + w01 * r01 * a01 + w11 * r11 * a11;
            uint32_t g_pm = w00 * g00 * a00 + w10 * g10 * a10 + w01 * g01 * a01 + w11 * g11 * a11;
            uint32_t b_pm = w00 * b00 * a00 + w10 * b10 * a10 + w01 * b01 * a01 + w11 * b11 * a11;
            uint16_t *dp = &row[x - bx];
            if (a_sum >= 65280u) {
                uint32_t or_ = (r_pm + 32640u) / 65280u;
                uint32_t og = (g_pm + 32640u) / 65280u;
                uint32_t ob = (b_pm + 32640u) / 65280u;
                *dp = (uint16_t)(((or_ >> 3) << 11) | ((og >> 2) << 5) | (ob >> 3));
            } else {
                uint16_t dst = *dp;
                uint32_t dr = ((dst >> 11) & 31u) * 255u / 31u;
                uint32_t dg = ((dst >> 5) & 63u) * 255u / 63u;
                uint32_t db = (dst & 31u) * 255u / 31u;
                uint32_t inv = 65280u - a_sum;
                uint32_t or_ = (r_pm + dr * inv + 32640u) / 65280u;
                uint32_t og = (g_pm + dg * inv + 32640u) / 65280u;
                uint32_t ob = (b_pm + db * inv + 32640u) / 65280u;
                *dp = (uint16_t)(((or_ >> 3) << 11) | ((og >> 2) << 5) | (ob >> 3));
            }
        }
    }
}
static int32_t whale_i8_evaluate(lv_draw_unit_t *u, lv_draw_task_t *t) {
    if (t->type != LV_DRAW_TASK_TYPE_IMAGE)
        return 0;
    const lv_draw_image_dsc_t *dsc = (const lv_draw_image_dsc_t *)t->draw_dsc;
    if (dsc->header.cf != LV_COLOR_FORMAT_I8 || dsc->rotation != 0 || dsc->tile ||
        dsc->skew_x != 0 || dsc->skew_y != 0 || dsc->clip_radius != 0 ||
        dsc->bitmap_mask_src != NULL || dsc->opa < LV_OPA_MAX ||
        lv_image_src_get_type(dsc->src) != LV_IMAGE_SRC_VARIABLE)
        return 0;
    if (t->preference_score > 50) {
        t->preference_score = 50;
        t->preferred_draw_unit_id = (uint8_t)u->idx;
    }
    return 0;
}
static int32_t whale_i8_dispatch(lv_draw_unit_t *u, lv_layer_t *layer) {
    lv_draw_task_t *t = lv_draw_get_next_available_task(layer, NULL, (uint8_t)u->idx);
    if (!t)
        return LV_DRAW_UNIT_IDLE;
    if (!lv_draw_layer_alloc_buf(layer))
        return LV_DRAW_UNIT_IDLE;
    t->state = LV_DRAW_TASK_STATE_IN_PROGRESS;
    whale_i8_draw_task(t, layer);
    t->state = LV_DRAW_TASK_STATE_FINISHED;
    lv_draw_dispatch_request();
    return 1;
}
static void whale_i8_register(void) {
    lv_draw_unit_t *u = LV_GLOBAL_DEFAULT()->draw_info.unit_head;
    while (u) {
        if (u->name && strcmp(u->name, "WHALE_I8") == 0)
            return;
        u = u->next;
    }
    lv_draw_unit_t *unit = lv_draw_create_unit(sizeof(lv_draw_unit_t));
    if (unit) {
        unit->name = "WHALE_I8";
        unit->evaluate_cb = whale_i8_evaluate;
        unit->dispatch_cb = whale_i8_dispatch;
    }
}
void whale_ui_init(void) {
    whale_i8_register();
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(PAPER), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i = 0; i < sizeof panels / sizeof panels[0]; i++) {
        *panels[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(*panels[i]);
        lv_obj_remove_flag(*panels[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    for (unsigned i = 0; i < 2; i++) {
        landscapes[i] = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                                                    .cf = LV_COLOR_FORMAT_RGB565,
                                                    .w = 240,
                                                    .h = 228,
                                                    .stride = 480},
                                         .data_size = ROOM_SCENE_BYTES,
                                         .data = room_scenes + i * ROOM_SCENE_BYTES};
        /* Extend the actual skyline into the status bar, one flash-backed row per scene. */
        sky_rows[i] = landscapes[i];
        sky_rows[i].header.h = 1;
        sky_rows[i].data_size = sky_rows[i].header.stride;
    }
    lv_obj_add_event_cb(sea, draw_landscape, LV_EVENT_DRAW_MAIN_END, NULL);
    for (int i = 0; i < 16; i++)
        sprites[i] = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                                                 .cf = LV_COLOR_FORMAT_RGB565A8,
                                                 .w = 196,
                                                 .h = 196,
                                                 .stride = 392},
                                      .data_size = 115248,
                                      .data = whale_poses + i * 115248};
    for (unsigned i = 0; i < OUTFIT_SETS * OUTFIT_POSES; i++) {
        const whale_outfit_frame_t *frame = &whale_outfit_frames[i];
        sprites[OUTFIT_BASE + i] = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC,
                       .cf = LV_COLOR_FORMAT_I8,
                       .w = frame->width,
                       .h = frame->height,
                       .stride = frame->width},
            .data_size = WHALE_OUTFIT_PALETTE_BYTES + (uint32_t)frame->width * frame->height,
            .data = outfit_poses + frame->offset};
    }
    for (unsigned i = 0; i < 8; i++) {
        const whale_image_asset_t *v = &whale_image_assets[i];
        lv_image_dsc_t d = {.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                                       .cf = LV_COLOR_FORMAT_RGB565A8,
                                       .w = v->width,
                                       .h = v->height,
                                       .stride = v->width * 2},
                            .data_size = v->width * v->height * 3,
                            .data = companion_poses + v->offset};
        if (i < 6)
            sprites[16 + i] = d;
        else
            props[i - 6] = d;
    }
    image = lv_image_create(screen);
    lv_obj_add_event_cb(image, accessory_event, LV_EVENT_ALL, NULL);
    coffee_cup = lv_image_create(screen);
    lv_image_set_src(coffee_cup, &props[0]);
    coffee_steam = lv_image_create(screen);
    lv_image_set_src(coffee_steam, &props[1]);
    for (unsigned i = 0; i < sizeof masks / sizeof masks[0]; ++i)
        masks[i] = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                                               .cf = LV_COLOR_FORMAT_A8,
                                               .w = 20,
                                               .h = 20,
                                               .stride = 20},
                                    .data_size = 400,
                                    .data = icon_pixels[i]};
    for (unsigned i = 0; i < sizeof icon_slots / sizeof icon_slots[0]; ++i) {
        icon_slots[i] = lv_image_create(screen);
        lv_obj_set_style_image_recolor_opa(icon_slots[i], LV_OPA_COVER, 0);
    }
    title_clip = lv_obj_create(screen);
    lv_obj_remove_style_all(title_clip);
    lv_obj_remove_flag(title_clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(title_clip, 16, 35);
    lv_obj_set_size(title_clip, 208, 24);
    title_text = lv_label_create(title_clip);
    lv_obj_remove_style_all(title_text);
    lv_obj_set_style_text_font(title_text, &lv_font_cn_16, 0);
    lv_obj_set_style_text_color(title_text, lv_color_hex(NAV), 0);
    lv_label_set_long_mode(title_text, LV_LABEL_LONG_CLIP);
    lv_label_set_text_static(title_text, shown_title);

    for (int i = 0; i < LABEL_COUNT; i++) {
        labels[i] = lv_label_create(screen);
        lv_label_set_long_mode(labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_height(labels[i], 20);
        lv_obj_set_style_text_font(labels[i], &lv_font_cn_16, 0);
        lv_obj_set_style_bg_opa(labels[i], LV_OPA_COVER, 0);
        label_bufs[i][0] = 0;
        lv_label_set_text_static(labels[i], label_bufs[i]);
    }
    battery_box = lv_obj_create(screen);
    lv_obj_remove_style_all(battery_box);
    lv_obj_set_size(battery_box, 17, 9);
    lv_obj_set_style_border_width(battery_box, 1, 0);
    battery_fill = lv_obj_create(battery_box);
    lv_obj_remove_style_all(battery_fill);
    lv_obj_set_pos(battery_fill, 1, 1);
    lv_obj_set_style_bg_opa(battery_fill, LV_OPA_COVER, 0);
}
static void home_title(const whale_app_t *a, const char *text) {
    if (!title_visible || title_session != a->rows[a->selected].session ||
        title_epoch != a->epoch || strcmp(shown_title, text)) {
        snprintf(shown_title, sizeof shown_title, "%s", text);
        title_started = a->now;
        title_session = a->rows[a->selected].session;
        title_epoch = a->epoch;
        lv_label_set_text_static(title_text, shown_title);
    }
    title_visible = true;
    lv_obj_remove_flag(title_clip, LV_OBJ_FLAG_HIDDEN);
    unsigned width = 0;
    const char *p = text;
    while (*p)
        width += lv_font_get_glyph_width(&lv_font_cn_16, decode(&p), 0);
    lv_obj_set_width(title_text, width + 1);
    unsigned distance = width > 208 ? width - 208 : 0, offset = 0;
    if (distance) {
        unsigned travel = (distance * 1000 + 21) / 22;
        unsigned phase = (a->now - title_started) % (3000 + travel);
        if (phase > 1500) {
            offset = (phase - 1500) * 22 / 1000;
            if (offset > distance)
                offset = distance;
        }
    }
    lv_obj_set_pos(title_text, -(int)offset, 0);
}
static const char *next_line(const char *s, char *line) {
    int bytes = 0, px = 0;
    while (*s) {
        if (*s == '\n') {
            line[bytes] = 0;
            return s + 1;
        }
        const char *next = s;
        uint32_t cp = decode(&next);
        int w = whale_glyph_advance(cp);
        if (bytes && (px + w > 208 || bytes + (next - s) >= 79))
            break;
        memcpy(line + bytes, s, next - s);
        bytes += next - s;
        px += w;
        s = next;
    }
    line[bytes] = 0;
    return s;
}
static const char *title_line(const char *s, char *line, size_t capacity, const lv_font_t *font,
                              unsigned max_width) {
    unsigned bytes = 0, width = 0;
    while (*s) {
        const char *next = s;
        uint32_t cp = decode(&next);
        unsigned advance = lv_font_get_glyph_width(font, cp, 0);
        if (bytes + (size_t)(next - s) + sizeof("…") > capacity || (bytes && width + advance > max_width))
            break;
        memcpy(line + bytes, s, next - s);
        bytes += next - s;
        width += advance;
        s = next;
    }
    line[bytes] = 0;
    return s;
}
static int scroll_max(const char *s, int visible) {
    char line[80];
    int n = 0;
    while (*s) {
        s = next_line(s, line);
        n++;
    }
    return n > visible ? n - visible : 0;
}
int whale_scroll_max(const char *s) { return scroll_max(s, 8); }
int whale_home_scroll_max(const char *s) { return scroll_max(s, HOME_REPLY_LINES); }
size_t whale_line_offset(const char *s, int line) {
    const char *start = s;
    char row[80];
    while (line-- > 0 && *s)
        s = next_line(s, row);
    return s - start;
}
int whale_scroll_at(const char *s, size_t offset) {
    const char *start = s;
    char row[80];
    int line = 0;
    while (*s) {
        const char *next = next_line(s, row);
        if ((size_t)(next - start) > offset)
            break;
        s = next;
        line++;
    }
    return line;
}
static void fulltext(const char *heading, const char *body, int offset) {
    char row[80];
    center(46, heading, PAPER, TEAL);
    while (offset-- > 0 && *body)
        body = next_line(body, row);
    for (int j = 0; j < 8 && *body; j++) {
        body = next_line(body, row);
        label(16, 80 + 24 * j, 208, row, NAV, PAPER, LV_TEXT_ALIGN_LEFT);
    }
}
static const char *states[] = {"离线缓存", "待机",     "思考中", "执行工具", "等待中",
                               "已完成",   "本轮失败", "已停止", "等待选择"};
static const char *tools[] = {"执行工具", "运行命令", "读取文件", "编辑文件",
                              "搜索中",   "浏览网页", "委派任务", "执行工具"};
static const char *hint_text[] = {
    "", "工具出错·继续", "请在电脑审批", "请在电脑多选", "请在电脑输入", "请在电脑处理"};
static const int state_pose[] = {15, 0, 2, 9, 10, 12, 13, 14, 11};
static const int tool_pose[] = {9, 3, 4, 5, 6, 7, 8, 9};
static int task_pose(const whale_row_t *r) {
    if (r->hint)
        return r->hint == 1 ? 9 : 10;
    return r->state == 3 ? tool_pose[r->tool] : state_pose[r->state];
}
static void list_state_icon(const whale_row_t *row, int y) {
    /* Row states use the same wire order as the detail screen. */
    static const struct {
        enum room_icon icon;
        int color;
    } state[] = {{ICON_offline, 0x737886}, {ICON_idle, 0x627587},    {ICON_thinking, 0x596c99},
                 {ICON_executing, 0x37746f}, {ICON_waiting, 0x876b37}, {ICON_completed, 0x39745d},
                 {ICON_failed, 0xa2474d},  {ICON_stopped, 0x697785}, {ICON_question, 0x975315}};
    unsigned index = whale_row_needs_answer(row) ? 8 : row->state;
    icon_at(state[index].icon, 11, y + 7, state[index].color);
    if (row->flags & (8 | 16 | 32 | 64))
        icon_at(ICON_unread, 16, y + 3, TEAL);
}
static const char *const badge_names[WHALE_BADGE_COUNT] = {
    "初航破浪", "果断拍板", "传声海螺", "日理万机",
    "深海潜航", "挑灯夜航", "摸鱼大师", "心意相通"};
static const char *const wear_names[WHALE_ACC_SETS + 1] = {
    "", "金丝眼镜", "珍珠耳麦", "星光提灯", "小鲸玩偶"};
static const enum room_icon badge_icons[WHALE_BADGE_COUNT] = {
    ICON_whale, ICON_glasses,   ICON_mic,  ICON_completed,
    ICON_clock, ICON_executing, ICON_fish, ICON_flower};
void whale_ui_draw(const whale_app_t *a) {
    char text[160], value[64];
    used = icon_count = 0;
    whale_row_t empty = {.state = a->linked ? 1 : 0};
    const whale_row_t *r = a->count ? &a->rows[a->selected] : &empty;
    bool animate = a->battery_level != 2;
    bool painted_coffee = false;
    if (a->view != TASK_DETAIL) {
        title_visible = false;
        lv_obj_add_flag(title_clip, LV_OBJ_FLAG_HIDDEN);
    }
    bool inline_reply = a->view == REPLY_READ || a->view == REPLY_LOAD || a->view == REPLY_FAILED;
    painted_pose = false;
    visible_panels = 0;
    bool desk = a->view == DESK;
    if (!desk) {
        bool dark_sky = a->view == HOME && a->night;
        if (a->view != HOME)
            panel(top_bar, 0, 0, 240, 24, BAR, 0);
        icon_at(a->ready ? ICON_bt : ICON_btOff, 10, 3,
                dark_sky ? (a->ready ? 0xc7f4e5 : 0xd1d9e0) : (a->ready ? TEAL : MUT));
        if (a->view == HOME || a->view == COMPANION)
            icon_at(ICON_bond0 + (a->bond * 10 + 49) / 99, 110, 3, dark_sky ? 0xffc5d2 : 0xb75f70);
        if (a->soc < 0)
            snprintf(value, sizeof value, "--%%");
        else
            snprintf(value, sizeof value, "%s%d%%", a->soc_stale ? "~" : "", a->soc);
        enum { battery_right = 240 - 10, icon_width = 17, icon_gap = 4 };
        lv_point_t text_size;
        lv_text_get_size(&text_size, value, &lv_font_cn_12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int value_x = battery_right - text_size.x;
        const int level_color = a->battery_level == 0   ? (dark_sky ? 0xffffff : NAV)
                                : a->battery_level == 1 ? (dark_sky ? 0xffdf9b : 0x815719)
                                                        : (dark_sky ? 0xffc1b8 : RED);
        text_at(value_x, 6, text_size.x, 18, &lv_font_cn_12, value, level_color, -1, LV_TEXT_ALIGN_RIGHT);
        lv_color_t outline = lv_color_hex(dark_sky ? 0xe7f1f2 : MUT);
        if (!lv_color_eq(lv_obj_get_style_border_color(battery_box, 0), outline))
            lv_obj_set_style_border_color(battery_box, outline, 0);
        lv_obj_set_pos(battery_box, value_x - icon_gap - icon_width, 8);
        lv_obj_set_size(battery_fill, a->soc > 0 ? (a->soc * 13 + 99) / 100 : 0, 5);
        lv_color_t battery_color = lv_color_hex(a->battery_level ? GOLD : 0xa8ddd3);
        if (!lv_color_eq(lv_obj_get_style_bg_color(battery_fill, 0), battery_color))
            lv_obj_set_style_bg_color(battery_fill, battery_color, 0);
    }
    /* The desk display has no status bar: date and battery sit at the bottom. */
    if (battery_box && desk != lv_obj_has_flag(battery_box, LV_OBJ_FLAG_HIDDEN)) {
        if (desk)
            lv_obj_add_flag(battery_box, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(battery_box, LV_OBJ_FLAG_HIDDEN);
    }
    const char *foot = "长按确认返回";
    bool was_room = room_visible;
    room_visible = a->view == HOME;
    if (room_visible) {
        panel(sea, 0, 0, 240, 252, 0xe2ecde, 0);
        if (!was_room || room_night != a->night)
            lv_obj_invalidate(sea);
        room_night = a->night;
        static const int life[] = {20, 19, 15, 3, 18, 19, 10, 1};
        int y = 45 + (animate && (a->now / 900) % 2);
        const char *offer = invitation_line(a);
        bool inviting = offer && !a->invitation_settling;
        if (offer) {
            static const int poses[] = {18, 16, 20, 20};
            int n = a->invitation_settling ? (a->invitation == 4 ? 20 : 19)
                                           : poses[a->invitation - 1];
            if (n == 16 && animate && a->now % 5000 > 4750)
                n = 17;
            int pose_y = 43 + (animate && (a->now / 900) % 2);
            dressed_acc(a->worn, a->worn_acc, n, 29, pose_y, 236);
            if (a->invitation == 4) {
                painted_coffee = true;
                show_coffee(29, pose_y, 236, a->now - a->invitation_at, animate);
            }
            panel(sheet, 14, 225, 212, 24, PAPER, 10);
            text_at(20, 228, 200, 20, &lv_font_cn_14, offer, NAV, -1, LV_TEXT_ALIGN_CENTER);
        } else if (a->feel) {
            /* Placeholder art: existing poses plus motion until the anxious
             * and celebrating frames are drawn. */
            bool cheer = a->feel == WHALE_FEEL_CELEBRATE;
            bool very = a->feel == WHALE_FEEL_VERY_ANXIOUS;
            int n = cheer ? ((a->now / 500) % 2 ? 18 : 19) : 10;
            int dx = 0, dy = 0;
            if (animate && cheer)
                dy = (a->now / 250) % 2 ? -5 : 0;
            else if (animate)
                dx = (a->now / (very ? 125 : 250)) % 2 ? 2 : -2;
            dressed_acc(a->worn, a->worn_acc, n, 29 + dx, 43 + dy, 236);
            if (cheer && (a->badge_cheer & 0x80u))
                snprintf(value, sizeof value, "解锁 %u 枚新徽章",
                         (unsigned)(a->badge_cheer & 0x7fu));
            else if (cheer && a->badge_cheer >= 1 && a->badge_cheer <= WHALE_BADGE_COUNT)
                snprintf(value, sizeof value, "解锁徽章 · %s", badge_names[a->badge_cheer - 1]);
            else if (cheer)
                snprintf(value, sizeof value, "任务完成啦！");
            else if (very)
                snprintf(value, sizeof value, "还在等你，来看看好不好？");
            else
                snprintf(value, sizeof value, "有 %u 个问题在等你", (unsigned)a->global_waiting);
            panel(sheet, 14, 225, 212, 24, cheer ? PAPER : 0xf6e3c0, 10);
            text_at(20, 228, 200, 20, &lv_font_cn_14, value, NAV, -1, LV_TEXT_ALIGN_CENTER);
        } else if (a->pomo_alert || a->pomo_state) {
            int n = a->pomo_state == WHALE_POMO_FOCUS && !a->pomo_paused ? 3
                    : a->pomo_state == WHALE_POMO_BREAK                  ? 20
                                                                         : life[a->activity % 8];
            dressed_acc(a->worn, a->worn_acc, n, 29, 43, 236);
            if (a->pomo_alert)
                snprintf(value, sizeof value,
                         a->pomo_alert == 1 ? "专注完成，歇会儿吧" : "休息结束，继续加油");
            else {
                uint32_t s = (a->pomo_rem_ms + 999u) / 1000u;
                snprintf(value, sizeof value, "%s %02lu:%02lu",
                         a->pomo_paused                       ? "已暂停"
                         : a->pomo_state == WHALE_POMO_BREAK ? "休息"
                                                             : "专注",
                         (unsigned long)(s / 60u), (unsigned long)(s % 60u));
            }
            panel(sheet, 14, 225, 212, 24, PAPER, 10);
            text_at(20, 228, 200, 20, &lv_font_cn_14, value, NAV, -1, LV_TEXT_ALIGN_CENTER);
        } else
            dressed_acc(a->worn, a->worn_acc, life[a->activity % 8], 22, y, 256);
        if (!offer && !a->feel && a->activity == 1)
            icon_at(ICON_cola, 148, y + 108, 0xab473d);
        static const enum room_icon icons[] = {ICON_heart, ICON_clock, ICON_chat, ICON_settings};
        const char *const names[] = {inviting ? "回应邀请" : "互动", "专注", "会话", "设置"};
        strip(icons, names, WHALE_DOCK_COUNT, whale_dock_slot(a->dock),
              inviting || a->badge_unread ? 0 : a->pomo_state ? 1 : -1, 0, true);
        if (a->global_unread) {
            snprintf(value, sizeof value, "%u", a->global_unread);
            panel(menu_box, 160, 250, 29, 16, 0xeac58c, 8);
            text_at(160, 250, 29, 16, &lv_font_cn_12, value, NAV, -1, LV_TEXT_ALIGN_CENTER);
        }
    } else if (a->view == TASK_DETAIL || inline_reply) {
        int n = r->state == 3 ? tool_pose[r->tool] : state_pose[r->state];
        if (r->hint)
            n = r->hint == 1 ? 9 : 10;
        if (!a->linked)
            n = 15;
        if (n == 0 && animate && a->now % 4200 < 250)
            n = 1;
        const char *title =
            a->title_session == r->session && a->title_run == r->run && a->full_title[0]
                ? a->full_title
            : r->title[0] ? r->title
            : a->count    ? "未命名会话"
                          : "等待新任务";
        const char *status = !a->linked                      ? (a->count ? "离线缓存" : "等待电脑")
                             : r->hint                       ? hint_text[r->hint]
                             : r->state == 8 && !r->question ? "请在电脑处理"
                             : r->state == 3                 ? tools[r->tool]
                                                             : states[r->state];
        int accent = r->state == 6                    ? RED
                     : r->state == 5                  ? 0x3a766a
                     : r->state == 4 || r->state == 8 ? 0x8c6b32
                     : r->state <= 1 || r->state == 7 ? MUT
                                                      : TEAL;
        uint32_t sec = r->elapsed;
        if (a->ready && (r->state == 2 || r->state == 3 || r->state == 4 || r->state == 8))
            sec += (a->now - r->at) / 1000;
        if (sec >= 3600)
            snprintf(value, sizeof value, "%lu:%02lu:%02lu", (unsigned long)sec / 3600,
                     (unsigned long)sec / 60 % 60, (unsigned long)sec % 60);
        else
            snprintf(value, sizeof value, "%02lu:%02lu", (unsigned long)sec / 60,
                     (unsigned long)sec % 60);
        bool todo = (r->flags & 1) && r->total;
        bool available = a->count && a->replies[a->selected].session == r->session &&
                         a->replies[a->selected].revision;
        bool newer =
            inline_reply && available && a->replies[a->selected].revision != a->read_revision;
        if (inline_reply) {
            pose_at(n, 13, 28, 67);
            char short_title[80];
            if (*title_line(title, short_title, sizeof short_title, &lv_font_cn_14, 154)) {
                title_line(title, short_title, sizeof short_title, &lv_font_cn_14, 140);
                strcat(short_title, "…");
            }
            text_at(74, 34, 154, 20, &lv_font_cn_14, short_title, NAV, PAPER, LV_TEXT_ALIGN_LEFT);
            text_at(74, 59, 95, 18, &lv_font_cn_12, status, accent, PAPER, LV_TEXT_ALIGN_LEFT);
            text_at(162, 59, 65, 18, &lv_font_cn_12, value, NAV, PAPER, LV_TEXT_ALIGN_RIGHT);
            panel(sheet, 0, 88, 240, 250, 0xffffff, 18);
            text_at(16, 105, 140, 20, &lv_font_cn_14, newer ? "最近回复 · 有更新" : "最近回复", NAV,
                    0xffffff, LV_TEXT_ALIGN_LEFT);
            if (a->view == REPLY_READ) {
                if (!a->read_revision)
                    center(164, "还没有最终回复", MUT, -1);
                const char *body = a->answer;
                char line[80];
                for (int j = 0; j < a->scroll && *body; j++)
                    body = next_line(body, line);
                for (int j = 0; j < HOME_REPLY_LINES && *body; j++) {
                    body = next_line(body, line);
                    text_at(16, 134 + j * 25, 208, 21, &lv_font_cn_16, line, NAV, 0xffffff,
                            LV_TEXT_ALIGN_LEFT);
                }
            } else {
                text_at(16, 181, 208, 22, &lv_font_cn_16,
                        a->view == REPLY_LOAD ? "正在读取最近回复…" : "暂时没能读取回复", MUT,
                        0xffffff, LV_TEXT_ALIGN_CENTER);
            }
            const char *nav = a->view == REPLY_LOAD || a->read_pending ? "正在读取"
                              : a->view == REPLY_FAILED                ? "↓ 重试"
                              : a->read_error                          ? "↓ 重试"
                              : !a->read_first && !a->scroll           ? "↑ 收起  ↓ 阅读"
                              : a->read_first + a->read_used == a->read_count &&
                                      a->scroll >= whale_home_scroll_max(a->answer)
                                  ? "↑ 上翻  已读完"
                                  : "↑↓ 阅读";
            panel(action_bar, 0, 292, 240, 28, BAR, 0);
            text_at(16, 298, 132, 18, &lv_font_cn_12, nav, NAV, -1, LV_TEXT_ALIGN_LEFT);
            text_at(151, 298, 73, 18, &lv_font_cn_12, "确认 收起", NAV, -1,
                    LV_TEXT_ALIGN_RIGHT);
        } else {
            parent_icon(ICON_chat, false);
            text_at(151, 35, 76, 18, &lv_font_cn_12, value, MUT, PAPER, LV_TEXT_ALIGN_RIGHT);
            lv_obj_set_pos(title_clip, 16, 62);
            home_title(a, title);
            pose_at(n, 29, 87 + (animate && r->state >= 2 && r->state <= 4 && a->now % 1700 < 650),
                    236);
            if (todo)
                snprintf(text, sizeof text, "%s %u/%u", r->state == 3 ? "执行" : states[r->state],
                         r->done, r->total);
            else
                snprintf(text, sizeof text, "%s", status);
            text_at(14, 268, 148, 20, &lv_font_cn_12,
                    r->question ? "?  长按↓作答"
                    : todo || r->hint || r->state == 8 || r->state == 6 || !a->linked ? text
                                                                                      : "",
                    accent, PAPER, LV_TEXT_ALIGN_LEFT);

            panel(action_bar, 0, 292, 240, 28, BAR, 0);
            static const char *const keys[] = {"↑", "●", "↓"};
            static const enum room_icon shortcuts[] = {ICON_chat, ICON_mic, ICON_read};
            for (int i = 0; i < 3; i++) {
                text_at(26 + i * 73, 298, 16, 19, &lv_font_cn_14, keys[i], TEAL, -1,
                        LV_TEXT_ALIGN_CENTER);
                icon_at(shortcuts[i], 44 + i * 73, 297, TEAL);
            }
            if (r->flags & 8)
                panel(menu_box, 210, 296, 5, 5, TEAL, 3);
        }
    } else if (a->view == COMPANION) {
        static const char *choices[] = {"打招呼", "摸摸", "喂小鱼", "喝咖啡",
                                        "服饰",   "徽章"};
        static const char *lines[] = {"我在呢，慢慢来。", "嗨，我在这里呀。", "嘿嘿，再摸摸嘛。",
                                      "啊呜，谢谢你的小鱼。", "喝咖啡"};
        unsigned action = a->companion_action <= 4 ? a->companion_action : 0;
        unsigned elapsed = a->now - a->action_at;
        /* Leave a clear line for action feedback above the dock. */
        const char *pending_offer = a->invitation == a->companion_choice + 1 && !a->back_focus
                                        ? invitation_line(a) : NULL;
        bool feedback = action || pending_offer || a->unlock_new || a->badge_unread;
        unsigned scale = feedback ? 236 : 256;
        int n = 16, x = feedback ? 29 : 22, y = 44;
        const char *line = lines[action];
        if (action && a->companion_invited) {
            static const char *const replies[] = {"嘿，等到你啦。", "嘿嘿，就等你啦。",
                                                  "啊呜，收到小鱼啦。", "一起喝，慢慢来。"};
            line = replies[action - 1];
        }
        if (!action) {
            unsigned phase = (a->now - a->companion_at) % 24000;
            n = phase > 20000 && phase < 22000      ? 21
                : animate && a->now % 5000 > 4750   ? 17
                                                    : 16;
            if (animate)
                y -= (a->now / 750) % 2 ? 2 : 0;
            static const char *idle[] = {"我在呢，慢慢来。", "陪你歇一小会儿。",
                                         "一起等下一件事吧。"};
            line = idle[((a->now - a->companion_at) / 24000) % 3];
        } else {
            n = action == 1 ? 18 : action == 2 ? 19 : 20;
            if (animate) {
                if (action == 1)
                    x += (elapsed / 250) % 2 ? 2 : -2;
                else
                    y -= (elapsed / 375) % 2 ? 2 : 0;
            }
            if (action == 4) {
                if (elapsed >= 2200) {
                    n = 19;
                    line = "嗯，精神一点啦。";
                } else {
                    painted_coffee = true;
                    show_coffee(x, y, scale, elapsed, animate);
                }
            }
        }
        dressed_acc(a->worn, a->worn_acc, n, x, y, scale);
        parent_icon(ICON_home, a->back_focus);
        text_at(54, 35, 132, 18, &lv_font_cn_14, "互动", NAV, -1, LV_TEXT_ALIGN_CENTER);
        if (action)
            text_at(8, 232, 224, 21, &lv_font_cn_14, line, NAV, -1, LV_TEXT_ALIGN_CENTER);
        else if (pending_offer)
            text_at(8, 232, 224, 21, &lv_font_cn_14, pending_offer, NAV, -1,
                    LV_TEXT_ALIGN_CENTER);
        else if (a->unlock_new)
            text_at(8, 232, 224, 21, &lv_font_cn_14,
                    "新服饰已解锁，去服饰试试",
                    NAV, -1, LV_TEXT_ALIGN_CENTER);
        else if (a->badge_unread)
            text_at(8, 232, 224, 21, &lv_font_cn_14,
                    "新徽章已解锁，去徽章看看",
                    NAV, -1, LV_TEXT_ALIGN_CENTER);
        static const enum room_icon icons[] = {ICON_wave,   ICON_pat,    ICON_fish,   ICON_coffee,
                                               ICON_hanger, ICON_flower};
        int comp_active = a->unlock_new ? 4 : a->badge_unread ? 5 : -1;
        unsigned cidx = a->companion_choice < 6 ? a->companion_choice : 0;
        int cwin = cidx >= 4 ? (int)cidx - 3 : 0;
        if (cwin > 2)
            cwin = 2;
        enum room_icon w_icons[4];
        const char *w_names[4];
        int w_active = -1;
        for (int i = 0; i < 4; i++) {
            w_icons[i] = icons[cwin + i];
            w_names[i] = choices[cwin + i];
            if (comp_active == cwin + i)
                w_active = i;
        }
        strip(w_icons, w_names, 4, a->back_focus ? -1 : (int)cidx - cwin, w_active, 0, false);
        if (cwin > 0)
            text_at(2, 264, 12, 18, &lv_font_cn_14, "‹", MUT, -1, LV_TEXT_ALIGN_CENTER);
        if (cwin + 4 < 6)
            text_at(226, 264, 12, 18, &lv_font_cn_14, "›", MUT, -1, LV_TEXT_ALIGN_CENTER);
    } else if (a->view == WARDROBE) {
        unsigned slot_idx = a->outfit_choice < WHALE_WARDROBE_COUNT ? a->outfit_choice : 0;
        uint8_t skin = whale_wardrobe_order[slot_idx];
        uint8_t preview = a->back_focus ? a->worn : (uint8_t)((1u << skin) & 30u);
        dressed_acc(preview, a->worn_acc, 16, 22, 44, 256);
        parent_icon(ICON_heart, a->back_focus);
        center(35, "服饰", NAV, -1);
        static const enum room_icon icons[WHALE_WARDROBE_COUNT] = {
            ICON_skin_maid, ICON_skin_pajamas, ICON_skin_witch, ICON_skin_lounge,
            ICON_skin_scholar};
        static const char *const names[WHALE_WARDROBE_COUNT] = {
            "女仆装", "长袖睡衣", "见习魔女", "连帽家居服", "书院大小姐"};
        int worn = whale_wardrobe_order[whale_skin_id(a->worn)];
        unsigned locked = 0;
        for (unsigned slot = 1; slot < WHALE_WARDROBE_COUNT; slot++)
            if (!(a->unlocked & (1u << whale_wardrobe_order[slot])))
                locked |= 1u << slot;
        strip(icons, names, WHALE_WARDROBE_COUNT, a->back_focus ? -1 : (int)slot_idx, worn, locked,
              false);
        if (a->locked_notice)
            center(232, skin == 1   ? "亲密度20解锁"
                        : skin == 2 ? "亲密度40解锁"
                        : skin == 3 ? "亲密度60解锁"
                                    : "亲密度80解锁",
                   MUT, -1);
    } else if (a->view == BADGES) {
        unsigned idx = a->badge_choice < WHALE_BADGE_COUNT ? a->badge_choice : 0;
        uint8_t preview_acc = a->back_focus ? a->worn_acc : whale_badge_wear[idx];
        int n = animate && a->now % 5000 > 4750 ? 17 : 16;
        dressed_acc(a->worn, preview_acc, n, 36, 56, 220);
        parent_icon(ICON_heart, a->back_focus);
        unsigned owned = 0;
        for (uint8_t m = a->badges; m; m &= m - 1)
            owned++;
        snprintf(value, sizeof value, "徽章 %u/%u", owned, (unsigned)WHALE_BADGE_COUNT);
        center(33, value, NAV, -1);
        int win = idx <= 1 ? 0 : idx >= WHALE_BADGE_COUNT - 2 ? WHALE_BADGE_COUNT - 4 : (int)idx - 1;
        enum room_icon icons[4];
        const char *names[4];
        unsigned locked = 0;
        int active = -1;
        for (int i = 0; i < 4; i++) {
            int b = win + i;
            icons[i] = badge_icons[b];
            names[i] = badge_names[b];
            if (!(a->badges & (1u << b)))
                locked |= 1u << i;
            if (a->badge_equipped == b + 1)
                active = i;
        }
        strip(icons, names, 4, a->back_focus ? -1 : (int)idx - win, active, locked, false);
        if (win > 0)
            text_at(2, 264, 12, 18, &lv_font_cn_14, "‹",
                    (a->badge_unread & ((1u << win) - 1u)) ? TEAL : MUT, -1, LV_TEXT_ALIGN_CENTER);
        if (win + 4 < WHALE_BADGE_COUNT)
            text_at(226, 264, 12, 18, &lv_font_cn_14, "›",
                    (a->badge_unread >> (win + 4)) ? TEAL : MUT, -1, LV_TEXT_ALIGN_CENTER);
        int hint_color = MUT;
        text[0] = 0;
        if (a->back_focus) {
            if (a->worn_acc >= 1 && a->worn_acc <= WHALE_ACC_SETS) {
                snprintf(text, sizeof text, "佩戴中 · %s", wear_names[a->worn_acc]);
                hint_color = TEAL;
            }
        } else if (a->badges & (1u << idx)) {
            uint8_t w = whale_badge_wear[idx];
            if (a->badge_equipped == idx + 1) {
                snprintf(text, sizeof text, "佩戴中 · %s · 确认卸下", wear_names[w]);
                hint_color = TEAL;
            } else {
                snprintf(text, sizeof text, "可装扮 · %s", wear_names[w]);
                hint_color = NAV;
            }
        } else {
            unsigned today_done =
                !a->summary_stale && a->summary_valid ? (a->summary_done > 5 ? 5u : a->summary_done) : 0u;
            unsigned today_min = !a->summary_stale && a->summary_valid
                                     ? (a->summary_seconds / 60 > 120 ? 120u : a->summary_seconds / 60)
                                     : 0u;
            const char *prefix = a->locked_notice ? "未解锁 · " : "";
            hint_color = a->locked_notice ? 0x8c6b32 : MUT;
            switch (idx) {
            case 0:
                snprintf(text, sizeof text, "%s完成 1 个任务解锁", prefix);
                break;
            case 1:
                snprintf(text, sizeof text, "%s设备作答 %u/10 解锁", prefix, (unsigned)a->badge_answers);
                break;
            case 2:
                snprintf(text, sizeof text, "%s语音发送 %u/5 解锁", prefix, (unsigned)a->badge_voices);
                break;
            case 3:
                snprintf(text, sizeof text, "%s单日完成 %u/5 个解锁", prefix, today_done);
                break;
            case 4:
                snprintf(text, sizeof text, "%s单日工作 %u/120m 解锁", prefix, today_min);
                break;
            case 5:
                snprintf(text, sizeof text, "%s夜间完成任务解锁", prefix);
                break;
            case 6:
                snprintf(text, sizeof text, "%s房间互动 %u/15 解锁", prefix, (unsigned)a->badge_taps);
                break;
            default:
                snprintf(text, sizeof text, "%s亲密度 %u/60 解锁", prefix,
                         a->bond > 60 ? 60u : (unsigned)a->bond);
                break;
            }
        }
        if (text[0])
            text_at(8, 231, 224, 19, &lv_font_cn_14, text, hint_color, -1, LV_TEXT_ALIGN_CENTER);
    } else if (a->view == POMODORO) {
        int pose_id = a->pomo_state == WHALE_POMO_FOCUS && !a->pomo_paused ? 3
                      : a->pomo_state == WHALE_POMO_BREAK                  ? 20
                      : animate && a->now % 5000 > 4750                    ? 17
                                                                           : 16;
        dressed_acc(a->worn, a->worn_acc, pose_id, 22, 44, 256);
        parent_icon(a->pomo_time_select ? ICON_clock : ICON_home, a->back_focus);
        if (a->pomo_state) {
            uint32_t s = (a->pomo_rem_ms + 999u) / 1000u;
            snprintf(value, sizeof value, "%s · %02lu:%02lu",
                     a->pomo_paused                       ? "已暂停"
                     : a->pomo_state == WHALE_POMO_BREAK ? "休息"
                                                         : "专注",
                     (unsigned long)(s / 60u), (unsigned long)(s % 60u));
            center(35, value, NAV, -1);
            if (a->pomo_alert)
                snprintf(text, sizeof text,
                         a->pomo_alert == 1 ? "专注完成，歇会儿吧" : "休息结束，继续加油");
            else
                snprintf(text, sizeof text, "今日已完成 %u 个番茄", (unsigned)a->pomo_today);
            text_at(8, 232, 224, 20, &lv_font_cn_14, text,
                    a->pomo_alert ? TEAL : MUT, -1, LV_TEXT_ALIGN_CENTER);
            static const enum room_icon act_icons[] = {ICON_clock, ICON_cancel, ICON_read};
            const char *act_names[] = {a->pomo_paused ? "继续" : "暂停", "结束", "日记"};
            strip(act_icons, act_names, 3, a->back_focus ? -1 : a->pomo_choice, 0, 0, false);
        } else if (a->pomo_time_select) {
            center(35, "选择时间", NAV, -1);
            text_at(8, 232, 224, 20, &lv_font_cn_14, "确认开始专注", MUT, -1,
                    LV_TEXT_ALIGN_CENTER);
            static const enum room_icon time_icons[] = {ICON_clock, ICON_clock, ICON_clock};
            static const char *const time_names[] = {"15分钟", "30分钟", "60分钟"};
            strip(time_icons, time_names, 3, a->back_focus ? -1 : a->pomo_choice, -1, 0, false);
        } else {
            center(35, "专注", NAV, -1);
            if (a->pomo_alert)
                snprintf(text, sizeof text,
                         a->pomo_alert == 1 ? "专注完成，歇会儿吧" : "休息结束，继续加油");
            else if (a->pomo_today)
                snprintf(text, sizeof text, "今日已完成 %u 个番茄", (unsigned)a->pomo_today);
            else
                snprintf(text, sizeof text, "设置时间，再开始专注");
            text_at(8, 232, 224, 20, &lv_font_cn_14, text,
                    a->pomo_alert ? TEAL : MUT, -1, LV_TEXT_ALIGN_CENTER);
            static const unsigned minutes[] = {15, 30, 60};
            snprintf(value, sizeof value, "开始 · %u分钟", minutes[a->pomo_preset < 3 ? a->pomo_preset : 0]);
            static const enum room_icon pre_icons[] = {ICON_clock, ICON_send, ICON_read};
            const char *pre_names[] = {"时间", value, "日记"};
            strip(pre_icons, pre_names, 3, a->back_focus ? -1 : a->pomo_choice, -1, 0, false);
        }
    } else if (a->view == DIARY) {
        parent_icon(ICON_clock, a->back_focus);
        bool diary_gap = a->work_day_valid && (!a->diary_count || a->diary[0].day != a->work_day);
        unsigned slots = diary_gap ? (unsigned)a->diary_count + 1u
                                   : (a->diary_count ? a->diary_count : 1u);
        unsigned choice = a->diary_choice < slots ? a->diary_choice : 0;
        const whale_diary_entry_t *e = NULL;
        if (diary_gap)
            e = choice == 0 ? NULL : &a->diary[choice - 1u];
        else if (a->diary_count)
            e = &a->diary[choice];
        if (!e) {
            center(35, a->work_day_valid ? "今日小日记" : "收工小日记", NAV, -1);
        } else if (a->work_day_valid && e->day == a->work_day) {
            center(35, "今日小日记", NAV, -1);
        } else {
            whale_local_time_t dt;
            whale_clock_civil((int64_t)e->day * 86400 + WHALE_WORK_DAY_START_HOUR * 3600, &dt);
            snprintf(value, sizeof value, "%u月%u日日记", dt.month, dt.day);
            center(35, value, NAV, -1);
        }
        uint8_t skin_id = e ? whale_diary_skin_id(e->flags) : 0;
        uint8_t d_worn = skin_id ? (uint8_t)(1u << skin_id) : 0;
        uint8_t d_acc = e ? (uint8_t)((e->flags >> 2) & 7u) : 0;
        dressed_acc(e ? d_worn : a->worn, e ? d_acc : a->worn_acc, 16, 71, 60, 128);
        panel(sheet, 12, 168, 216, 114, CARD, 14);
        if (!e || (!e->done && !e->seconds && !e->pomo)) {
            text_at(22, 184, 196, 20, &lv_font_cn_14,
                    !e && !a->work_day_valid ? "未校时 · 暂无日记" : "今天还没开始记录", NAV, -1,
                    LV_TEXT_ALIGN_CENTER);
            paragraph_at(22, 214, 196, 46,
                         "连接电脑开工或开一轮专注，小鲸会帮你记下每日收获。", MUT,
                         LV_TEXT_ALIGN_CENTER);
        } else {
            unsigned mins = e->seconds / 60u;
            if (mins >= 60u)
                snprintf(value, sizeof value, "完成%u项 · %uh%02um · %u番茄", (unsigned)e->done,
                         mins / 60u, mins % 60u, (unsigned)e->pomo);
            else
                snprintf(value, sizeof value, "完成%u项 · %um · %u番茄", (unsigned)e->done, mins,
                         (unsigned)e->pomo);
            text_at(20, 180, 200, 20, &lv_font_cn_14, value, TEAL, -1, LV_TEXT_ALIGN_CENTER);
            const char *note =
                e->done >= 5 || mins >= 120u
                    ? "今天真高产！辛苦忙了一整天，记得喝水早点休息。"
                : (e->flags & 0x20u)
                    ? "夜深了还在陪你赶进度，收工后好好睡一觉吧。"
                : e->pomo >= 2
                    ? "节奏稳稳的，专注把一件事做完的感觉真棒！"
                    : "一点一滴都在推进，今天也有好好陪伴彼此。";
            paragraph_at(22, 208, 196, 60, note, NAV, LV_TEXT_ALIGN_LEFT);
        }
        panel(action_bar, 0, 292, 240, 28, BAR, 0);
        text_at(8, 297, 224, 19, &lv_font_cn_12,
                slots > 1 ? "↑↓ 翻看往日  ·  确认 返回" : "确认 返回", NAV, -1,
                LV_TEXT_ALIGN_CENTER);
    } else if (desk) {
        static const char *week[] = {"日", "一", "二", "三", "四", "五", "六"};
        const whale_local_time_t *t = &a->clock;
        char clock[8], line[96];
        if (a->clock_valid)
            snprintf(clock, sizeof clock, "%02u:%02u", t->hour % 24u, t->minute % 60u);
        else
            snprintf(clock, sizeof clock, "--:--");
        if (a->soc < 0)
            snprintf(value, sizeof value, "电量 --%%");
        else
            snprintf(value, sizeof value, "电量 %s%d%%", a->soc_stale ? "~" : "", a->soc);
        if (a->clock_valid)
            snprintf(line, sizeof line, "%u月%u日 周%s  ·  %s", t->month, t->day, week[t->wday % 7],
                     value);
        else
            snprintf(line, sizeof line, "等待电脑校准时间  ·  %s", value);
        int low = a->battery_level == 0 ? -1 : a->battery_level == 1 ? GOLD : RED;
        /* Today's summary replaces the plain clock unless chosen otherwise in
         * settings; without totals from the bridge the plain clock remains. */
        bool summary = !a->standby_mode && a->summary_valid;
        bool idle_day = summary && (a->summary_stale || (!a->summary_done && !a->summary_seconds));
        if (a->night) {
            /* Night: she is asleep; only a dark clock remains. */
            panel(sea, 0, 0, 240, 320, 0x0b1120, 0);
            int y = summary && !idle_day ? 84 : 102;
            text_at(0, y, 240, 67, &lv_font_cn_64, clock, 0x5b6a82, -1, LV_TEXT_ALIGN_CENTER);
            text_at(8, y + 82, 224, 20, &lv_font_cn_14, line, low >= 0 ? low : 0x3f4d63, -1,
                    LV_TEXT_ALIGN_CENTER);
            if (summary && !idle_day)
                summary_card(a, 196, 0x121a2b, 0x22304a, 0x5b6a82, 0x3f4d63);
        } else if (summary) {
            text_at(0, 0, 240, 67, &lv_font_cn_64, clock, NAV, PAPER, LV_TEXT_ALIGN_CENTER);
            text_at(8, 66, 224, 20, &lv_font_cn_14, line, low >= 0 ? low : MUT, PAPER,
                    LV_TEXT_ALIGN_CENTER);
            unsigned phase = (a->now - a->page_at) % 24000;
            int n = a->pomo_state == WHALE_POMO_FOCUS && !a->pomo_paused ? 3
                    : a->pomo_state == WHALE_POMO_BREAK                  ? 20
                    : phase > 20000 && phase < 22000                     ? 21
                    : animate && a->now % 5000 > 4750                    ? 17
                                                                         : 16;
            const char *note = desk_line(a, idle_day);
            if (a->pomo_alert) {
                note = a->pomo_alert == 1 ? "专注完成，歇会儿吧" : "休息结束，继续加油";
            } else if (a->pomo_state) {
                uint32_t s = (a->pomo_rem_ms + 999u) / 1000u;
                snprintf(value, sizeof value, "%s %02lu:%02lu",
                         a->pomo_paused                       ? "已暂停"
                         : a->pomo_state == WHALE_POMO_BREAK ? "休息"
                                                             : "专注",
                         (unsigned long)(s / 60u), (unsigned long)(s % 60u));
                note = value;
            }
            if (idle_day) {
                int w = text_width(&lv_font_cn_14, note) + 28;
                dressed_acc(a->worn, a->worn_acc, n, 45, 92, 196);
                panel(sheet, 120 - w / 2, 254, w, 30, CARD, 15);
                text_at(120 - w / 2, 260, w, 18, &lv_font_cn_14, note, MUT, -1,
                        LV_TEXT_ALIGN_CENTER);
            } else {
                dressed_acc(a->worn, a->worn_acc, n, 54, 88, 172);
                text_at(16, 226, 208, 20, &lv_font_cn_14, note, MUT, PAPER,
                        LV_TEXT_ALIGN_CENTER);
                summary_card(a, 250, CARD, 0xddd3c2, NAV, MUT);
            }
        } else {
            text_at(0, 8, 240, 67, &lv_font_cn_64, clock, NAV, PAPER, LV_TEXT_ALIGN_CENTER);
            unsigned phase = (a->now - a->page_at) % 24000;
            int n = phase > 20000 && phase < 22000    ? 21
                    : animate && a->now % 5000 > 4750 ? 17
                                                      : 16;
            dressed_acc(a->worn, a->worn_acc, n, 22, 82, 256);
            text_at(8, 290, 224, 20, &lv_font_cn_14, line, low >= 0 ? low : MUT, PAPER,
                    LV_TEXT_ALIGN_CENTER);
        }
    } else if (a->view == MANAGE || a->view == SOUND || a->view == STANDBY) {
        bool sound = a->view == SOUND, standby = a->view == STANDBY;
        parent_icon(sound || standby ? ICON_settings : ICON_home, a->back_focus);
        center(35, sound ? "声音提醒" : standby ? "待机画面" : "设备设置", NAV, -1);
        pose_at(sound ? 18 : 9, 22, 60, 245);
        static const enum room_icon settings[] = {ICON_speaker, ICON_bt, ICON_clock, ICON_info};
        static const enum room_icon modes[] = {ICON_music, ICON_voice};
        static const enum room_icon screens[] = {ICON_completed, ICON_clock};
        static const char *const names[] = {"声音提醒", "蓝牙连接", "待机画面", "版本信息"};
        static const char *const sounds[] = {"音效", "语音"};
        static const char *const desks[] = {"今日小结", "时钟"};
        strip(sound ? modes : standby ? screens : settings, sound ? sounds : standby ? desks : names,
              sound || standby ? 2 : 4,
              a->back_focus ? -1
              : sound       ? a->sound_choice
              : standby     ? a->standby_choice
                            : a->menu_choice,
              sound ? a->sound_mode : standby ? a->standby_mode : -1, 0, false);
        if (a->sound_saved < 0)
            center(228, "保存失败，请重试", RED, -1);
    } else if (a->view == VOICE) {
        unsigned v = a->voice_phase;
        if (v == VOICE_PREVIEW) {
            fulltext("核对语音文字", a->voice_text, a->scroll);
            foot = "上下阅读  确认看操作";
        } else if (v == VOICE_ACTIONS) {
            text_at(16, 40, 208, 24, &lv_font_cn_18, "核对识别文字", NAV, PAPER,
                    LV_TEXT_ALIGN_CENTER);
            text_at(12, 73, 216, 20, &lv_font_cn_14, a->voice_title, MUT, PAPER,
                    LV_TEXT_ALIGN_CENTER);
            const char *body = a->voice_text;
            char line[80];
            for (int j = 0; j < 6 && *body; j++) {
                body = next_line(body, line);
                text_at(15, 107 + j * 22, 210, 21, &lv_font_cn_16, line, NAV, PAPER,
                        LV_TEXT_ALIGN_LEFT);
            }
            static const enum room_icon choices[] = {ICON_send, ICON_mic, ICON_cancel};
            static const char *const names[] = {"发送", "重新录音", "取消"};
            strip(choices, names, 3, a->voice_choice, -1, 0, false);
            foot = NULL;
        } else {
            static const char *headings[] = {
                "语音回复",   "正在准备",         "我在听",           "正在识别",
                "核对文字",   "发送选项",         "正在发送",         "已发送",
                "已排队",     "发送结果待核对",   "尚未发送",         "语音尚未就绪",
                "识别未完成", "会话已归档或删除", "请先处理当前问题", "草稿已过期",
                "没有听清",   "录音传输中断",     "电脑已断开",       "录音暂不可用",
                "电脑上没有会话"};
            const char *detail = "确认开始  最长 60 秒";
            int n = 11;
            foot = "确认录音  ↑ 返回";
            switch (v) {
            case VOICE_PREPARE:
                detail = "正在连接本地语音识别";
                foot = "↑ 取消";
                n = 2;
                break;
            case VOICE_RECORD:
                snprintf(value, sizeof value, "%02lu:%02lu / 01:00",
                         (unsigned long)a->voice_ms / 60000,
                         (unsigned long)a->voice_ms / 1000 % 60);
                detail = value;
                foot = "确认结束  ↑ 取消";
                n = 2;
                break;
            case VOICE_FINISH:
                detail = "完成后先核对文字";
                foot = "↑ 取消";
                n = 2;
                break;
            case VOICE_SENDING:
                detail = "等待电脑确认接收";
                foot = "请稍候";
                n = 2;
                break;
            case VOICE_SENT:
                detail = a->voice_new ? "电脑上已开新会话" : "已送达当前会话";
                foot = "确认返回";
                n = 12;
                break;
            case VOICE_QUEUED:
                detail = "当前任务结束后继续";
                foot = "确认返回";
                n = 12;
                break;
            case VOICE_UNKNOWN:
                detail = "保留文字，只查询结果";
                foot = "确认查询  ↑ 看文字";
                n = 10;
                break;
            case VOICE_FAILED:
                detail = "文字保留，可核对后重试";
                foot = "确认看文字  ↑ 返回";
                n = 13;
                break;
            case VOICE_UNSUPPORTED:
                detail = a->voice_new && a->voice_cap && !a->voice_task_cap
                             ? "请更新电脑"
                             : "电脑启用本地语音识别";
                foot = "确认重试  ↑ 返回";
                n = 15;
                break;
            case VOICE_ASR_ERROR:
                detail = "未发送，可重新录音";
                foot = "确认重录  ↑ 返回";
                n = 13;
                break;
            case VOICE_ARCHIVED:
                detail = "文字不会发给其他会话";
                foot = "确认回列表  ↑ 看文字";
                n = 15;
                break;
            case VOICE_QUESTION:
                detail = "按原来的问题方式作答";
                foot = "确认去处理  ↑ 返回";
                break;
            case VOICE_EXPIRED:
                detail = "请重新录音";
                foot = "确认重录  ↑ 返回";
                n = 15;
                break;
            case VOICE_EMPTY:
                detail = "靠近设备，再说一次";
                foot = "确认重录  ↑ 返回";
                break;
            case VOICE_BROKEN:
                detail = "未发送，请重新录音";
                foot = "确认重录  ↑ 返回";
                n = 13;
                break;
            case VOICE_DISCONNECTED:
                detail = a->voice_text[0] ? "文字已保留，等待重连" : "请等待电脑重新连接";
                foot = "确认重试  ↑ 返回";
                n = 15;
                break;
            case VOICE_MIC_ERROR:
                detail = "请稍候再试";
                foot = "确认重试  ↑ 返回";
                n = 13;
                break;
            case VOICE_NO_TARGET:
                detail = "先在电脑打开会话";
                foot = "确认返回";
                n = 15;
                break;
            default:
                break;
            }
            text_at(16, 40, 208, 20, &lv_font_cn_14, a->voice_title, MUT, PAPER,
                    LV_TEXT_ALIGN_CENTER);
            pose(n, 72);
            const char *heading = v < sizeof headings / sizeof headings[0] ? headings[v] : "语音回复";
            if (a->voice_new && v == VOICE_SENT)
                heading = "新任务已开始";
            else if (a->voice_new && v == VOICE_PREPARE)
                heading = "说出新任务";
            center(206, heading, NAV, PAPER);
            center(244, detail, MUT, PAPER);
        }
    } else if (a->view == LIST) {
        parent_icon(ICON_home, a->list_back);
        center(35, "会话", NAV, -1);
        if (a->feel == WHALE_FEEL_CELEBRATE)
            pose_at((a->now / 500) % 2 ? 18 : 19, 52,
                    54 - (animate && (a->now / 250) % 2 ? 4 : 0), 178);
        else if (a->feel)
            pose_at(10, 52 + (!animate ? 0 : (a->now / 250) % 2 ? 2 : -2), 54, 178);
        else
            pose_at(task_pose(r), 52, 54, 178);
        /* Row -1 is "语音新任务", above the first session of the first window. */
        int first = whale_list_has_new(a) ? -1 : 0;
        int focus = a->list_new ? -1 : a->selected;
        int start = focus - 2 > first ? focus - 2 : first;
        if (!a->count)
            center(first < 0 ? 278 : 238, "还没有任务", MUT, PAPER);
        if (start < 0) {
            int y = 201;
            if (a->list_new && !a->list_back)
                panel(status_box, 8, y, 218, 34, 0xd6e7da, 7);
            icon_at(ICON_mic, 11, y + 7, TEAL);
            text_at(38, y + 6, 180, 21, &lv_font_cn_14, "语音新任务", TEAL, -1, LV_TEXT_ALIGN_LEFT);
        }
        for (int j = start < 0 ? 0 : start; j < start + 3 && j < a->count; j++) {
            const whale_row_t *t = &a->rows[j];
            int y = 201 + 38 * (j - start);
            bool selected = j == a->selected && !a->list_back && !a->list_new;
            if (selected)
                panel(status_box, 8, y, 218, 34, 0xd6e7da, 7);
            list_state_icon(t, y);
            char short_title[80];
            if (*title_line(t->title[0] ? t->title : "未命名会话", short_title, sizeof short_title, &lv_font_cn_14,
                            128)) {
                title_line(t->title, short_title, sizeof short_title, &lv_font_cn_14, 114);
                strcat(short_title, "…");
            }
            text_at(38, y + 6, 129, 21, &lv_font_cn_14, short_title, NAV, -1, LV_TEXT_ALIGN_LEFT);
            uint32_t sec = t->elapsed;
            if (a->ready && (t->state == 2 || t->state == 3 || t->state == 4 || t->state == 8))
                sec += (a->now - t->at) / 1000;
            snprintf(value, sizeof value, "%lu:%02lu", (unsigned long)sec / 60,
                     (unsigned long)sec % 60);
            text_at(169, y + 8, 54, 19, &lv_font_cn_12, value, MUT, -1, LV_TEXT_ALIGN_RIGHT);
        }
        if (start < 0)
            start = 0; /* scroll bar covers sessions only */
        if (a->total > 3) {
            unsigned visible = a->count - start < 3 ? a->count - start : 3;
            int h = 108 * visible / a->total;
            if (h < 6)
                h = 6;
            unsigned first = a->cursor + start;
            if (first > (unsigned)a->total - visible)
                first = a->total - visible;
            panel(sheet, 230, 203, 3, 108, 0xe1e7de, 2);
            panel(action_bar, 230, 203 + (108 - h) * first / (a->total - visible), 3, h, TEAL, 2);
        }
    } else if (a->view == QUESTION) {
        parent_icon(ICON_chat, a->defer_question);
        const char *title = r->title[0] ? r->title : "当前会话";
        char heading[80];
        if (text_width(&lv_font_cn_16, title) > 132) {
            title_line(title, heading, sizeof heading, &lv_font_cn_16,
                       132 - text_width(&lv_font_cn_16, "…"));
            strcat(heading, "…");
            title = heading;
        }
        text_at(54, 35, 132, 20, &lv_font_cn_16, title, NAV, -1, LV_TEXT_ALIGN_CENTER);
        bool long_prompt = whale_text_wide(a->prompt);
        if (long_prompt) {
            char brief[80];
            title_line(a->prompt, brief, sizeof brief, &lv_font_cn_16, 190);
            strcat(brief, "…");
            center(74, brief, NAV, PAPER);
        } else if (strlen(a->prompt) <= 27) {
            pose_at(11, 182, 60, 55);
            label(12, 74, 166, a->prompt, NAV, PAPER, LV_TEXT_ALIGN_LEFT);
        } else
            center(74, a->prompt, NAV, PAPER);
        bool multi = a->question_mode == WHALE_MODE_MULTI;
        unsigned picks = 0;
        for (uint64_t m = a->picked; m; m &= m - 1)
            picks++;
        if (multi)
            snprintf(text, sizeof text, "多选 · 已选 %u 项 · 第 %u/%u 页", picks, a->page + 1,
                     a->pages);
        else if (a->question_mode == WHALE_MODE_APPROVAL)
            snprintf(text, sizeof text, "请审批");
        else
            snprintf(text, sizeof text, "第 %u/%u 页", a->page + 1, a->pages);
        center(109, text, TEAL, PAPER);
        for (int j = 0; j < a->options; j++) {
            char row[264];
            bool on = multi && a->option_ids[j] < 64 && (a->picked >> a->option_ids[j] & 1);
            snprintf(row, sizeof row, "%s%s", !multi ? "" : on ? "● " : "· ", a->labels[j]);
            label(12, 144 + 42 * j, 216, row,
                  !a->defer_question && j == a->focus ? PAPER : NAV,
                  !a->defer_question && j == a->focus ? NAV : 0xe5f0e9, LV_TEXT_ALIGN_LEFT);
        }
        bool submit_row = multi && a->page + 1 >= a->pages;
        if (submit_row) {
            bool focused = !a->defer_question && a->focus == a->options;
            int y = 144 + 42 * a->options;
            snprintf(text, sizeof text, picks ? "提交 %u 项" : "先选中一项", picks);
            center(y > 266 ? 266 : y, text, focused ? PAPER : TEAL, focused ? TEAL : PAPER);
        }
        foot = a->defer_question                      ? "确认 暂不回答"
               : submit_row && a->focus == a->options ? (picks ? "确认提交" : "先选中一项")
               : a->question_mode == WHALE_MODE_APPROVAL && a->option_ids[a->focus] == 0
                   ? "确认 读全文再提交"
               : multi && whale_text_wide(a->labels[a->focus]) ? "确认展开全文"
               : multi                                ? "确认选中  选完下翻提交"
               : long_prompt && whale_text_wide(a->labels[a->focus]) ? "长按↓读题  确认展开全文"
               : long_prompt                          ? "长按↓读题  确认提交"
               : whale_text_wide(a->labels[a->focus]) ? "确认展开全文"
                                                      : "上下选择  确认提交";
    } else if (a->view == OPTION || a->view == PROMPT) {
        /* Approving an action re-reads the whole request, never just the label. */
        bool approve = a->view == OPTION && a->question_mode == WHALE_MODE_APPROVAL;
        fulltext(approve ? "审批全文" : a->view == PROMPT ? "问题全文" : "选项全文",
                 approve || a->view == PROMPT ? a->prompt : a->labels[a->focus], a->scroll);
        foot = a->view == PROMPT ? "上下滚动  确认读选项"
               : a->question_mode == WHALE_MODE_MULTI ? "上下滚动  确认选中或取消"
                                                      : "上下滚动  确认提交";
    } else {
        const char *heading = "", *detail = "";
        int n = 10;
        switch (a->view) {
        case PAIR:
            heading = "确认绑定电脑";
            snprintf(value, sizeof value, "%06lu", (unsigned long)a->passkey);
            detail = value;
            foot = "确认绑定  长按拒绝";
            n = 11;
            break;
        case REPLY_LOAD:
            heading = "正在读取回复";
            break;
        case REPLY_FAILED:
            heading = "回复需重新读取";
            detail = "确认重试  长按返回";
            break;
        case LIST_LOAD:
            heading = "正在读取任务";
            break;
        case LIST_FAILED:
            heading = "取页失败";
            detail = "确认重试";
            n = 15;
            break;
        case MANAGE:
            heading = "绑定管理";
            detail = "确认管理  上下看版本";
            break;
        case KEEP:
            heading = a->ready ? "蓝牙已连接" : "电脑未连接";
            detail = "下键选择解除绑定";
            break;
        case REVOKE:
            heading = "确认解除绑定";
            detail = "确认断开并清除配对";
            n = 14;
            break;
        case UNBINDING:
            heading = "正在解绑";
            break;
        case UNBIND_FAILED:
            heading = "解绑未完成";
            detail = "确认重试";
            n = 13;
            break;
        case VERSION:
            heading = "固件 v0.18.0 Dev2";
            detail = "HARNESS-WHALE · BLE 2.3";
            break;
        case LOADING:
            heading = a->meta ? "正在读取选项" : "正在读取题干";
            n = 2;
            if (a->parts) {
                snprintf(value, sizeof value, "已收 %u / %u 片", a->received, a->parts);
                detail = value;
            } else {
                detail = a->now - a->page_at >= 1000 ? "已等待 1 秒" : "等待设备数据";
            }
            break;
        case LOAD_FAILED:
            heading = "选项读取失败";
            detail = "确认重新读取";
            n = 15;
            break;
        case SENDING:
            heading = "正在提交";
            detail = "等待电脑确认";
            break;
        case CHECKING:
            heading = "等待核对";
            detail = a->ready ? "正在查询原答复" : "重连后查询原答复";
            break;
        case UNSURE:
            heading = "结果仍待核对";
            detail = "确认只查询原答复";
            break;
        case RESULT:
            heading = a->unsupported   ? "请在电脑处理"
                      : a->result == 0 && a->question_mode == WHALE_MODE_APPROVAL ? "审批已提交"
                      : a->result == 0 ? "回答已接受"
                      : a->result == 1 ? "已在电脑处理"
                      : a->result == 2 ? "问题已撤回"
                                       : "答案已过期";
            n = a->result <= 1 ? 12 : 14;
            /* Pending queue: the next question opens by itself. Count only a
             * summary newer than the answer, which no longer includes it. */
            if (a->result == 0 && !a->unsupported && a->global_valid && a->global_waiting &&
                (int32_t)(a->global_at - a->page_at) >= 0) {
                snprintf(value, sizeof value, "还有 %u 个待处理，接着打开",
                         (unsigned)a->global_waiting);
                detail = value;
            }
            break;
        default:
            break;
        }
        pose(n, 45);
        center(172, heading, PAPER, TEAL);
        center(217, detail, NAV, PAPER);
    }
    for (unsigned i = icon_count; i < sizeof icon_slots / sizeof icon_slots[0]; i++)
        lv_obj_add_flag(icon_slots[i], LV_OBJ_FLAG_HIDDEN);
    if (!painted_coffee) {
        lv_obj_add_flag(coffee_cup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(coffee_steam, LV_OBJ_FLAG_HIDDEN);
    }
    if (!painted_pose)
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    if (a->view != HOME && a->view != WARDROBE && a->view != BADGES && a->view != POMODORO &&
        a->view != DIARY && a->view != LIST && a->view != MANAGE && a->view != SOUND &&
        a->view != STANDBY && a->view != COMPANION && a->view != TASK_DETAIL && !inline_reply &&
        !desk && foot) {
        panel(action_bar, 0, 292, 240, 28, BAR, 0);
        text_at(8, 297, 224, 19, &lv_font_cn_12, foot, NAV, -1, LV_TEXT_ALIGN_CENTER);
    }
    for (unsigned i = 0; i < sizeof panels / sizeof panels[0]; i++)
        if (!(visible_panels & (1u << i)))
            lv_obj_add_flag(*panels[i], LV_OBJ_FLAG_HIDDEN);
    for (unsigned i = used; i < LABEL_COUNT; i++)
        lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
}
