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
  *          0 = 不加歌手（现状），1 = A，2 = B，3 = C，取值见 PROTOTYPE.md。
  ******************************************************************************
  */

#include "prototype_artist_layout.h"

#include <stdio.h>
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
 * @brief 按选定版式改唱盘、曲名与控制区的位置，并创建歌手 Label。
 * @param[in] layout 0..3；越界按 0。
 */
void proto_artist_layout_init(int layout)
{
    const Service_GUI_ViewMusicTypeDef *music = &service_gui_view_get()->music;
    const lv_font_t *font = service_gui_font_title();
    const proto_layout_t *l;

    if ((layout < 0) || (layout >= PROTO_LAYOUT_COUNT))
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
    if (title[0] != '\0')
    {
        lv_obj_set_y(music->title_label, has_artist ? proto_layout->title_y : proto_layout->title_y_alone);
    }
}
