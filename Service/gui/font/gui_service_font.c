/**
  ******************************************************************************
  * @file    gui_service_font.c
  * @brief   曲名字体子模块：查询 Resource Service 的可选 FONT 载荷，解码到 GUI 自有的 SDRAM 区。
  *
  * @details
  *          Service_GUI_Init() 在创建 Screen 之前调用 service_gui_font_init() 一次（GUI Task）。
  *          载荷可用时先 Measure 再 Build，把字形描述、cmap、字距与重排位图写进链接器为本模块
  *          预留的 `.sdram_gui_font` 区，并填写本模块持有的 lv_font_t；只读访问 Resource 的 FONT
  *          区，不写回。成功记一条 INFO（字形数、解码耗时），不可用或失败记一条带原因的 WARN，
  *          曲名 Label 改用 lv_font_montserrat_12。不使用 LVGL 堆，不改 lv_conf.h。
  *
  *          解码区只经 CPU 读写（LVGL 软件渲染逐字形读取位图），不作 DMA 源或目标，
  *          因此不需要 Cache 维护。
  ******************************************************************************
  */

#include "Service/gui/font/gui_service_font.h"

#include <stdbool.h>
#include <stdint.h>

#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#include "Service/gui/font/gui_service_font_decoder.h"
#include "Service/log/log_service.h"
#include "Service/resource/resource_service.h"

/** @brief 链接器为 GUI 字体解码区预留的 SDRAM 起点（32 B 对齐）。 */
extern uint8_t __gui_font_decode_start__[];
/** @brief 链接器为 GUI 字体解码区预留的 SDRAM 终点。 */
extern uint8_t __gui_font_decode_end__[];

/** @brief 日志 tag，静态存储期（DIAG-1）。 */
static const char service_gui_font_log_tag[] = "GUI";

/** @brief 解码成功后的曲名字体；dsc 指向解码区首块。 */
static lv_font_t service_gui_font_decoded;

/** @brief 解码成功后为真；为假时曲名用 Montserrat 12。 */
static bool service_gui_font_ready;

/**
 * @brief 记一条字体回退 WARN。
 * @param[in] reason 静态英文短语，说明为何不可用。
 */
static void service_gui_font_warn(const char *reason)
{
    char text[64];

    (void)lv_snprintf(text, sizeof(text), "Title font fallback: %s.", reason);
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_WARN, service_gui_font_log_tag, text);
}

/**
 * @brief 查询可选字体并解码；只在 GUI Task 的 Service_GUI_Init() 中调用一次。
 * @note  任何失败都不返回错误：记 WARN 后曲名回退 Montserrat，GUI 照常启动。
 *        解码器失败时不写解码区与字体对象，因此回退后不会留下半成品。
 */
void service_gui_font_init(void)
{
    Service_ResourceFontTypeDef payload;
    Service_GUI_FontDecodeInfoTypeDef info;
    Service_GUI_FontDecodeStatusTypeDef status;
    const uint32_t region_size = (uint32_t)(__gui_font_decode_end__ - __gui_font_decode_start__);
    uint32_t start_cycles;
    uint32_t elapsed_cycles;
    uint32_t frequency_hz;
    char text[80];

    service_gui_font_ready = false;

    if ((Service_Resource_GetFont(&payload) != SERVICE_OK) || !payload.IsAvailable)
    {
        service_gui_font_warn("resource unavailable");
        return;
    }

    /* 计数器由 app_init() 在 Resource 加载前启动；这里只读，不复位，避免干扰其他测量。 */
    start_cycles = CortexM7CycleCounter_Read();

    status = service_gui_font_decode_measure(payload.Data, payload.Length, &info);
    if ((status == SERVICE_GUI_FONT_DECODE_OK) && (info.RegionSize > region_size))
    {
        status = SERVICE_GUI_FONT_DECODE_OUTPUT_TOO_SMALL;
    }

    if (status == SERVICE_GUI_FONT_DECODE_OK)
    {
        status = service_gui_font_decode_build(payload.Data,
                                               payload.Length,
                                               __gui_font_decode_start__,
                                               region_size,
                                               &service_gui_font_decoded,
                                               &info);
    }

    elapsed_cycles = CortexM7CycleCounter_Read() - start_cycles;

    if (status != SERVICE_GUI_FONT_DECODE_OK)
    {
        service_gui_font_warn(service_gui_font_decode_status_text(status));
        return;
    }

    service_gui_font_ready = true;

    frequency_hz = CortexM7CycleCounter_GetFrequencyHz();
    (void)lv_snprintf(text,
                      sizeof(text),
                      "Title font ready: glyphs=%lu, bytes=%lu, decode=%lu us.",
                      (unsigned long)info.GlyphCount,
                      (unsigned long)info.RegionSize,
                      (unsigned long)((frequency_hz == 0U) ? 0ULL :
                          ((uint64_t)elapsed_cycles * 1000000ULL / frequency_hz)));
    (void)Service_Log_Post(SERVICE_LOG_LEVEL_INFO, service_gui_font_log_tag, text);
}

/**
 * @brief 显示曲目元数据的 Label（唱盘页曲名、Queue 曲名与歌手）应使用的字体（GUI-24）。
 * @return 解码成功时为资源包 FONT 解码出的字体，否则为 lv_font_montserrat_12。
 * @note  在 service_gui_font_init() 之后调用；结果在 GUI 生命周期内不变。
 */
const lv_font_t *service_gui_font_title(void)
{
    return service_gui_font_ready ? &service_gui_font_decoded : &lv_font_montserrat_12;
}
