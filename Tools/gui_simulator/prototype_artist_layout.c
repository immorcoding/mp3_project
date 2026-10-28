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
  *          6 = 第三轮：#146 布局需求七条，整页由 proto_r3_cfg 一份像素常量推出
  *          （曲名在上、歌手在下，四组等距 S，按 ink 计量）；`--artist-hide 1|2` 只供测量。
  *          7 = 第四轮：在 6 上把文字组 → 进度组收紧为 4，腾出的 4 px 给唱盘，Label 高 16。
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

#define PROTO_R3_LAYOUT (6)
#define PROTO_R4_LAYOUT (7)
static void proto_r3_init_layout(int layout);
static void proto_r3_step(void);

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

    if ((layout == PROTO_R3_LAYOUT) || (layout == PROTO_R4_LAYOUT))
    {
        proto_r3_init_layout(layout);
        return;
    }
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

    proto_r3_step();
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

/* ---- 第三轮（--artist-layout 6）：整页一份像素常量配置（#146 布局需求七条） ----
 *
 * 只有下面这组常量是“手填”的；各元素的 Y、唱盘直径 D 与组间距 S 全部由它们、
 * 字体度量（ink 带）和 Playing 内容区高度 H 推出（proto_r3_solve）。
 * 纵向分四组：唱盘 / 曲名+歌手 / 进度条+时间 / 三键，五段组间距都等于 S：
 *   标签栏→唱盘、唱盘→文字组、文字组→进度组、进度组→三键、三键→页底。
 * 一律按 ink 计量：文字按字体 ink 带（参考字形集合的可见上下沿），不按 Label 盒子。 */

typedef struct
{
    lv_coord_t grid;          /* 间距网格。 */
    lv_coord_t s_max;         /* 组间距的首选上限；不足时按 grid 递减。 */
    lv_coord_t s_min;         /* 组间距下限。 */
    lv_coord_t disc_min;      /* 唱盘可见直径下限。 */
    lv_coord_t disc_src;      /* 资源包唱盘底图边长（= SERVICE_GUI_MUSIC_VINYL_DIAMETER）。 */
    lv_coord_t text_gap;      /* 曲名 ink 下沿 → 歌手 ink 上沿。 */
    lv_coord_t progress_gap;  /* 进度条下沿 → 时间 ink 上沿。 */
    lv_coord_t column_w;      /* 横向内容列宽：进度条、曲名窗、歌手窗、三键外沿。 */
    lv_coord_t slider_h;      /* 进度条高度（含 1 px 边）。 */
    lv_coord_t side_btn;      /* 侧键直径。 */
    lv_coord_t main_btn;      /* 主键直径。 */
    lv_coord_t text_progress_gap; /* 文字组 → 进度组；0 = 等于 S（layout 6），layout 7 为独立常量 4。 */
    lv_coord_t label_pad;     /* 曲名、歌手 Label 高 = line_height + label_pad（6：2，7：0）。 */
} proto_r3_cfg_t;

/* [0] = layout 6（第三轮）；[1] = layout 7（第四轮：文字组 → 进度组收紧为 4，腾出的 4 px 给唱盘，Label 高 16）。 */
static const proto_r3_cfg_t proto_r3_cfgs[] = {
  {
    .grid = 4,
    .s_max = 12,
    .s_min = 4,
    .disc_min = 112,
    .disc_src = 144,
    .text_gap = 4,
    .progress_gap = 4,
    .column_w = 192,
    .slider_h = 4,
    .side_btn = 36,
    .main_btn = 44,
    .text_progress_gap = 0,
    .label_pad = 2,
  },
  {
    .grid = 4,
    .s_max = 12,
    .s_min = 4,
    .disc_min = 112,
    .disc_src = 144,
    .text_gap = 4,
    .progress_gap = 4,
    .column_w = 192,
    .slider_h = 4,
    .side_btn = 36,
    .main_btn = 44,
    .text_progress_gap = 4,
    .label_pad = 0,
  },
};

static const proto_r3_cfg_t *proto_r3_c = &proto_r3_cfgs[0];

/* 字体 ink 带的参考字形：曲名/歌手覆盖拉丁大小写、数字与常见 CJK；时间覆盖数字与分隔符。 */
#define PROTO_R3_TEXT_INK_REF "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" \
                              "晴天周杰伦国的一是了我不人在他有这中大来上"
#define PROTO_R3_TIME_INK_REF "0123456789:/-"

/** @brief 字体 ink 带：相对 Label 内容区顶边的可见上沿与高度。 */
typedef struct
{
    lv_coord_t top; /* 第一行着色像素相对 Label 顶边的偏移。 */
    lv_coord_t h;   /* 着色行数。 */
} proto_ink_t;

/** @brief 推出的版面（Y 都是相对 Playing 内容区顶边，ink 口径）。 */
typedef struct
{
    lv_coord_t h;          /* 内容区高度。 */
    lv_coord_t s;          /* 组间距。 */
    lv_coord_t disc;       /* 唱盘可见直径。 */
    uint16_t zoom;         /* lv_img zoom。 */
    lv_coord_t disc_top;
    lv_coord_t title_ink_top;
    lv_coord_t artist_ink_top;
    lv_coord_t slider_top;
    lv_coord_t time_ink_top;
    lv_coord_t btn_top;    /* 主键上沿；三键中线 = btn_top + (main_btn - 1) / 2。 */
    lv_coord_t col_x;      /* 内容列左沿（相对内容区）。 */
    lv_coord_t slack;      /* D 取偶数后余下的行，加给进度条（见 proto_r3_solve）。 */
} proto_r3_layout_t;

static bool proto_r3_active;
static lv_obj_t *proto_r3_artist;
static lv_obj_t *proto_r3_holder;
static char proto_r3_last_title[STORAGE_CATALOG_NAME_BYTES + 4];
static char proto_r3_last_icon[8]; /* 主键图标字形（UTF-8 副本）。 */
static proto_r3_layout_t proto_r3;
uint32_t proto_artist_hide; /* --artist-hide：1 = 隐去标签栏与 Playing 全部元素（测量用背景），2 = 只隐去三键图标。 */

/* 可见 ink 的 alpha 门限（0..255）：字形边缘 alpha 低于约 20% 的行在 RGB565 上几乎看不出，不计入 ink 带。 */
#define PROTO_R3_INK_ALPHA (48U)

/**
 * @brief 读字形位图 (row, col) 的 alpha，归一到 0..255（fmt_txt 未压缩位图，bpp 1/2/4/8）。
 * @param[in] font 字体（取 bpp）。
 * @param[in] bm 字形位图。
 * @param[in] g 字形描述。
 * @param[in] row 位图行。
 * @param[in] col 位图列。
 * @return alpha，0..255。
 */
static uint32_t proto_r3_alpha(const lv_font_t *font, const uint8_t *bm, const lv_font_glyph_dsc_t *g,
                               uint32_t row, uint32_t col)
{
    uint32_t bpp = ((const lv_font_fmt_txt_dsc_t *)font->dsc)->bpp;
    uint32_t bit = (row * g->box_w + col) * bpp;
    uint32_t v = (bm[bit >> 3] >> (8U - bpp - (bit & 7U))) & ((1U << bpp) - 1U);

    return v * 255U / ((1U << bpp) - 1U);
}

/**
 * @brief 由字形位图求一组参考字形的可见 ink 带。
 * @param[in] font 字体。
 * @param[in] ref UTF-8 参考字形。
 * @return ink 带：相对 Label 顶边的第一行与行数（LVGL 8 绘字：字形顶 = line_height - base_line - box_h - ofs_y）。
 * @note  只计入 alpha ≥ PROTO_R3_INK_ALPHA 的行。
 */
static proto_ink_t proto_r3_ink(const lv_font_t *font, const char *ref)
{
    proto_ink_t ink = {0, 0};
    lv_coord_t top = 10000;
    lv_coord_t bottom = -10000;
    uint32_t i = 0;

    while (ref[i] != '\0')
    {
        lv_font_glyph_dsc_t g;
        uint32_t letter = _lv_txt_encoded_next(ref, &i);
        const uint8_t *bm;
        uint32_t row;
        uint32_t col;

        if (!lv_font_get_glyph_dsc(font, &g, letter, 0) || (g.box_h == 0) ||
            ((bm = lv_font_get_glyph_bitmap(font, letter)) == NULL))
        {
            continue;
        }
        for (row = 0; row < g.box_h; row++)
        {
            for (col = 0; col < g.box_w; col++)
            {
                if (proto_r3_alpha(font, bm, &g, row, col) >= PROTO_R3_INK_ALPHA)
                {
                    lv_coord_t y = (lv_coord_t)(font->line_height - font->base_line - g.box_h - g.ofs_y + (lv_coord_t)row);

                    top = LV_MIN(top, y);
                    bottom = LV_MAX(bottom, y);
                    break;
                }
            }
        }
    }
    if (bottom >= top)
    {
        ink.top = top;
        ink.h = (lv_coord_t)(bottom - top + 1);
    }
    return ink;
}

/**
 * @brief 推出整页纵向版面。
 * @param[in] c 常量配置。
 * @param[in] text 曲名/歌手字体 ink 带。
 * @param[in] time 时间字体 ink 带。
 * @param[in] h 内容区高度。
 * @param[in] w 内容区宽度。
 * @return 版面。
 * @note  4S + G + D + (2·text.h + text_gap) + (slider_h + progress_gap + time.h) + main_btn = H，
 *        G = 文字组 → 进度组（text_progress_gap，为 0 时等于 S）。
 *        S 从 s_max 起按 grid 递减，取第一个使 D ≥ disc_min 的值。D 取偶数（唱盘绕中心缩放，
 *        奇数直径无法在像素上对称），余下的 1 行加给进度条高度。
 */
static proto_r3_layout_t proto_r3_solve(const proto_r3_cfg_t *c, proto_ink_t text, proto_ink_t time,
                                        lv_coord_t h, lv_coord_t w)
{
    proto_r3_layout_t l;
    lv_coord_t fixed = (lv_coord_t)(2 * text.h + c->text_gap + c->slider_h + c->progress_gap + time.h + c->main_btn);
    lv_coord_t g;

    memset(&l, 0, sizeof(l));
    l.h = h;
    for (l.s = c->s_max; l.s >= c->s_min; l.s = (lv_coord_t)(l.s - c->grid))
    {
        g = (c->text_progress_gap > 0) ? c->text_progress_gap : l.s;
        l.disc = (lv_coord_t)(h - 4 * l.s - g - fixed);
        if (l.disc >= c->disc_min)
        {
            break;
        }
    }
    l.slack = (lv_coord_t)(l.disc & 1);
    l.disc = (lv_coord_t)(l.disc - l.slack);
    l.zoom = (uint16_t)((l.disc * 256 + c->disc_src - 1) / c->disc_src); /* 向上取整，让可见盘面铺满 D。 */
    l.disc_top = l.s;
    l.title_ink_top = (lv_coord_t)(l.disc_top + l.disc + l.s);
    l.artist_ink_top = (lv_coord_t)(l.title_ink_top + text.h + c->text_gap);
    l.slider_top = (lv_coord_t)(l.artist_ink_top + text.h + ((c->text_progress_gap > 0) ? c->text_progress_gap : l.s));
    l.time_ink_top = (lv_coord_t)(l.slider_top + c->slider_h + l.slack + c->progress_gap);
    l.btn_top = (lv_coord_t)(l.time_ink_top + time.h + l.s);
    l.col_x = (lv_coord_t)((w - c->column_w) / 2);
    return l;
}

/**
 * @brief 图标光学居中：把字形 alpha 加权质心平移到圆心。
 * @param[in] icon 图标 Label（父对象为圆键）。
 * @note  质心偏差向 0 取整（不超调）：对称字形（暂停、上一首、下一首）质心偏差 < 1 px，不动；
 *        播放三角形的质心偏左约 1.5 px，右移 1 px。残差与包围盒偏差见日志与 PROTOTYPE.md。
 */
static void proto_r3_center_icon(lv_obj_t *icon)
{
    const lv_font_t *font = lv_obj_get_style_text_font(icon, LV_PART_MAIN);
    const char *text = lv_label_get_text(icon);
    lv_obj_t *btn = lv_obj_get_parent(icon);
    lv_font_glyph_dsc_t g;
    const uint8_t *bm;
    lv_area_t la;
    lv_area_t ba;
    uint32_t i = 0;
    uint32_t letter = _lv_txt_encoded_next(text, &i);
    uint32_t row;
    uint32_t col;
    double sw = 0.0;
    double sx = 0.0;
    double sy = 0.0;
    double gx;
    double gy;
    lv_coord_t top;
    lv_coord_t dx;
    lv_coord_t dy;

    lv_obj_set_style_translate_x(icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_translate_y(icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_update_layout(btn);
    lv_obj_get_coords(icon, &la);
    lv_obj_get_coords(btn, &ba);
    if (!lv_font_get_glyph_dsc(font, &g, letter, 0) || ((bm = lv_font_get_glyph_bitmap(font, letter)) == NULL))
    {
        return;
    }
    top = (lv_coord_t)(la.y1 + font->line_height - font->base_line - g.box_h - g.ofs_y);
    for (row = 0; row < g.box_h; row++)
    {
        for (col = 0; col < g.box_w; col++)
        {
            double w = (double)proto_r3_alpha(font, bm, &g, row, col);

            sw += w;
            sx += w * (double)(la.x1 + g.ofs_x + (lv_coord_t)col);
            sy += w * (double)(top + (lv_coord_t)row);
        }
    }
    if (sw <= 0.0)
    {
        return;
    }
    gx = (ba.x1 + ba.x2) / 2.0 - sx / sw; /* 圆心 - 质心 */
    gy = (ba.y1 + ba.y2) / 2.0 - sy / sw;
    dx = (lv_coord_t)gx; /* 向 0 取整 */
    dy = (lv_coord_t)gy;
    lv_obj_set_style_translate_x(icon, dx, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_translate_y(icon, dy, LV_PART_MAIN | LV_STATE_DEFAULT);
    printf("[proto] icon U+%04lX box %dx%d: centroid off centre by (%+.2f,%+.2f) -> translate %d,%d; "
           "bbox centre off by (%+.1f,%+.1f)\n",
           (unsigned long)letter, g.box_w, g.box_h, -gx, -gy, dx, dy,
           (la.x1 + g.ofs_x + dx) + (g.box_w - 1) / 2.0 - (ba.x1 + ba.x2) / 2.0,
           (top + dy) + (g.box_h - 1) / 2.0 - (ba.y1 + ba.y2) / 2.0);
}

/**
 * @brief 把对象放到内容区坐标 (x, y)，左上对齐，清掉旧 translate。
 * @param[in] obj LVGL 对象。
 * @param[in] x 相对父内容区的 X。
 * @param[in] y 相对父内容区的 Y。
 */
static void proto_r3_place(lv_obj_t *obj, lv_coord_t x, lv_coord_t y)
{
    lv_obj_set_align(obj, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_translate_x(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_translate_y(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief 测量用：把对象设为完全透明（不改布局）。
 * @param[in] obj LVGL 对象。
 */
static void proto_r3_hide(lv_obj_t *obj)
{
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief 第三轮 / 第四轮：按常量配置推出版面并重排 Now Playing。
 * @param[in] c 常量配置（layout 6 或 7）。
 */
static void proto_r3_init(const proto_r3_cfg_t *c)
{
    const Service_GUI_ViewMusicTypeDef *music = &service_gui_view_get()->music;
    const lv_font_t *font = service_gui_font_title();
    lv_obj_t *tab = lv_obj_get_parent(music->title_label);
    lv_obj_t *controls = lv_obj_get_parent(music->slider);
    proto_ink_t text_ink;
    proto_ink_t time_ink;
    lv_coord_t tab_w;
    lv_coord_t tab_h;
    lv_coord_t side_dy;
    lv_area_t a;

    proto_r3_active = true;
    proto_r3_c = c;
    lv_obj_update_layout(tab);
    tab_w = lv_obj_get_content_width(tab);
    tab_h = lv_obj_get_content_height(tab);
    text_ink = proto_r3_ink(font, PROTO_R3_TEXT_INK_REF);
    time_ink = proto_r3_ink(lv_obj_get_style_text_font(music->time_label, LV_PART_MAIN), PROTO_R3_TIME_INK_REF);
    proto_r3 = proto_r3_solve(c, text_ink, time_ink, tab_h, tab_w);
    lv_obj_get_coords(tab, &a);

    printf("[proto] r3 tab %dx%d at LCD (%d,%d); text font line_height %d base_line %d ink top %d h %d; "
           "time font line_height %d ink top %d h %d\n",
           tab_w, tab_h, a.x1, a.y1, font->line_height, font->base_line, text_ink.top, text_ink.h,
           lv_obj_get_style_text_font(music->time_label, LV_PART_MAIN)->line_height, time_ink.top, time_ink.h);
    printf("[proto] r3 S %d disc %d (slack %d) zoom %u; ink tops: disc %d title %d artist %d slider %d time %d btn %d; "
           "btn mid2 %d; col x %d\n",
           proto_r3.s, proto_r3.disc, proto_r3.slack, proto_r3.zoom, proto_r3.disc_top, proto_r3.title_ink_top,
           proto_r3.artist_ink_top, proto_r3.slider_top, proto_r3.time_ink_top, proto_r3.btn_top,
           2 * proto_r3.btn_top + c->main_btn - 1, proto_r3.col_x);

    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    /* 唱盘：D×D 的透明容器裁切 144 的 Image，盒子不再撑高内容区；Image 仍以中心为轴缩放与旋转。 */
    proto_r3_holder = lv_obj_create(tab);
    lv_obj_remove_style_all(proto_r3_holder);
    lv_obj_clear_flag(proto_r3_holder, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(proto_r3_holder, proto_r3.disc, proto_r3.disc);
    proto_r3_place(proto_r3_holder, (lv_coord_t)((tab_w - proto_r3.disc) / 2), proto_r3.disc_top);
    lv_obj_move_to_index(proto_r3_holder, 0);
    lv_obj_set_parent(music->vinyl_image, proto_r3_holder);
    lv_obj_set_align(music->vinyl_image, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(music->vinyl_image, (lv_coord_t)(-(c->disc_src - proto_r3.disc) / 2),
                   (lv_coord_t)(-(c->disc_src - proto_r3.disc) / 2));
    lv_img_set_zoom(music->vinyl_image, proto_r3.zoom);

    /* 曲名与歌手：同宽 column_w，Label 盒子顶 = ink 顶 - ink 带偏移。 */
    lv_obj_set_size(music->title_label, c->column_w, (lv_coord_t)(font->line_height + c->label_pad));
    proto_r3_place(music->title_label, proto_r3.col_x, (lv_coord_t)(proto_r3.title_ink_top - text_ink.top));

    proto_r3_artist = lv_label_create(tab);
    lv_obj_set_size(proto_r3_artist, c->column_w, (lv_coord_t)(font->line_height + c->label_pad));
    proto_r3_place(proto_r3_artist, proto_r3.col_x, (lv_coord_t)(proto_r3.artist_ink_top - text_ink.top));
    lv_obj_clear_flag(proto_r3_artist, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_long_mode(proto_r3_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(proto_r3_artist, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(proto_r3_artist, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    service_gui_theme_style_add(proto_r3_artist, SERVICE_GUI_THEME_TEXT, SERVICE_GUI_THEME_INK,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(proto_r3_artist, 180, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(proto_r3_artist, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(proto_r3_artist, "");

    /* 控制区容器铺满内容区、无内边距，子对象坐标即内容区坐标；不可点，不挡下层。 */
    lv_obj_set_size(controls, tab_w, tab_h);
    proto_r3_place(controls, 0, 0);
    lv_obj_set_style_pad_all(controls, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_size(music->slider, c->column_w, (lv_coord_t)(c->slider_h + proto_r3.slack));
    proto_r3_place(music->slider, proto_r3.col_x, proto_r3.slider_top);

    /* 时间：内容宽，水平居中；盒子顶 = ink 顶 - ink 带偏移。 */
    lv_obj_set_align(music->time_label, LV_ALIGN_TOP_MID);
    lv_obj_set_pos(music->time_label, 0, (lv_coord_t)(proto_r3.time_ink_top - time_ink.top));

    /* 三键：固定像素坐标，共用中线；侧键比主键各缩进 (main - side) / 2。 */
    side_dy = (lv_coord_t)((c->main_btn - c->side_btn) / 2);
    proto_r3_place(music->previous_button, proto_r3.col_x, (lv_coord_t)(proto_r3.btn_top + side_dy));
    lv_obj_set_size(music->previous_button, c->side_btn, c->side_btn);
    proto_r3_place(music->play_pause_button, (lv_coord_t)((tab_w - c->main_btn) / 2), proto_r3.btn_top);
    lv_obj_set_size(music->play_pause_button, c->main_btn, c->main_btn);
    proto_r3_place(music->next_button, (lv_coord_t)(proto_r3.col_x + c->column_w - c->side_btn),
                   (lv_coord_t)(proto_r3.btn_top + side_dy));
    lv_obj_set_size(music->next_button, c->side_btn, c->side_btn);

    lv_obj_set_pos(lv_obj_get_child(music->previous_button, 0), 0, 0);
    lv_obj_set_pos(lv_obj_get_child(music->next_button, 0), 0, 0);
    lv_obj_set_pos(music->play_pause_icon, 0, 0);
    proto_r3_center_icon(lv_obj_get_child(music->previous_button, 0));
    proto_r3_center_icon(lv_obj_get_child(music->next_button, 0));
    proto_r3_center_icon(music->play_pause_icon);
    (void)snprintf(proto_r3_last_icon, sizeof(proto_r3_last_icon), "%s", lv_label_get_text(music->play_pause_icon));

    if ((proto_artist_hide & 1U) != 0U)
    {
        proto_r3_hide(lv_tabview_get_tab_btns(lv_obj_get_parent(lv_obj_get_parent(tab))));
        proto_r3_hide(tab);
    }
    if ((proto_artist_hide & 2U) != 0U)
    {
        proto_r3_hide(lv_obj_get_child(music->previous_button, 0));
        proto_r3_hide(lv_obj_get_child(music->next_button, 0));
        proto_r3_hide(music->play_pause_icon);
    }

    proto_dump("tab", tab);
    proto_dump("holder", proto_r3_holder);
    proto_dump("vinyl box", music->vinyl_image);
    proto_dump("title", music->title_label);
    proto_dump("artist", proto_r3_artist);
    proto_dump("slider", music->slider);
    proto_dump("time", music->time_label);
    proto_dump("prev", music->previous_button);
    proto_dump("play", music->play_pause_button);
    proto_dump("next", music->next_button);
}

/**
 * @brief 按 layout 6 / 7 选常量配置并重排。
 * @param[in] layout PROTO_R3_LAYOUT 或 PROTO_R4_LAYOUT。
 */
static void proto_r3_init_layout(int layout)
{
    proto_r3_init(&proto_r3_cfgs[(layout == PROTO_R4_LAYOUT) ? 1 : 0]);
}

/**
 * @brief 第三轮每圈：曲名变化时写歌手、按 column_w 重定曲名 Long mode；主键图标换字形时重新居中。
 */
static void proto_r3_step(void)
{
    const Service_GUI_ViewMusicTypeDef *music = &service_gui_view_get()->music;
    const char *title;
    const char *artist;
    const char *icon;
    lv_label_long_mode_t mode;
    lv_coord_t width;

    if (!proto_r3_active)
    {
        return;
    }

    icon = lv_label_get_text(music->play_pause_icon);
    if (strcmp(icon, proto_r3_last_icon) != 0)
    {
        proto_r3_center_icon(music->play_pause_icon);
        (void)snprintf(proto_r3_last_icon, sizeof(proto_r3_last_icon), "%s", icon);
    }

    title = lv_label_get_text(music->title_label);
    if (strcmp(title, proto_r3_last_title) == 0)
    {
        return;
    }
    (void)snprintf(proto_r3_last_title, sizeof(proto_r3_last_title), "%s", title);

    /* main/title 按 160 判定滚动；原型的曲名窗是 column_w，按它重判（只在不同时改，避免重启动画）。 */
    width = lv_txt_get_width(title, (uint32_t)strlen(title), lv_obj_get_style_text_font(music->title_label, LV_PART_MAIN),
                             0, LV_TEXT_FLAG_NONE);
    mode = (width > proto_r3_c->column_w) ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP;
    if (lv_label_get_long_mode(music->title_label) != mode)
    {
        lv_label_set_long_mode(music->title_label, mode);
    }

    if ((title[0] == '\0') || (strcmp(title, "--") == 0))
    {
        artist = "";
    }
    else
    {
        artist = (storage_namebuffer.Artist[0] != '\0') ? storage_namebuffer.Artist : PROTO_UNKNOWN_ARTIST;
    }
    lv_label_set_text(proto_r3_artist, artist);
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
