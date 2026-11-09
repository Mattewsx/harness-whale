#include "lvgl.h"
#include "whale_app.h"
#include "whale_assets.h"
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
/* Outfit sets 1 and 2 redraw these sprite indices (tools/make_whale_outfits.py). */
extern const uint8_t outfit_poses[] asm("_binary_outfits_rgb_start");
static const uint8_t outfit_pose_ids[] = {1, 3, 10, 15, 16, 17, 18, 19, 20, 21};
enum { OUTFIT_SETS = 2, OUTFIT_POSES = sizeof outfit_pose_ids, OUTFIT_BASE = 22 };
static lv_image_dsc_t sprites[OUTFIT_BASE + OUTFIT_SETS * OUTFIT_POSES], props[2];
static lv_obj_t *coffee_cup, *coffee_steam, *title_clip, *title_text;
static uint32_t title_started, title_session;
static uint64_t title_epoch;
static char shown_title[513];
static bool title_visible;
enum { LABEL_COUNT = 22 };
static lv_obj_t *labels[LABEL_COUNT], *image;
static lv_obj_t *sea, *sheet, *top_bar, *action_bar, *status_box;
static lv_obj_t *menu_box, *hold_box, *hold_track, *hold_fill;
static lv_obj_t **const panels[] = {&sea,      &sheet,    &top_bar,    &action_bar, &status_box,
                                    &menu_box, &hold_box, &hold_track, &hold_fill};
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
static void text_at(int x, int y, int w, int h, const lv_font_t *font, const char *s, int fg,
                    int bg, lv_text_align_t align) {
    if (used >= LABEL_COUNT)
        return;
    lv_obj_t *o = labels[used++];
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
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
    if (strcmp(lv_label_get_text(o), s))
        lv_label_set_text(o, s);
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
    lv_obj_set_pos(image, x, y);
    if (previous_pose != index) {
        lv_image_set_src(image, sprites + index);
        previous_pose = index;
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
static void strip(const enum room_icon *icons, const char *const *names, int count, int selected,
                  int active, unsigned locked, bool home) {
    int step = home ? 66 : count == 5 ? 43 : 55;
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
static void dressed(const whale_app_t *a, int pose_id, int x, int y, unsigned scale) {
    int set = a->worn & 4 ? 2 : a->worn & 2 ? 1 : 0;
    if (set)
        for (unsigned i = 0; i < OUTFIT_POSES; i++)
            if (outfit_pose_ids[i] == pose_id) {
                pose_id = OUTFIT_BASE + (set - 1) * OUTFIT_POSES + i;
                break;
            }
    pose_at(pose_id, x, y, scale);
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
void whale_ui_init(void) {
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
        sprites[OUTFIT_BASE + i] = sprites[0];
        sprites[OUTFIT_BASE + i].data = outfit_poses + i * 115248;
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
    lv_obj_move_foreground(hold_box);
    lv_obj_move_foreground(hold_track);
    lv_obj_move_foreground(hold_fill);

    for (int i = 0; i < LABEL_COUNT; i++) {
        labels[i] = lv_label_create(screen);
        lv_label_set_long_mode(labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_height(labels[i], 20);
        lv_obj_set_style_text_font(labels[i], &lv_font_cn_16, 0);
        lv_obj_set_style_bg_opa(labels[i], LV_OPA_COVER, 0);
        lv_label_set_text(labels[i], "");
    }
}
static void home_title(const whale_app_t *a, const char *text) {
    if (!title_visible || title_session != a->rows[a->selected].session ||
        title_epoch != a->epoch || strcmp(shown_title, text)) {
        snprintf(shown_title, sizeof shown_title, "%s", text);
        title_started = a->now;
        title_session = a->rows[a->selected].session;
        title_epoch = a->epoch;
        lv_label_set_text(title_text, text);
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
        if (!battery_box) {
            battery_box = lv_obj_create(lv_screen_active());
            lv_obj_remove_style_all(battery_box);
            lv_obj_set_size(battery_box, icon_width, 9);
            lv_obj_set_style_border_width(battery_box, 1, 0);
            battery_fill = lv_obj_create(battery_box);
            lv_obj_remove_style_all(battery_fill);
            lv_obj_set_pos(battery_fill, 1, 1);
            lv_obj_set_style_bg_opa(battery_fill, LV_OPA_COVER, 0);
        }
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
        dressed(a, life[a->activity % 8], 22, y, 256);
        if (a->activity == 1)
            icon_at(ICON_cola, 148, y + 108, 0xab473d);
        static const enum room_icon icons[] = {ICON_heart, ICON_chat, ICON_mic, ICON_settings};
        static const char *const names[] = {"互动", "会话", "说任务", "设置"};
        strip(icons, names, WHALE_DOCK_ITEMS, a->dock, -1, 0, true);
        if (a->global_unread) {
            /* Badge beside the 会话 icon (second of four, 66 px apart). */
            snprintf(value, sizeof value, "%u", a->global_unread);
            panel(menu_box, 97, 250, 29, 16, 0xeac58c, 8);
            text_at(97, 250, 29, 16, &lv_font_cn_12, value, NAV, -1, LV_TEXT_ALIGN_CENTER);
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
        static const char *choices[] = {"打招呼", "摸摸", "喂小鱼", "喝咖啡", "服饰"};
        static const char *lines[] = {"我在呢，慢慢来。", "嗨，我在这里呀。", "嘿嘿，再摸摸嘛。",
                                      "啊呜，谢谢你的小鱼。", "喝咖啡"};
        unsigned action = a->companion_action <= 4 ? a->companion_action : 0;
        unsigned elapsed = a->now - a->action_at;
        /* Leave a clear line for action feedback above the dock. */
        int n = 16, x = 22, y = 44;
        const char *line = lines[action];
        if (!action) {
            unsigned phase = (a->now - a->companion_at) % 24000;
            n = phase > 20000 && phase < 22000 ? 21 : a->now % 5000 > 4750 ? 17 : 16;
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
                    lv_obj_remove_flag(coffee_cup, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_remove_flag(coffee_steam, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_set_pos(coffee_cup, x + 78, y + 104);
                    lv_obj_set_pos(coffee_steam, x + 121,
                                   y + 72 - (animate ? (elapsed / 250) % 4 : 0));
                }
            }
        }
        dressed(a, n, x, y, 256);
        parent_icon(ICON_home, a->back_focus);
        text_at(54, 35, 132, 18, &lv_font_cn_14, "互动", NAV, -1, LV_TEXT_ALIGN_CENTER);
        if (action)
            text_at(8, 232, 224, 21, &lv_font_cn_14, line, NAV, -1, LV_TEXT_ALIGN_CENTER);
        else if (a->unlock_new)
            text_at(8, 232, 224, 21, &lv_font_cn_14,
                    "新服饰已解锁，去服饰试试",
                    NAV, -1, LV_TEXT_ALIGN_CENTER);
        static const enum room_icon icons[] = {ICON_wave, ICON_pat, ICON_fish, ICON_coffee,
                                               ICON_hanger};
        strip(icons, choices, 5, a->back_focus ? -1 : a->companion_choice, -1, 0, false);
    } else if (a->view == WARDROBE) {
        dressed(a, 16, 22, 44, 256);
        parent_icon(ICON_heart, a->back_focus);
        center(35, "服饰", NAV, -1);
        static const enum room_icon icons[] = {ICON_hanger, ICON_hanger, ICON_hanger};
        static const char *const names[] = {"女仆装", "酒红女仆装", "墨绿女仆装"};
        int worn = a->worn & 4 ? 2 : a->worn & 2 ? 1 : 0;
        strip(icons, names, 3, a->back_focus ? -1 : a->outfit_choice, worn, (~a->unlocked) & 6,
              false);
        if (a->locked_notice)
            center(232, "一起完成更多任务后解锁", MUT, -1);
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
        if (a->night) {
            /* Night: she is asleep; only a dark clock remains. */
            panel(sea, 0, 0, 240, 320, 0x0b1120, 0);
            text_at(0, 102, 240, 67, &lv_font_cn_64, clock, 0x5b6a82, -1, LV_TEXT_ALIGN_CENTER);
            text_at(8, 184, 224, 20, &lv_font_cn_14, line, low >= 0 ? low : 0x3f4d63, -1,
                    LV_TEXT_ALIGN_CENTER);
        } else {
            text_at(0, 8, 240, 67, &lv_font_cn_64, clock, NAV, PAPER, LV_TEXT_ALIGN_CENTER);
            unsigned phase = (a->now - a->page_at) % 24000;
            int n = phase > 20000 && phase < 22000 ? 21 : animate && a->now % 5000 > 4750 ? 17 : 16;
            pose_at(n, 22, 82, 256);
            text_at(8, 290, 224, 20, &lv_font_cn_14, line, low >= 0 ? low : MUT, PAPER,
                    LV_TEXT_ALIGN_CENTER);
        }
    } else if (a->view == MANAGE || a->view == SOUND) {
        bool sound = a->view == SOUND;
        parent_icon(sound ? ICON_settings : ICON_home, a->back_focus);
        center(35, sound ? "声音提醒" : "设备设置", NAV, -1);
        pose_at(sound ? 18 : 9, 22, 60, 245);
        static const enum room_icon settings[] = {ICON_speaker, ICON_bt, ICON_info};
        static const enum room_icon modes[] = {ICON_music, ICON_voice};
        static const char *const names[] = {"声音提醒", "蓝牙连接", "版本信息"};
        static const char *const sounds[] = {"音效", "语音"};
        strip(sound ? modes : settings, sound ? sounds : names, sound ? 2 : 3,
              a->back_focus ? -1
              : sound       ? a->sound_choice
                            : a->menu_choice,
              sound ? a->sound_mode : -1, 0, false);
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
        pose_at(task_pose(r), 52, 54, 178);
        int start = a->selected > 2 ? a->selected - 2 : 0;
        if (!a->count)
            center(238, "还没有任务", MUT, PAPER);
        for (int j = start; j < start + 3 && j < a->count; j++) {
            const whale_row_t *t = &a->rows[j];
            int y = 201 + 38 * (j - start);
            bool selected = j == a->selected && !a->list_back;
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
        if (a->total > 3) {
            unsigned visible = a->count - start < 3 ? a->count - start : 3;
            int h = 108 * visible / a->total;
            if (h < 6)
                h = 6;
            unsigned first = a->cursor + start;
            if (first > (unsigned)a->total - visible)
                first = a->total - visible;
            panel(hold_track, 230, 203, 3, 108, 0xe1e7de, 2);
            panel(hold_fill, 230, 203 + (108 - h) * first / (a->total - visible), 3, h, TEAL, 2);
        }
    } else if (a->view == QUESTION) {
        center(41, r->title[0] ? r->title : "当前会话", NAV, PAPER);
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
        snprintf(text, sizeof text, "第 %u/%u 页", a->page + 1, a->pages);
        center(109, text, TEAL, PAPER);
        for (int j = 0; j < a->options; j++)
            label(12, 144 + 42 * j, 216, a->labels[j],
                  !a->defer_question && j == a->focus ? PAPER : NAV,
                  !a->defer_question && j == a->focus ? NAV : 0xe5f0e9, LV_TEXT_ALIGN_LEFT);
        if (a->defer_question)
            parent_icon(ICON_chat, true);
        foot = a->defer_question                      ? "确认 暂不回答"
               : long_prompt && whale_text_wide(a->labels[a->focus]) ? "长按↓读题  确认展开全文"
               : long_prompt                          ? "长按↓读题  确认提交"
               : whale_text_wide(a->labels[a->focus]) ? "确认展开全文"
                                                      : "上下选择  确认提交";
    } else if (a->view == OPTION || a->view == PROMPT) {
        fulltext(a->view == PROMPT ? "问题全文" : "选项全文",
                 a->view == PROMPT ? a->prompt : a->labels[a->focus], a->scroll);
        foot = a->view == OPTION ? "上下滚动  确认提交" : "上下滚动  确认读选项";
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
            heading = "固件 v0.15.0 Demo";
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
                      : a->result == 0 ? "回答已接受"
                      : a->result == 1 ? "已在电脑处理"
                      : a->result == 2 ? "问题已撤回"
                                       : "答案已过期";
            n = a->result <= 1 ? 12 : 14;
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
    if (a->view != HOME && a->view != WARDROBE && a->view != LIST && a->view != MANAGE &&
        a->view != SOUND && a->view != COMPANION && a->view != TASK_DETAIL && !inline_reply &&
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
