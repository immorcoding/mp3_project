/**
  ******************************************************************************
  * @file    gui_service_theme_apply.c
  * @brief   用 LVGL color_filter_dsc 映射占位 hex，并应用壁纸/Ground 副作用。
  *
  * @details
 *          SquareLine `ui_init()` 会换成 basic theme，因此必须在其后重新挂接。
 *          切外观只换调色板指针并 invalidate；过滤器只改 RGB，不改 Opa。
 *          Boot Arc 动画本轮不改；Solid 下 Boot/BootReveal 与 Lock/Main 一样关壁纸。
  ******************************************************************************
  */

#include "Service/gui/theme/gui_service_theme_apply.h"

#include "Service/gui/gui_service.h"
#include "Service/gui/theme/gui_service_theme.h"
#include "Service/gui/theme/gui_service_theme_config.h"

#include "Service/gui/view/gui_service_view.h"

static lv_theme_t service_gui_theme_instance;
static lv_style_t service_gui_theme_filter_style;
static lv_color_filter_dsc_t service_gui_theme_filter_dsc;
static bool service_gui_theme_filter_ready;

/**
 * @brief 判断绘制色是否等于某占位 hex 量化后的 lv_color_t。
 * @param[in] color 过滤器收到的已量化颜色。
 * @param[in] placeholder 24-bit 占位 hex。
 * @return 与 `lv_color_hex(placeholder)` 的 `.full` 相同则为 true。
 * @note RGB565 会丢低位，必须两边都走 `lv_color_hex`，不能拿 24-bit 字面量对 `.full`。
 */
static bool service_gui_theme_color_is_placeholder(lv_color_t color, uint32_t placeholder)
{
    return color.full == lv_color_hex(placeholder).full;
}

/**
 * @brief 给指定 selector 挂上共享颜色过滤器；已挂过则先卸再挂。
 * @param[in,out] obj 目标对象。
 * @param[in] selector part 与 state。
 */
static void service_gui_theme_bind_selector(lv_obj_t *obj, lv_style_selector_t selector)
{
    lv_obj_remove_style(obj, &service_gui_theme_filter_style, selector);
    lv_obj_add_style(obj, &service_gui_theme_filter_style, selector);
}

/**
 * @brief 给一个对象挂上共享颜色过滤器。
 * @param[in,out] obj 目标对象。
 * @note 只 `add_style` 共享 style，不新建 local style，避免 48KB LVGL 堆被每对象
 *       十几份 style 槽撑爆。SquareLine `remove_style_all` 之后可重复调用。
 *       DEFAULT 选择器在按下/选中时仍匹配；无需再为 PRESSED 另挂一份。
 */
static void service_gui_theme_bind_obj(lv_obj_t *obj)
{
    if ((obj == NULL) || (!service_gui_theme_filter_ready))
    {
        return;
    }

    service_gui_theme_bind_selector(obj, LV_PART_MAIN);

    if (lv_obj_has_class(obj, &lv_bar_class) ||
        lv_obj_check_type(obj, &lv_arc_class))
    {
        service_gui_theme_bind_selector(obj, LV_PART_INDICATOR);
        service_gui_theme_bind_selector(obj, LV_PART_KNOB);
    }

    if (lv_obj_check_type(obj, &lv_btnmatrix_class))
    {
        service_gui_theme_bind_selector(obj, LV_PART_ITEMS);
    }
}

/**
 * @brief 把五个占位色换成当前调色板；其它颜色原样返回。
 * @param[in] dsc 未使用的过滤器描述符。
 * @param[in] color 当前绘制色。
 * @param[in] opa 未使用；不得用它改 RGB。
 * @return 映射后的 RGB；非占位则返回 `color`。
 */
static lv_color_t service_gui_theme_filter_cb(
    const lv_color_filter_dsc_t *dsc,
    lv_color_t color,
    lv_opa_t opa)
{
    uint32_t placeholder;

    (void)dsc;
    (void)opa;

    if (service_gui_theme_color_is_placeholder(color, SERVICE_GUI_THEME_PLACEHOLDER_ACCENT))
    {
        placeholder = SERVICE_GUI_THEME_PLACEHOLDER_ACCENT;
    }
    else if (service_gui_theme_color_is_placeholder(color, SERVICE_GUI_THEME_PLACEHOLDER_INK))
    {
        placeholder = SERVICE_GUI_THEME_PLACEHOLDER_INK;
    }
    else if (service_gui_theme_color_is_placeholder(color, SERVICE_GUI_THEME_PLACEHOLDER_MUTED))
    {
        placeholder = SERVICE_GUI_THEME_PLACEHOLDER_MUTED;
    }
    else if (service_gui_theme_color_is_placeholder(color, SERVICE_GUI_THEME_PLACEHOLDER_WASH))
    {
        placeholder = SERVICE_GUI_THEME_PLACEHOLDER_WASH;
    }
    else if (service_gui_theme_color_is_placeholder(color, SERVICE_GUI_THEME_PLACEHOLDER_GROUND))
    {
        placeholder = SERVICE_GUI_THEME_PLACEHOLDER_GROUND;
    }
    else
    {
        return color;
    }

    return lv_color_hex(service_gui_theme_map_placeholder(placeholder));
}

/**
 * @brief 给新建对象挂上共享颜色过滤器。
 * @param[in] theme 当前 display theme。
 * @param[in,out] obj 刚创建的对象。
 */
static void service_gui_theme_apply_cb(lv_theme_t *theme, lv_obj_t *obj)
{
    (void)theme;
    service_gui_theme_bind_obj(obj);
}

/**
 * @brief 递归给对象及其子对象挂上颜色过滤器。
 * @param[in,out] root 子树根；空指针则忽略。
 * @note SquareLine `remove_style_all` 之后必须再走一遍。Queue 后造行也要调用。
 */
void service_gui_theme_bind_tree(lv_obj_t *root)
{
    uint32_t i;
    uint32_t child_count;

    if (root == NULL)
    {
        return;
    }

    service_gui_theme_bind_obj(root);
    child_count = lv_obj_get_child_cnt(root);
    for (i = 0U; i < child_count; i++)
    {
        service_gui_theme_bind_tree(lv_obj_get_child(root, (int32_t)i));
    }
}

/**
 * @brief 给已导出的 Screen 整树重新挂过滤器。
 * @note 在 ui_init() 与 Main 准备之后调用，覆盖 SquareLine 清掉的 style。
 */
void service_gui_theme_bind_screens(void)
{
    const Service_GUI_ViewTypeDef *view = service_gui_view_get();

    service_gui_theme_bind_tree(view->boot.screen);
    service_gui_theme_bind_tree(view->boot_reveal);
    service_gui_theme_bind_tree(view->lock);
    service_gui_theme_bind_tree(view->main.screen);
}

/**
 * @brief 按当前外观设置 Screen 根的壁纸显隐与 Ground 底。
 * @param[in,out] screen Boot、BootReveal、Lock 或 Main。
 * @note Solid 关掉壁纸 Image，用占位 Ground 铺满，由过滤器映射成真值。
 *       Default 恢复透明壳 + 壁纸 COVER。不改 Arc 动画流程。
 */
static void service_gui_theme_apply_screen_chrome(lv_obj_t *screen)
{
    if (service_gui_theme_uses_wallpaper())
    {
        lv_obj_set_style_bg_img_opa(screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else
    {
        lv_obj_set_style_bg_img_opa(screen, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(
            screen,
            lv_color_hex(SERVICE_GUI_THEME_PLACEHOLDER_GROUND),
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_obj_invalidate(screen);
}

/**
 * @brief Solid 用半透明 Wash 代替 Music 毛玻璃图；Default 把 Tab 底重新打透明。
 * @note Default 的裁剪图由 background_prepare 绑定，此处不写 src，以免运行时切回
 *       Default 时丢掉已生成的图。Solid 清除 src，避免残留毛玻璃。
 */
static void service_gui_theme_apply_tabs_chrome(lv_obj_t *tabs)
{
    if (service_gui_theme_uses_glass())
    {
        lv_obj_set_style_bg_opa(
            tabs,
            LV_OPA_TRANSP,
            LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else
    {
        lv_obj_set_style_bg_img_src(
            tabs,
            NULL,
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_img_opa(
            tabs,
            LV_OPA_TRANSP,
            LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(
            tabs,
            SERVICE_GUI_THEME_SOLID_TABS_WASH_OPA,
            LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    lv_obj_invalidate(tabs);
}

/**
 * @brief 在 ui_init() 之后把占位色过滤器挂到 display theme。
 * @param[in,out] disp 已注册的 LVGL display。
 * @retval SERVICE_OK 已设置 theme，parent 为 ui_init() 装上的 basic theme。
 * @retval SERVICE_INVALID_PARAM disp 为空。
 * @note 只能由 GUI Task 在 Init 中、ui_init() 之后调用。不得放在 ui_init() 前：
 *       生成代码会 `lv_theme_basic_init` 覆盖 display theme。之后新建对象走 apply_cb。
 */
Service_StatusTypeDef service_gui_theme_attach(lv_disp_t *disp)
{
    lv_theme_t *parent;

    if (disp == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    if (!service_gui_theme_filter_ready)
    {
        lv_color_filter_dsc_init(
            &service_gui_theme_filter_dsc,
            service_gui_theme_filter_cb);
        lv_style_init(&service_gui_theme_filter_style);
        lv_style_set_color_filter_dsc(
            &service_gui_theme_filter_style,
            &service_gui_theme_filter_dsc);
        lv_style_set_color_filter_opa(
            &service_gui_theme_filter_style,
            LV_OPA_COVER);
        service_gui_theme_filter_ready = true;
    }

    parent = lv_disp_get_theme(disp);
    lv_memset_00(&service_gui_theme_instance, sizeof(service_gui_theme_instance));
    lv_theme_set_parent(&service_gui_theme_instance, parent);
    lv_theme_set_apply_cb(&service_gui_theme_instance, service_gui_theme_apply_cb);
    service_gui_theme_instance.disp = disp;

    if (parent != NULL)
    {
        service_gui_theme_instance.color_primary = parent->color_primary;
        service_gui_theme_instance.color_secondary = parent->color_secondary;
        service_gui_theme_instance.font_small = parent->font_small;
        service_gui_theme_instance.font_normal = parent->font_normal;
        service_gui_theme_instance.font_large = parent->font_large;
        service_gui_theme_instance.flags = parent->flags;
        service_gui_theme_instance.user_data = parent->user_data;
    }

    lv_disp_set_theme(disp, &service_gui_theme_instance);
    return SERVICE_OK;
}

/**
 * @brief 切换当前调色板，并更新 Boot/Lock/Main 壁纸与 Music Tab 薄层。
 * @param[in] id `SERVICE_GUI_THEME_DEFAULT` 或 `SERVICE_GUI_THEME_SOLID`。
 * @retval SERVICE_OK 已换表并 invalidate。
 * @retval SERVICE_INVALID_PARAM id 非法，当前外观不变。
 * @retval SERVICE_NOT_READY SquareLine 对象尚未由 ui_init() 创建。
 * @note 只能由 GUI Task 调用。不扫对象改 hex。首版在 Init 时对 STARTUP 外观调用；
 *       运行时切到 Default 时若尚未生成毛玻璃，不会在此处补做 Canvas 模糊。
 */
Service_StatusTypeDef Service_GUI_ThemeApply(uint8_t id)
{
    const Service_GUI_ViewTypeDef *view = service_gui_view_get();
    Service_StatusTypeDef status;

    status = service_gui_theme_select(id);
    if (status != SERVICE_OK)
    {
        return status;
    }

    if ((view->boot.screen == NULL) || (view->boot_reveal == NULL) ||
        (view->lock == NULL) || (view->main.screen == NULL) || (view->music.tabs == NULL))
    {
        return SERVICE_NOT_READY;
    }

    service_gui_theme_apply_screen_chrome(view->boot.screen);
    service_gui_theme_apply_screen_chrome(view->boot_reveal);
    service_gui_theme_apply_screen_chrome(view->lock);
    service_gui_theme_apply_screen_chrome(view->main.screen);
    service_gui_theme_apply_tabs_chrome(view->music.tabs);
    service_gui_theme_bind_screens();

    if (lv_scr_act() != NULL)
    {
        lv_obj_invalidate(lv_scr_act());
    }

    return SERVICE_OK;
}
