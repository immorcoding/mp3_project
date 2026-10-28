/**
  ******************************************************************************
  * @file    prototype_artist_layout.c
  * @brief   PROTOTYPE（#152，throwaway，不合入）：唱盘页歌手行的几种版式。
  *
  * @details
  *          只在 prototype/vinyl-artist-layout 分支存在。不改 Service_GUI_TitleApply：
  *          每圈在 gui_music_step() 之后看曲名 Label 文本是否变化，变化时按名称槽替身
  *          最后一次应答的 Artist 写歌手 Label（曲名为空白或 `--` 时歌手写空串），
  *          近似“曲名与歌手同一次 Apply”。版式由 `--artist-layout N` 选择：
  *          0 = 不加歌手（现状），1 = A，2 = B，3 = C（第一轮）；4 = B + 唱盘 124 px，
  *          5 = B + 唱盘 132 px（第二轮：唱盘用 lv_img zoom 缩放，纵向按统一节奏重排，
  *          歌手为空时显示 `Unknown Artist`），取值见 PROTOTYPE.md。
  ******************************************************************************
  */

#include "prototype_artist_layout.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "APP/tasks/storage/catalog/storage_namebuffer.h"
#include "Service/gui/font/gui_service_font.h"
#include "Service/gui/gui_service_config.h"
#include "Service/gui/theme/gui_service_theme_style.h"
#include "Service/gui/view/gui_service_view.h"
#include "lvgl.h"

typedef struct
{
    const char *name;
    lv_coord_t vinyl_y;        /* 唱盘 Image Y（相对 Playing 内容区）；现状 4。 */
    lv_coord_t title_y;        /* 有歌手时曲名 Y；现状 160。 */
    lv_coord_t title_y_alone;  /* 无歌手时曲名 Y；等于 title_y 表示留空不动。 */
    lv_coord_t artist_y;       /* 歌手 Y。 */
    lv_coord_t artist_width;   /* 歌手可见宽度。 */
    uint8_t role;              /* Service_GUI_ThemeRoleTypeDef 调色板角色。 */
    lv_opa_t opa;              /* 歌手 text_opa。 */
    bool scroll;               /* true：超宽循环滚动（同曲名）；false：LV_LABEL_LONG_DOT 省略号。 */
    lv_coord_t controls_shift; /* 进度条与时间 translate_y（三键不动）。 */
} proto_layout_t;

static const proto_layout_t proto_layouts[] = {
    {"current", 4, 160, 160, 0, 0, SERVICE_GUI_THEME_INK, LV_OPA_COVER, false, 0},
    /* A：曲名下方；唱盘上移 4、曲名上移 15，歌手占原曲名位；Ink 180（同 Queue 歌手）；滚动；无歌手留空。 */
    {"A", 0, 145, 145, 160, 160, SERVICE_GUI_THEME_INK, 180, true, 0},
    /* B：曲名上方；唱盘上移 4，曲名不动，歌手插在唱盘与曲名之间；Ink 140；宽 180 省略号；无歌手留空（即现状）。 */
    {"B", 0, 160, 160, 145, 180, SERVICE_GUI_THEME_INK, 140, false, 0},
    /* C：曲名下方；唱盘不动，进度条与时间下移 6（三键不动）；Accent 200；滚动；无歌手时曲名居中。 */
    {"C", 4, 150, 157, 165, 160, SERVICE_GUI_THEME_ACCENT, 200, true, 6},
};

#define PROTO_LAYOUT_COUNT ((int)(sizeof(proto_layouts) / sizeof(proto_layouts[0])))

/* 第二轮（版式 B 的两档唱盘）：各元素顶边的 LCD 绝对 Y；间距推算见 PROTOTYPE.md。 */
typedef struct
{
    const char *name;
    lv_coord_t disc;     /* 唱盘目标直径。 */
    uint16_t zoom;       /* lv_img zoom，256 = 1 倍；round(disc * 256 / 144)。 */
    lv_coord_t disc_top; /* 唱盘可见上沿。 */
    lv_coord_t artist_top;
    lv_coord_t title_top;
    lv_coord_t slider_top;
    lv_coord_t time_top;
    lv_coord_t buttons_top;
} proto_rhythm_t;

#define PROTO_RHYTHM_FIRST   (4)
#define PROTO_UNKNOWN_ARTIST "Unknown Artist"

static const proto_rhythm_t proto_rhythms[] = {
    /* 4：唱盘 124；间距 5/5/2/5/3/5/4（目标 8/8/2/8/4/8/6–8 按比例压缩）。 */
    {"B-124", 124, 220, 44, 173, 193, 216, 224, 240},
    /* 5：唱盘 132；间距 4/4/1/4/2/3/3。 */
    {"B-132", 132, 235, 43, 179, 198, 220, 227, 241},
};

#define PROTO_RHYTHM_COUNT ((int)(sizeof(proto_rhythms) / sizeof(proto_rhythms[0])))

static const proto_layout_t proto_layout_rhythm =
    {"B2", 0, 0, 0, 0, 180, SERVICE_GUI_THEME_INK, 180, false, 0};
static bool proto_unknown_artist;

static const proto_layout_t *proto_layout;
static lv_obj_t *proto_artist_label;
static char proto_last_title[STORAGE_CATALOG_NAME_BYTES + 4];

/**
 * @brief 打印对象的 LCD 绝对坐标，供 PROTOTYPE.md 记录取值。
 * @param[in] what 名称。
 * @param[in] obj LVGL 对象。
 */
static void proto_dump(const char *what, lv_obj_t *obj)
{
    lv_area_t a;

    lv_obj_update_layout(obj);
    lv_obj_get_coords(obj, &a);
    printf("[proto] %-10s x=%d..%d y=%d..%d (h=%d)\n", what, a.x1, a.x2, a.y1, a.y2, a.y2 - a.y1 + 1);
}

/**
 * @brief 把对象（含现有 translate）整体平移到 LCD 顶边 top。
 * @param[in] obj LVGL 对象。
 * @param[in] top 目标顶边的 LCD 绝对 Y。
 */
static void proto_move_top(lv_obj_t *obj, lv_coord_t top)
{
    lv_area_t a;

    lv_obj_update_layout(obj);
    lv_obj_get_coords(obj, &a);
    lv_obj_set_style_translate_y(obj, lv_obj_get_style_translate_y(obj, LV_PART_MAIN) + (top - a.y1),
                                 LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief 第二轮：版式 B，唱盘用 zoom 缩到 r->disc，纵向按 r 的 LCD 顶边重排。
 * @param[in] r 档位。
 * @note  唱盘 Image 仍是 144×144（VIRTUAL 尺寸模式），只设 zoom；底图与每帧旋转不变。
 *        Image 盒子上沿因此可能高出内容区，Playing 页清掉 SCROLLABLE，避免出现纵向滚动余量。
 */
static void proto_rhythm_init(const proto_rhythm_t *r)
{
    const Service_GUI_ViewMusicTypeDef *music = &service_gui_view_get()->music;
    const lv_font_t *font = service_gui_font_title();
    lv_obj_t *tab = lv_obj_get_parent(music->title_label);
    lv_area_t tab_area;
    lv_coord_t box = lv_obj_get_height(music->vinyl_image);

    proto_layout = &proto_layout_rhythm;
    proto_unknown_artist = true;
    printf("[proto] artist layout %s, disc %d zoom %u\n", r->name, r->disc, r->zoom);

    lv_obj_update_layout(tab);
    lv_obj_get_coords(tab, &tab_area);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_img_set_zoom(music->vinyl_image, r->zoom);
    lv_obj_set_y(music->vinyl_image, (r->disc_top - tab_area.y1) - ((box - r->disc) / 2));
    lv_obj_set_y(music->title_label, r->title_top - tab_area.y1);

    proto_artist_label = lv_label_create(tab);
    lv_obj_set_size(proto_artist_label, proto_layout->artist_width, font->line_height + 2);
    lv_obj_set_pos(proto_artist_label, 0, r->artist_top - tab_area.y1);
    lv_obj_set_align(proto_artist_label, LV_ALIGN_TOP_MID);
    lv_obj_clear_flag(proto_artist_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_long_mode(proto_artist_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(proto_artist_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(proto_artist_label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(proto_artist_label, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(proto_artist_label, proto_layout->opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(proto_artist_label, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(proto_artist_label, "");

    proto_move_top(music->slider, r->slider_top);
    proto_move_top(music->time_label, r->time_top);
    proto_move_top(music->previous_button, r->buttons_top);
    proto_move_top(music->play_pause_button, r->buttons_top);
    proto_move_top(music->next_button, r->buttons_top);

    proto_dump("tab", tab);
    proto_dump("vinyl box", music->vinyl_image);
    proto_dump("artist", proto_artist_label);
    proto_dump("title", music->title_label);
    proto_dump("slider", music->slider);
    proto_dump("time", music->time_label);
    proto_dump("play", music->play_pause_button);
}

/**
 * @brief 按选定版式改唱盘、曲名与控制区的位置，并创建歌手 Label。
 * @param[in] layout 0..3；越界按 0。
 */
void proto_artist_layout_init(int layout)
{
    const Service_GUI_ViewMusicTypeDef *music = &service_gui_view_get()->music;
    const lv_font_t *font = service_gui_font_title();
    const proto_layout_t *l;

    if ((layout < 0) || (layout >= PROTO_RHYTHM_FIRST + PROTO_RHYTHM_COUNT))
    {
        layout = 0;
    }
    if (layout >= PROTO_RHYTHM_FIRST)
    {
        proto_rhythm_init(&proto_rhythms[layout - PROTO_RHYTHM_FIRST]);
        return;
    }
    if (layout >= PROTO_LAYOUT_COUNT)
    {
        layout = 0;
    }
    l = &proto_layouts[layout];
    proto_layout = l;
    printf("[proto] artist layout %d (%s), title font line_height=%d\n", layout, l->name, font->line_height);

    if (layout != 0)
    {
        lv_obj_set_y(music->vinyl_image, l->vinyl_y);
        lv_obj_set_y(music->title_label, l->title_y_alone);

        proto_artist_label = lv_label_create(lv_obj_get_parent(music->title_label));
        lv_obj_set_size(proto_artist_label, l->artist_width, font->line_height + 2);
        lv_obj_set_pos(proto_artist_label, 0, l->artist_y);
        lv_obj_set_align(proto_artist_label, LV_ALIGN_TOP_MID);
        lv_obj_clear_flag(proto_artist_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_label_set_long_mode(proto_artist_label, l->scroll ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(proto_artist_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(proto_artist_label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
        service_gui_theme_style_add(proto_artist_label, SERVICE_GUI_THEME_TEXT,
                                    (Service_GUI_ThemeRoleTypeDef)l->role, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(proto_artist_label, l->opa, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(proto_artist_label, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_anim_speed(proto_artist_label, SERVICE_GUI_MUSIC_TITLE_SPEED, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(proto_artist_label, "");

        if (l->controls_shift != 0)
        {
            lv_obj_set_style_translate_y(music->slider, l->controls_shift, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_translate_y(music->time_label, l->controls_shift, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    proto_dump("tab", lv_obj_get_parent(music->title_label));
    proto_dump("vinyl", music->vinyl_image);
    proto_dump("title", music->title_label);
    if (proto_artist_label != NULL)
    {
        proto_dump("artist", proto_artist_label);
    }
    proto_dump("slider", music->slider);
    proto_dump("time", music->time_label);
    proto_dump("play", music->play_pause_button);
}

/**
 * @brief 曲名文本变化时同步歌手：名字取名称槽替身最后一次应答的 Artist，空白与 `--` 写空串。
 * @note  每圈在 gui_music_step() 之后调用；采信与 Apply 在同一圈，名称槽的 Artist 仍是该应答。
 */
void proto_artist_layout_step(void)
{
    const Service_GUI_ViewMusicTypeDef *music = &service_gui_view_get()->music;
    const char *title;
    const char *artist;
    bool has_artist;

    if (proto_artist_label == NULL)
    {
        return;
    }

    title = lv_label_get_text(music->title_label);
    if (strcmp(title, proto_last_title) == 0)
    {
        return;
    }
    (void)snprintf(proto_last_title, sizeof(proto_last_title), "%s", title);

    artist = ((title[0] == '\0') || (strcmp(title, "--") == 0)) ? "" : storage_namebuffer.Artist;
    if (proto_unknown_artist && (artist[0] == '\0') && (title[0] != '\0') && (strcmp(title, "--") != 0))
    {
        artist = PROTO_UNKNOWN_ARTIST; /* 第二轮：有名字但歌手为空串时显示界面文案，样式同歌手。 */
    }
    has_artist = (artist[0] != '\0');

    if (proto_layout->scroll)
    {
        lv_coord_t width = lv_txt_get_width(artist, (uint32_t)strlen(artist),
                                            lv_obj_get_style_text_font(proto_artist_label, LV_PART_MAIN),
                                            0, LV_TEXT_FLAG_NONE);
        lv_label_set_long_mode(proto_artist_label,
                               (width > proto_layout->artist_width) ? LV_LABEL_LONG_SCROLL_CIRCULAR
                                                                    : LV_LABEL_LONG_CLIP);
    }
    lv_label_set_text(proto_artist_label, artist);

    /* 曲名为空白（应答未到）时不挪动曲名，避免切歌时跳两次；只有名字或 `--` 才决定位置。 */
    if ((title[0] != '\0') && !proto_unknown_artist) /* 第二轮版式固定，曲名不挪。 */
    {
        lv_obj_set_y(music->title_label, has_artist ? proto_layout->title_y : proto_layout->title_y_alone);
    }
}

/* ---- 帧开销：只在 --artist-perf START:END 给出时统计 ---- */

extern uint32_t proto_sim_flush_pixels; /* sim_display.c */

static uint32_t proto_perf_start_ms;
static uint32_t proto_perf_end_ms;
static bool proto_perf_on;
static Uint64 proto_perf_t0;
static uint32_t proto_perf_px0;
static uint32_t proto_perf_frames;
static uint64_t proto_perf_sum_ns;
static uint64_t proto_perf_max_ns;
static uint64_t proto_perf_px;

/**
 * @brief 解析统计窗口 START:END（脚本时间，毫秒）。
 * @param[in] spec 参数文本。
 */
void proto_artist_perf_window(const char *spec)
{
    char *end = NULL;

    proto_perf_start_ms = (uint32_t)strtoul(spec, &end, 10);
    proto_perf_end_ms = ((end != NULL) && (*end == ':')) ? (uint32_t)strtoul(end + 1, NULL, 10) : 0U;
    proto_perf_on = (proto_perf_end_ms > proto_perf_start_ms);
}

/** @brief Service_GUI_Process() 之前调用：记下计时起点与已写像素数。 */
void proto_artist_perf_begin(void)
{
    proto_perf_t0 = SDL_GetPerformanceCounter();
    proto_perf_px0 = proto_sim_flush_pixels;
}

/**
 * @brief Service_GUI_Process() 之后调用：本圈有像素写出（即渲染了一帧）且在窗口内时计入。
 * @param[in] script_ms 脚本时间。
 */
void proto_artist_perf_end(uint32_t script_ms)
{
    const Uint64 t1 = SDL_GetPerformanceCounter();
    uint64_t ns;

    if (!proto_perf_on || (script_ms < proto_perf_start_ms) || (script_ms >= proto_perf_end_ms) ||
        (proto_sim_flush_pixels == proto_perf_px0))
    {
        return;
    }
    ns = (uint64_t)((t1 - proto_perf_t0) * 1000000000ULL / SDL_GetPerformanceFrequency());
    proto_perf_frames++;
    proto_perf_sum_ns += ns;
    proto_perf_px += (uint64_t)(proto_sim_flush_pixels - proto_perf_px0);
    if (ns > proto_perf_max_ns)
    {
        proto_perf_max_ns = ns;
    }
}

/** @brief 退出前打印统计：帧数、平均与最大渲染时间（PC 墙钟）、平均每帧写出像素。 */
void proto_artist_perf_report(void)
{
    if (!proto_perf_on || (proto_perf_frames == 0U))
    {
        return;
    }
    printf("[perf] frames=%lu mean_us=%.1f max_us=%.1f px_per_frame=%lu\n",
           (unsigned long)proto_perf_frames,
           (double)proto_perf_sum_ns / (double)proto_perf_frames / 1000.0,
           (double)proto_perf_max_ns / 1000.0,
           (unsigned long)(proto_perf_px / proto_perf_frames));
}
