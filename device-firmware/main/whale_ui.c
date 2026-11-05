#include "lvgl.h"
#include "whale_app.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Match the tested native renderer. LVGL 9.6 ignores the deprecated KB
 * setting when its byte-sized setting exists, otherwise silently using 64 KiB. */
_Static_assert(LV_MEM_SIZE == 32768, "Whale LVGL pool must match the tested 32 KiB budget");

LV_FONT_DECLARE(lv_font_cn_12);
LV_FONT_DECLARE(lv_font_cn_14);
LV_FONT_DECLARE(lv_font_cn_16);
LV_FONT_DECLARE(lv_font_cn_18);
LV_FONT_DECLARE(lv_font_cn_21);
unsigned whale_glyph_advance(unsigned cp);
extern const uint8_t whale_poses[] asm("_binary_poses_rgb_start");
static lv_image_dsc_t sprites[16];
enum { LABEL_COUNT = 22 };
static lv_obj_t *labels[LABEL_COUNT], *image;
static lv_obj_t *sea, *sheet, *status_box, *footer_line, *link_dot;
static lv_obj_t **const panels[] = {&sea, &sheet, &status_box, &footer_line, &link_dot};
static unsigned visible_panels;
static unsigned used;
static bool painted_pose;
static lv_obj_t *battery_box, *battery_fill;
static int previous_pose = -1;
#define NAV 0x253853
#define TEAL 0x446f9c
#define PAPER 0xfaf9f4
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
        font = &lv_font_cn_21;
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
void whale_ui_init(void) {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(PAPER), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i = 0; i < sizeof panels / sizeof panels[0]; i++) {
        *panels[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(*panels[i]);
        lv_obj_remove_flag(*panels[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    for (int i = 0; i < 16; i++)
        sprites[i] = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                                                 .cf = LV_COLOR_FORMAT_RGB565A8,
                                                 .w = 196,
                                                 .h = 196,
                                                 .stride = 392},
                                      .data_size = 115248,
                                      .data = whale_poses + i * 115248};
    image = lv_image_create(screen);
    for (int i = 0; i < LABEL_COUNT; i++) {
        labels[i] = lv_label_create(screen);
        lv_label_set_long_mode(labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_height(labels[i], 20);
        lv_obj_set_style_text_font(labels[i], &lv_font_cn_16, 0);
        lv_obj_set_style_bg_opa(labels[i], LV_OPA_COVER, 0);
        lv_label_set_text(labels[i], "");
    }
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
static const char *title_line(const char *s, char *line, const lv_font_t *font,
                              unsigned max_width) {
    unsigned bytes = 0, width = 0;
    while (*s) {
        const char *next = s;
        uint32_t cp = decode(&next);
        unsigned advance = lv_font_get_glyph_width(font, cp, 0);
        if (bytes && width + advance > max_width)
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
void whale_ui_draw(const whale_app_t *a) {
    char text[160], value[64];
    used = 0;
    whale_row_t empty = {.state = a->linked ? 1 : 0};
    const whale_row_t *r = a->count ? &a->rows[a->selected] : &empty;
    bool animate = a->battery_level != 2;
    bool inline_reply = a->view == REPLY_READ || a->view == REPLY_LOAD || a->view == REPLY_FAILED;
    painted_pose = false;
    visible_panels = 0;
    panel(link_dot, 13, 11, 5, 5, a->ready ? 0x558c82 : 0x9ca5b2, LV_RADIUS_CIRCLE);
    text_at(23, 4, 90, 18, &lv_font_cn_12,
            a->ready    ? (a->recovered && a->now - a->recovered < 1200 ? "同步已恢复" : "蓝牙已连")
            : a->linked ? "正在同步"
            : a->count  ? "正在重连"
                        : "等待连接",
            MUT, PAPER, LV_TEXT_ALIGN_LEFT);
    if (a->count && !inline_reply) {
        snprintf(text, sizeof text, "%lu/%u", (unsigned long)a->cursor + a->selected + 1, a->total);
        text_at(111, 4, 47, 18, &lv_font_cn_12, text, MUT, PAPER, LV_TEXT_ALIGN_CENTER);
    }
    if (a->soc < 0)
        snprintf(value, sizeof value, "--%%");
    else
        snprintf(value, sizeof value, "%s%d%%", a->soc_stale ? "~" : "", a->soc);
    label(180, 3, 44, value,
          a->battery_level == 0   ? MUT
          : a->battery_level == 1 ? GOLD
                                  : RED,
          PAPER, LV_TEXT_ALIGN_RIGHT);
    if (!battery_box) {
        battery_box = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(battery_box);
        lv_obj_set_pos(battery_box, 161, 8);
        lv_obj_set_size(battery_box, 17, 9);
        lv_obj_set_style_border_width(battery_box, 1, 0);
        lv_obj_set_style_border_color(battery_box, lv_color_hex(MUT), 0);
        battery_fill = lv_obj_create(battery_box);
        lv_obj_remove_style_all(battery_fill);
        lv_obj_set_pos(battery_fill, 1, 1);
        lv_obj_set_style_bg_opa(battery_fill, LV_OPA_COVER, 0);
    }
    lv_obj_set_size(battery_fill, a->soc > 0 ? (a->soc * 13 + 99) / 100 : 0, 5);
    lv_color_t battery_color = lv_color_hex(a->battery_level ? GOLD : 0xa8ddd3);
    if (!lv_color_eq(lv_obj_get_style_bg_color(battery_fill, 0), battery_color))
        lv_obj_set_style_bg_color(battery_fill, battery_color, 0);
    const char *foot = "长按确认返回";
    if (a->view == HOME || inline_reply) {
        int n = r->state == 3 ? tool_pose[r->tool] : state_pose[r->state];
        if (r->hint)
            n = r->hint == 1 ? 9 : 10;
        if (!a->linked)
            n = 15;
        if (n == 0 && animate && a->now % 4200 < 250)
            n = 1;
        const char *title = r->title[0] ? r->title : a->count ? "未命名会话" : "等待新任务";
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
        int tint = r->state == 6                    ? 0xf7e8e7
                   : r->state == 5                  ? 0xe5f0eb
                   : r->state == 4 || r->state == 8 ? 0xf6eddc
                   : r->state <= 1 || r->state == 7 ? 0xe9ecf0
                                                    : 0xe7eef7;
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
            if (*title_line(title, short_title, &lv_font_cn_14, 154)) {
                title_line(title, short_title, &lv_font_cn_14, 140);
                strcat(short_title, "…");
            }
            text_at(74, 34, 154, 20, &lv_font_cn_14, short_title, NAV, PAPER, LV_TEXT_ALIGN_LEFT);
            text_at(74, 59, 95, 18, &lv_font_cn_12, status, accent, PAPER, LV_TEXT_ALIGN_LEFT);
            text_at(162, 59, 65, 18, &lv_font_cn_12, value, NAV, PAPER, LV_TEXT_ALIGN_RIGHT);
            panel(sheet, 0, 88, 240, 250, 0xffffff, 18);
            text_at(16, 105, 140, 20, &lv_font_cn_14, newer ? "最近回复 · 有更新" : "最近回复", NAV,
                    0xffffff, LV_TEXT_ALIGN_LEFT);
            if (a->view == REPLY_READ) {
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
            panel(footer_line, 16, 292, 208, 1, 0xe9edf1, 0);
            text_at(16, 298, 132, 18, &lv_font_cn_12, nav, MUT, 0xffffff, LV_TEXT_ALIGN_LEFT);
            text_at(151, 298, 73, 18, &lv_font_cn_12, "确认 收起", MUT, 0xffffff,
                    LV_TEXT_ALIGN_RIGHT);
        } else {
            char first[80], second[80];
            const char *rest = title_line(title, first, &lv_font_cn_18, 208);
            bool two_lines = *rest != 0;
            title_line(rest, second, &lv_font_cn_18, 208);
            text_at(16, 35, 208, 25, &lv_font_cn_18, first, NAV, PAPER, LV_TEXT_ALIGN_CENTER);
            if (two_lines)
                text_at(16, 59, 208, 25, &lv_font_cn_18, second, NAV, PAPER, LV_TEXT_ALIGN_CENTER);
            panel(sea, 35, 109, 170, 150, 0xeaf0f6, LV_RADIUS_CIRCLE);
            pose_at(n, two_lines ? 34 : 22,
                    (two_lines ? 88 : 62) +
                        (animate && r->state >= 2 && r->state <= 4 && a->now % 1700 < 650),
                    two_lines ? 225 : 256);
            panel(status_box, 16, 264, 110, 25, tint, 7);
            if (todo)
                snprintf(text, sizeof text, "%s %u/%u", r->state == 3 ? "执行" : states[r->state],
                         r->done, r->total);
            else
                snprintf(text, sizeof text, "%s", status);
            text_at(23, 268, 96, 19, &lv_font_cn_12, text, accent, -1, LV_TEXT_ALIGN_CENTER);
            bool hours = sec >= 3600;
            text_at(132, 267, 92, 25, hours ? &lv_font_cn_16 : &lv_font_cn_21, value, NAV, PAPER,
                    LV_TEXT_ALIGN_RIGHT);
            if (!hours)
                text_at(130, 272, 28, 16, &lv_font_cn_12, "用时", MUT, PAPER, LV_TEXT_ALIGN_LEFT);
            panel(footer_line, 16, 294, 208, 1, 0xe2e6e9, 0);
            text_at(16, 299, 132, 17, &lv_font_cn_12,
                    a->count ? (available ? "长按↑录音 ↓回复" : "长按↑ 录音") : "暂无回复",
                    available ? TEAL : MUT, PAPER, LV_TEXT_ALIGN_LEFT);
            text_at(151, 299, 73, 17, &lv_font_cn_12, r->question ? "确认 回答" : "确认 列表", MUT,
                    PAPER, LV_TEXT_ALIGN_RIGHT);
        }
    } else if (a->view == VOICE) {
        unsigned v = a->voice_phase;
        if (v == VOICE_PREVIEW) {
            fulltext("核对语音文字", a->voice_text, a->scroll);
            foot = "上下阅读  确认看操作";
        } else if (v == VOICE_ACTIONS) {
            center(46, "发送给当前会话", NAV, PAPER);
            label(16, 85, 208, a->voice_title, MUT, PAPER, LV_TEXT_ALIGN_CENTER);
            const char *choices[] = {"发送这段文字", "重新录音", "取消并返回"};
            for (unsigned i = 0; i < 3; i++)
                label(16, 146 + i * 42, 208, choices[i], i == a->voice_choice ? PAPER : NAV,
                      i == a->voice_choice ? NAV : 0xe5f0e9, LV_TEXT_ALIGN_CENTER);
            foot = "上下选择  确认执行";
        } else {
            static const char *headings[] = {
                "语音回复",   "正在准备",         "我在听",           "正在识别",
                "核对文字",   "发送选项",         "正在发送",         "已发送",
                "已排队",     "发送结果待核对",   "尚未发送",         "语音尚未就绪",
                "识别未完成", "会话已归档或删除", "请先处理当前问题", "草稿已过期",
                "没有听清",   "录音传输中断",     "电脑已断开",       "录音暂不可用"};
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
                detail = "已送达当前会话";
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
                detail = "电脑启用本地语音识别";
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
            default:
                break;
            }
            text_at(16, 40, 208, 20, &lv_font_cn_14, a->voice_title, MUT, PAPER,
                    LV_TEXT_ALIGN_CENTER);
            pose(n, 72);
            center(206, v < sizeof headings / sizeof headings[0] ? headings[v] : "语音回复", NAV,
                   PAPER);
            center(244, detail, MUT, PAPER);
        }
    } else if (a->view == LIST) {
        center(40, "任务列表", NAV, PAPER);
        int start = (a->selected / 3) * 3;
        if (!a->count) {
            pose(0, 85);
            center(214, "还没有任务", MUT, PAPER);
        }
        for (int j = start; j < start + 3 && j < a->count; j++) {
            const whale_row_t *t = &a->rows[j];
            int y = 73 + (j - start) * 68;
            bool selected = j == a->selected;
            bool waiting = t->question || t->state == 8 || t->state == 4;
            bool error = t->state == 6 || (t->flags & 16);
            bool fresh = t->state == 5 || (t->flags & 8);
            int color = waiting ? (selected ? 0xffd68a : 0x876020)
                        : error ? (selected ? 0xffb3b8 : RED)
                        : fresh ? (selected ? 0xa5e5ca : 0x306d59)
                        : t->state == 2 || t->state == 3 ? (selected ? 0xb4d5ff : 0x38689c)
                                                        : (selected ? 0xc2ccda : MUT);
            lv_obj_t *cards[] = {sea, sheet, status_box};
            panel(cards[j - start], 10, y, 220, 61, selected ? NAV : 0xebeff3, 8);
            text_at(18, y + 5, 204, 21, &lv_font_cn_16,
                    t->title[0] ? t->title : "未命名会话", selected ? PAPER : NAV, -1,
                    LV_TEXT_ALIGN_LEFT);
            text_at(18, y + 33, 204, 19, &lv_font_cn_14,
                  waiting ? "等你回答"
                  : (t->flags & 16)            ? "出错 · 未读"
                  : (t->flags & 8)             ? (t->state == 6 ? "出错 · 有新回复" : "有新回复")
                                               : states[t->state],
                  color, -1, LV_TEXT_ALIGN_LEFT);
        }
        foot = "上下选择  确认进主页";
    } else if (a->view == QUESTION) {
        center(41, r->title[0] ? r->title : "当前会话", NAV, PAPER);
        if (!whale_text_wide(a->prompt) && strlen(a->prompt) <= 27) {
            pose_at(11, 182, 60, 55);
            label(12, 74, 166, a->prompt, NAV, PAPER, LV_TEXT_ALIGN_LEFT);
        } else
            center(74, a->prompt, NAV, PAPER);
        snprintf(text, sizeof text, "第 %u/%u 页", a->page + 1, a->pages);
        center(109, text, TEAL, PAPER);
        for (int j = 0; j < a->options; j++)
            label(12, 144 + 42 * j, 216, a->labels[j], j == a->focus ? PAPER : NAV,
                  j == a->focus ? NAV : 0xe5f0e9, LV_TEXT_ALIGN_LEFT);
        foot = whale_text_wide(a->labels[a->focus]) ? "确认展开全文" : "上下选择  确认提交";
    } else if (a->view == OPTION || a->view == PROMPT) {
        fulltext(a->view == PROMPT ? "问题全文" : "选项全文",
                 a->view == PROMPT ? a->prompt : a->labels[a->focus],
                 a->scroll);
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
            heading = "保留当前绑定";
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
            heading = "固件 R11.4";
            detail = "BLE 2.2";
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
    if (!painted_pose)
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    for (unsigned i = 0; i < sizeof panels / sizeof panels[0]; i++)
        if (!(visible_panels & (1u << i)))
            lv_obj_add_flag(*panels[i], LV_OBJ_FLAG_HIDDEN);
    if (a->view != HOME && !inline_reply)
        center(292, foot, MUT, PAPER);
    for (unsigned i = used; i < LABEL_COUNT; i++)
        lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
}
