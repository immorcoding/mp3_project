/**
 ******************************************************************************
 * @file    resource_service.c
 * @brief   当前产品 RPKC1 映射、校验和 SDRAM 启动加载实现。
 ******************************************************************************
 */

#include "Service/resource/resource_service.h"
#include "Service/resource/resource_service_config.h"

#include <stddef.h>
#include <string.h>

#include "Adapters/cortex/cache/cortex_m7_dcache_adapter.h"
#if SERVICE_RESOURCE_LOG_ENABLE && SERVICE_RESOURCE_LOG_TIMING_ENABLE
#include "Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter.h"
#endif
#include "Components/log/log.h"
#include "Components/resource_pack/resource_pack.h"
#include "Platform/flash/platform_flash.h"

/** @brief 链接器为 CP936 Unicode 到 OEM 表预留的 SDRAM 起点。 */
extern uint8_t __external_resource_uni2oem_start__[];
/** @brief 链接器为 CP936 Unicode 到 OEM 表预留的 SDRAM 终点。 */
extern uint8_t __external_resource_uni2oem_end__[];
/** @brief 链接器为 CP936 OEM 到 Unicode 表预留的 SDRAM 起点。 */
extern uint8_t __external_resource_oem2uni_start__[];
/** @brief 链接器为 CP936 OEM 到 Unicode 表预留的 SDRAM 终点。 */
extern uint8_t __external_resource_oem2uni_end__[];
/** @brief 链接器为默认壁纸数据预留的 SDRAM 起点。 */
extern uint8_t __external_resource_wallpaper_start__[];
/** @brief 链接器为默认壁纸数据预留的 SDRAM 终点。 */
extern uint8_t __external_resource_wallpaper_end__[];

/** @brief 单个启动资源的产品约束和 SDRAM 目标。 */
typedef struct
{
    uint32_t ResourceID;
    uint16_t ExpectedType;
    uint8_t *DestinationStart;
    uint8_t *DestinationEnd;
    bool Required;
    bool VerifyAfterCopy;
} Service_ResourceTargetTypeDef;

/** @brief 当前资源包物理位置和产品身份。 */
typedef struct
{
    uint32_t FlashOffset;
    uint32_t MaximumSize;
    uint32_t VendorID;
    uint32_t ProductID;
} Service_ResourceSourceConfigTypeDef;

/** @brief 当前产品只允许从预留的 NOR 资源分区打开 RPKC1。 */
static const Service_ResourceSourceConfigTypeDef resource_source_config = {
    .FlashOffset = SERVICE_RESOURCE_FLASH_OFFSET,
    .MaximumSize = SERVICE_RESOURCE_MAXIMUM_SIZE,
    .VendorID = SERVICE_RESOURCE_VENDOR_ID,
    .ProductID = SERVICE_RESOURCE_PRODUCT_ID
};

/** @brief 当前启动前必须加载完成的三项资源。 */
static const Service_ResourceTargetTypeDef resource_targets[] = {
    {
        .ResourceID = SERVICE_RESOURCE_ID_CP936_UNI2OEM,
        .ExpectedType = RESOURCE_PACK_TYPE_BINARY,
        .DestinationStart = __external_resource_uni2oem_start__,
        .DestinationEnd = __external_resource_uni2oem_end__,
        .Required = true,
        .VerifyAfterCopy = true
    },
    {
        .ResourceID = SERVICE_RESOURCE_ID_CP936_OEM2UNI,
        .ExpectedType = RESOURCE_PACK_TYPE_BINARY,
        .DestinationStart = __external_resource_oem2uni_start__,
        .DestinationEnd = __external_resource_oem2uni_end__,
        .Required = true,
        .VerifyAfterCopy = true
    },
    {
        .ResourceID = SERVICE_RESOURCE_ID_DEFAULT_WALLPAPER,
        .ExpectedType = RESOURCE_PACK_TYPE_IMAGE,
        .DestinationStart = __external_resource_wallpaper_start__,
        .DestinationEnd = __external_resource_wallpaper_end__,
        .Required = true,
        .VerifyAfterCopy = true
    }
};

/** @brief Service 生命周期内保留的最终状态和最近失败信息。 */
static Service_ResourceDiagnosticsTypeDef resource_diagnostics;

#if SERVICE_RESOURCE_LOG_ENABLE
/**
 * @brief 将无符号 64 位整数转换为十进制字符串。
 * @param value 待转换数值。
 * @param text 接收字符串的缓冲区。
 * @param text_size 缓冲区字节数，包含字符串结束符。
 * @retval true 转换成功。
 * @retval false 参数无效或缓冲区空间不足。
 */
static bool resource_format_u64(uint64_t value, char *text, uint32_t text_size)
{
    char reversed_digits[20];
    uint32_t digit_count = 0U;

    if ((text == NULL) || (text_size == 0U))
    {
        return false;
    }

    do
    {
        reversed_digits[digit_count] = (char)('0' + (value % 10U));
        digit_count++;
        value /= 10U;
    } while (value != 0U);

    if (text_size <= digit_count)
    {
        return false;
    }

    for (uint32_t index = 0U; index < digit_count; index++)
    {
        text[index] = reversed_digits[digit_count - index - 1U];
    }

    text[digit_count] = '\0';
    return true;
}
#endif

/**
 * @brief 记录初始化失败的资源、阶段和 Component 状态。
 * @param resource_id 失败资源 ID；包级失败使用零。
 * @param stage 失败阶段。
 * @param component_status 最接近失败原因的 Component 状态。
 * @return 固定返回 SERVICE_ERROR，便于调用者直接上抛。
 */
static Service_StatusTypeDef resource_fail(
    uint32_t resource_id,
    Service_ResourceStageTypeDef stage,
    ResourcePack_StatusTypeDef component_status)
{
    resource_diagnostics.IsReady = false;
    resource_diagnostics.FailedResourceID = resource_id;
    resource_diagnostics.FailedStage = stage;
    resource_diagnostics.ComponentStatus = component_status;
    return SERVICE_ERROR;
}

/**
 * @brief 校验当前产品对 CP936 BINARY Metadata 的额外约束。
 * @param entry 当前 BINARY Entry。
 * @param view 当前包内 View。
 * @return Component 风格的约束校验状态。
 */
static ResourcePack_StatusTypeDef resource_validate_cp936_metadata(
    const ResourcePack_EntryTypeDef *entry,
    const ResourcePack_EntryViewTypeDef *view)
{
    ResourcePack_BinaryMetadataTypeDef metadata;
    ResourcePack_StatusTypeDef status = ResourcePack_DecodeBinaryMetadata(
        entry,
        view,
        &metadata);

    if (status != RESOURCE_PACK_OK)
    {
        return status;
    }

    if ((metadata.ElementFormat != RESOURCE_PACK_ELEMENT_FORMAT_UINT16) ||
        (metadata.ByteOrder != RESOURCE_PACK_BYTE_ORDER_LITTLE_ENDIAN) ||
        (metadata.ElementSize != 2U) ||
        (metadata.ElementCount != SERVICE_RESOURCE_CP936_ELEMENT_COUNT) ||
        (entry->DataLength != SERVICE_RESOURCE_CP936_TABLE_BYTES))
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    return RESOURCE_PACK_OK;
}

/**
 * @brief 校验当前产品对默认壁纸 IMAGE Metadata 的额外约束。
 * @param entry 当前 IMAGE Entry。
 * @param view 当前包内 View。
 * @return Component 风格的约束校验状态。
 */
static ResourcePack_StatusTypeDef resource_validate_wallpaper_metadata(
    const ResourcePack_EntryTypeDef *entry,
    const ResourcePack_EntryViewTypeDef *view)
{
    ResourcePack_ImageMetadataTypeDef metadata;
    ResourcePack_StatusTypeDef status = ResourcePack_DecodeImageMetadata(
        entry,
        view,
        &metadata);

    if (status != RESOURCE_PACK_OK)
    {
        return status;
    }

    if ((metadata.ImageFormat != RESOURCE_PACK_IMAGE_FORMAT_LVGL_NATIVE) ||
        (metadata.PixelFormat != RESOURCE_PACK_PIXEL_FORMAT_TRUE_COLOR_ALPHA) ||
        (metadata.Width != SERVICE_RESOURCE_WALLPAPER_WIDTH) ||
        (metadata.Height != SERVICE_RESOURCE_WALLPAPER_HEIGHT) ||
        (metadata.StrideBytes != SERVICE_RESOURCE_WALLPAPER_STRIDE_BYTES) ||
        (metadata.FrameCount != 1U) ||
        (metadata.ColorSpace != RESOURCE_PACK_COLOR_SPACE_SRGB) ||
        (metadata.AlphaMode != RESOURCE_PACK_ALPHA_MODE_STRAIGHT) ||
        (entry->DataLength != SERVICE_RESOURCE_WALLPAPER_BYTES))
    {
        return RESOURCE_PACK_FORMAT_ERROR;
    }

    return RESOURCE_PACK_OK;
}

/**
 * @brief 依据目标 ID 和类型执行当前产品的 Metadata 约束校验。
 * @param target 当前目标配置。
 * @param entry 匹配 Entry。
 * @param view 匹配包内 View。
 * @return Metadata 校验状态。
 */
static ResourcePack_StatusTypeDef resource_validate_target_metadata(
    const Service_ResourceTargetTypeDef *target,
    const ResourcePack_EntryTypeDef *entry,
    const ResourcePack_EntryViewTypeDef *view)
{
    if ((target->ResourceID == SERVICE_RESOURCE_ID_CP936_UNI2OEM) ||
        (target->ResourceID == SERVICE_RESOURCE_ID_CP936_OEM2UNI))
    {
        return resource_validate_cp936_metadata(entry, view);
    }

    if (target->ResourceID == SERVICE_RESOURCE_ID_DEFAULT_WALLPAPER)
    {
        return resource_validate_wallpaper_metadata(entry, view);
    }

    return RESOURCE_PACK_UNSUPPORTED_TYPE;
}

/**
 * @brief 查找、校验并加载一项目标资源到链接器预留的 SDRAM。
 * @param handle 当前启动期资源包句柄。
 * @param target 产品目标配置。
 * @return 加载结果。
 */
static Service_StatusTypeDef resource_load_target(
    const ResourcePack_HandleTypeDef *handle,
    const Service_ResourceTargetTypeDef *target)
{
    ResourcePack_EntryTypeDef entry;
    ResourcePack_EntryViewTypeDef view;
    ResourcePack_StatusTypeDef component_status;
    uint32_t destination_size;

    component_status = ResourcePack_FindEntry(handle, target->ResourceID, &entry);
    if (component_status != RESOURCE_PACK_OK)
    {
        if ((!target->Required) && (component_status == RESOURCE_PACK_NOT_FOUND))
        {
            return SERVICE_OK;
        }

        return resource_fail(target->ResourceID,
                             SERVICE_RESOURCE_STAGE_FIND_ENTRY,
                             component_status);
    }

    if (entry.ResourceType != target->ExpectedType)
    {
        return resource_fail(target->ResourceID,
                             SERVICE_RESOURCE_STAGE_CHECK_TYPE,
                             RESOURCE_PACK_FORMAT_ERROR);
    }

    component_status = ResourcePack_GetEntryView(handle, &entry, &view);
    if (component_status != RESOURCE_PACK_OK)
    {
        return resource_fail(target->ResourceID,
                             SERVICE_RESOURCE_STAGE_CHECK_METADATA,
                             component_status);
    }

    component_status = resource_validate_target_metadata(target, &entry, &view);
    if (component_status != RESOURCE_PACK_OK)
    {
        return resource_fail(target->ResourceID,
                             SERVICE_RESOURCE_STAGE_CHECK_METADATA,
                             component_status);
    }

    destination_size = (uint32_t)(target->DestinationEnd - target->DestinationStart);
    if ((destination_size < entry.DataLength) ||
        (((uintptr_t)target->DestinationStart % CORTEX_M7_DCACHE_LINE_SIZE) != 0U))
    {
        return resource_fail(target->ResourceID,
                             SERVICE_RESOURCE_STAGE_CHECK_DESTINATION,
                             RESOURCE_PACK_FORMAT_ERROR);
    }

    memcpy(target->DestinationStart, view.Data, entry.DataLength);

    if (target->VerifyAfterCopy &&
        (ResourcePack_VerifyBuffer(target->DestinationStart,
                                   entry.DataLength,
                                   entry.DataCRC32) != RESOURCE_PACK_OK))
    {
        return resource_fail(target->ResourceID,
                             SERVICE_RESOURCE_STAGE_VERIFY_DATA,
                             RESOURCE_PACK_CRC_ERROR);
    }

    if (!CortexM7DCache_Clean_Rounded(target->DestinationStart, entry.DataLength))
    {
        return resource_fail(target->ResourceID,
                             SERVICE_RESOURCE_STAGE_CLEAN_CACHE,
                             RESOURCE_PACK_INVALID_PARAM);
    }

#if SERVICE_RESOURCE_LOG_ENABLE && SERVICE_RESOURCE_LOG_ENTRY_ENABLE
    char version_text[21] = "?";

    (void)resource_format_u64(entry.ResourceVersion,
                              version_text,
                              sizeof(version_text));

    (void)LOG_Printf(LOG_LEVEL_INFO,
                     "RESOURCE",
                     "Loaded id=%lu, type=%u, version=%s, bytes=%lu.",
                     (unsigned long)entry.ResourceID,
                     (unsigned int)entry.ResourceType,
                     version_text,
                     (unsigned long)entry.DataLength);
#endif

    return SERVICE_OK;
}

/**
 * @brief 在任务启动前打开 RPKC1 并加载当前产品全部必需资源。
 * @retval SERVICE_OK 三项资源已校验并加载到 SDRAM。
 * @retval SERVICE_ERROR 映射、协议、身份、资源或目标校验失败。
 * @note 初始化成功后不保留或向外发布 NOR 映射 View。
 */
Service_StatusTypeDef Service_Resource_Init(void)
{
    const uint8_t *mapped_base;
    uint32_t mapped_size;
    const uint8_t *pack;
    ResourcePack_HandleTypeDef handle = {0};
    ResourcePack_InfoTypeDef info;
    ResourcePack_StatusTypeDef component_status;
#if SERVICE_RESOURCE_LOG_ENABLE && SERVICE_RESOURCE_LOG_TIMING_ENABLE
    uint32_t start_cycles;
    uint32_t elapsed_cycles;
    uint32_t frequency_hz;
#endif

    memset(&resource_diagnostics, 0, sizeof(resource_diagnostics));

#if SERVICE_RESOURCE_LOG_ENABLE && SERVICE_RESOURCE_LOG_TIMING_ENABLE
    if (!CortexM7CycleCounter_Start())
    {
        return resource_fail(0U,
                             SERVICE_RESOURCE_STAGE_MAP_FLASH,
                             RESOURCE_PACK_INVALID_PARAM);
    }
    start_cycles = CortexM7CycleCounter_Read();
#endif

    if (Platform_Flash_EnableMemoryMappedMode(&mapped_base, &mapped_size) != PLATFORM_OK)
    {
        return resource_fail(0U,
                             SERVICE_RESOURCE_STAGE_MAP_FLASH,
                             RESOURCE_PACK_NOT_OPEN);
    }

    if ((resource_source_config.FlashOffset > mapped_size) ||
        (resource_source_config.MaximumSize >
         mapped_size - resource_source_config.FlashOffset))
    {
        return resource_fail(0U,
                             SERVICE_RESOURCE_STAGE_MAP_FLASH,
                             RESOURCE_PACK_FORMAT_ERROR);
    }

    pack = mapped_base + resource_source_config.FlashOffset;
    component_status = ResourcePack_Open(pack,
                                         resource_source_config.MaximumSize,
                                         &handle);
    if (component_status != RESOURCE_PACK_OK)
    {
        return resource_fail(0U,
                             SERVICE_RESOURCE_STAGE_OPEN_PACKAGE,
                             component_status);
    }

    component_status = ResourcePack_GetInfo(&handle, &info);
    if (component_status != RESOURCE_PACK_OK)
    {
        return resource_fail(0U,
                             SERVICE_RESOURCE_STAGE_OPEN_PACKAGE,
                             component_status);
    }

    resource_diagnostics.VendorID = info.VendorID;
    resource_diagnostics.ProductID = info.ProductID;
    resource_diagnostics.PackageVersion = info.PackageVersion;
    resource_diagnostics.EntryCount = info.EntryCount;

    if ((info.VendorID != resource_source_config.VendorID) ||
        (info.ProductID != resource_source_config.ProductID))
    {
        return resource_fail(0U,
                             SERVICE_RESOURCE_STAGE_CHECK_IDENTITY,
                             RESOURCE_PACK_FORMAT_ERROR);
    }

#if SERVICE_RESOURCE_LOG_ENABLE
    char package_version_text[21] = "?";

    (void)resource_format_u64(info.PackageVersion,
                              package_version_text,
                              sizeof(package_version_text));

    (void)LOG_Printf(LOG_LEVEL_INFO,
                     "RESOURCE",
                     "RPKC1 vendor=%lu, product=%lu, version=%s, entries=%u.",
                     (unsigned long)info.VendorID,
                     (unsigned long)info.ProductID,
                     package_version_text,
                     (unsigned int)info.EntryCount);
#endif

    for (uint32_t index = 0U;
         index < sizeof(resource_targets) / sizeof(resource_targets[0]);
         index++)
    {
        if (resource_load_target(&handle, &resource_targets[index]) != SERVICE_OK)
        {
            return SERVICE_ERROR;
        }
    }

    resource_diagnostics.IsReady = true;
    resource_diagnostics.FailedStage = SERVICE_RESOURCE_STAGE_NONE;
    resource_diagnostics.ComponentStatus = RESOURCE_PACK_OK;

#if SERVICE_RESOURCE_LOG_ENABLE && SERVICE_RESOURCE_LOG_TIMING_ENABLE
    elapsed_cycles = CortexM7CycleCounter_Read() - start_cycles;
    frequency_hz = CortexM7CycleCounter_GetFrequencyHz();
    if (frequency_hz != 0U)
    {
        uint64_t elapsed_us = (uint64_t)elapsed_cycles * 1000000ULL / frequency_hz;
        char elapsed_us_text[21] = "?";

        (void)resource_format_u64(elapsed_us,
                                  elapsed_us_text,
                                  sizeof(elapsed_us_text));

        (void)LOG_Printf(LOG_LEVEL_INFO,
                         "RESOURCE",
                         "Initialization time=%s us.",
                         elapsed_us_text);
    }
#endif

    return SERVICE_OK;
}

/**
 * @brief 查询当前三项启动资源是否全部可用。
 * @retval true 初始化已经完整成功。
 * @retval false 尚未初始化或初始化失败。
 */
bool Service_Resource_IsReady(void)
{
    return resource_diagnostics.IsReady;
}

/**
 * @brief 取得最近一次资源初始化结果快照。
 * @param diagnostics 接收状态、包身份和失败位置。
 * @retval SERVICE_OK 已返回快照。
 * @retval SERVICE_INVALID_PARAM 输出指针为空。
 */
Service_StatusTypeDef Service_Resource_GetDiagnostics(
    Service_ResourceDiagnosticsTypeDef *diagnostics)
{
    if (diagnostics == NULL)
    {
        return SERVICE_INVALID_PARAM;
    }

    *diagnostics = resource_diagnostics;
    return SERVICE_OK;
}
