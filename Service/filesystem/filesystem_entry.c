/**
 ******************************************************************************
 * @file    filesystem_entry.c
 * @brief   文件与目录入口共用的进门和出门序列，以及 SD 的闸。
 *
 * @details
 *          闸是一个静态事件组的一位：开着时位为 1，关着时位为 0。选事件组而不是信号量，是因为开闸要一次叫醒
 *          所有在等的调用者（事件组置位会放行全部等待者），而用二值信号量只能一个一个传递；事件组也不占信号量，
 *          不改变三把互斥锁的个数。闸在第一次用到时创建，初始为开。
 *
 *          闸的等待没有上界：`INCLUDE_vTaskSuspend` 为 0 时 `portMAX_DELAY` 只是约 49.7 天的有限超时，
 *          超时后继续等。谁保证开闸：卷任务，每次暂停都要在所有路径（含挂载失败、刷新失败）上配一次恢复。
 *          等闸的调用者不持任何锁，所以不会因为它等得久而挡住别人；它也不占 CPU。
 ******************************************************************************
 */

#include "Service/filesystem/filesystem_entry.h"

#include "Service/filesystem/filesystem_card.h"
#include "Service/filesystem/filesystem_handle.h"
#include "Service/filesystem/sd/filesystem_sd_transfer.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/event_groups.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

/** @brief 闸的位：置位表示开。 */
#define FILESYSTEM_GATE_OPEN_BIT ((EventBits_t)1U)

static StaticEventGroup_t filesystem_gate_storage;
static EventGroupHandle_t filesystem_gate;
static volatile bool filesystem_gate_created;

/**
 * @brief 幂等地创建闸，初始为开。
 * @note  第一次调用在临界区内创建，之后只读一个标志；调度器启动前后都可以调用。
 */
static void filesystem_gate_ensure_created(void)
{
    if (filesystem_gate_created)
    {
        return;
    }

    taskENTER_CRITICAL();
    if (!filesystem_gate_created)
    {
        filesystem_gate = xEventGroupCreateStatic(&filesystem_gate_storage);
        configASSERT(filesystem_gate != NULL);
        (void)xEventGroupSetBits(filesystem_gate, FILESYSTEM_GATE_OPEN_BIT);
        filesystem_gate_created = true;
    }
    taskEXIT_CRITICAL();
}

/**
 * @brief 关闸：此后 SD 的文件与目录入口在拿锁之前等闸打开。
 * @note  不拿卷锁、不阻塞；重复关闸无副作用（闸不是计数的）。已经持锁的调用不被打断，已经通过闸、正在
 *        等卷锁的调用也不被追回：它们拿到锁之后由“卷还挂着吗”的第二次检查兜住。卷任务在每个检测边沿调用。
 */
void Service_Filesystem_PauseSD(void)
{
    filesystem_gate_ensure_created();
    (void)xEventGroupClearBits(filesystem_gate, FILESYSTEM_GATE_OPEN_BIT);
}

/**
 * @brief 开闸：一次放行所有在等的调用者。
 * @note  不拿卷锁、不阻塞；闸本来就开着时无副作用。放行的调用者各自去拿卷锁，由优先级继承保证高优先级先拿到。
 *        卷任务必须在每次暂停之后的所有路径上调用本函数，否则所有 SD 调用者会一直等下去。
 */
void Service_Filesystem_ResumeSD(void)
{
    filesystem_gate_ensure_created();
    (void)xEventGroupSetBits(filesystem_gate, FILESYSTEM_GATE_OPEN_BIT);
}

/**
 * @brief 等闸打开，无上界。
 * @note  调用者不得持任何 Filesystem 锁（断言停机）：持着锁等闸，卷任务要这把锁才能对账和开闸，就是死锁。
 *        等待期间调用者不持锁，所以不挡别的调用者，也不影响卷任务拿锁；调用者优先级再高也只是睡着。
 */
static void filesystem_entry_wait_gate(void)
{
    filesystem_lock_assert_caller_holds_none();
    filesystem_gate_ensure_created();

    while ((xEventGroupWaitBits(filesystem_gate, FILESYSTEM_GATE_OPEN_BIT, pdFALSE, pdFALSE, portMAX_DELAY) &
            FILESYSTEM_GATE_OPEN_BIT) == 0U)
    {
    }
}

/**
 * @brief 文件与目录入口的进门序列：拿锁前看卷没挂、SD 等闸、拿卷锁、拿到后再确认卷还挂着。
 * @param[in] volume 目标卷，合法（调用者已拒绝非法卷）。
 * @param[in] operation 本次公开调用的操作，只用于持锁统计。
 * @param[in] not_mounted_status 卷没挂时返回的状态：按路径或卷操作的入口给 `SERVICE_NOT_READY`，
 *                               按句柄操作的入口给 `SERVICE_INVALID_HANDLE`（卷没挂则其上所有句柄都已失效）。
 * @retval SERVICE_OK 已持所属卷的卷锁，调用者做完事必须调用 `filesystem_entry_leave`。
 * @retval 其他 卷没挂（值为 not_mounted_status），调用者不持锁，不得放锁。
 * @note  第一次检查在拿锁之前，所以卷没挂时不等卷锁：Flash 的 `MountFlash` 持锁 3.3 至 8.3 s，不能让调用者陪着等。
 *        等闸之后再看一次，是因为等闸的这段时间里卷可能已被卸载。拿到锁之后再确认一次，防的是另一种竞争：
 *        第一次检查通过后、拿到锁之前，卷任务在锁内卸载了卷。只读卷的挂载标志不需要锁（一个字节，只有持锁者写），
 *        所以前两次只是提示，真正的依据是拿到锁之后的这一次。已持锁的调用不受影响；先拿到锁的调用做完才轮到卷任务。
 */
Service_StatusTypeDef filesystem_entry_enter(Service_Filesystem_VolumeTypeDef volume,
                                             filesystem_lock_op_t operation,
                                             Service_StatusTypeDef not_mounted_status)
{
    if (!filesystem_handle_is_mounted(volume))
    {
        return not_mounted_status;
    }

    if (volume == SERVICE_FILESYSTEM_VOLUME_SD)
    {
        filesystem_entry_wait_gate();
        if (!filesystem_handle_is_mounted(volume))
        {
            return not_mounted_status;
        }
    }

    filesystem_lock_take(volume, operation);
    if (!filesystem_handle_is_mounted(volume))
    {
        filesystem_lock_give(volume);
        return not_mounted_status;
    }

    return SERVICE_OK;
}

/**
 * @brief 文件与目录入口的出门序列：锁内给失败分类，然后放锁。
 * @param[in] volume 目标卷，调用者持其卷锁。
 * @param[in] status 入口做完事得到的状态。
 * @return 最终返回给调用者的状态。SD 卷上的 `SERVICE_ERROR`（读写失败等没有更具体语义的失败）在锁内看卡检测脚，
 *         卡不在就改成 `SERVICE_NO_MEDIA`；其他状态原样返回。
 * @note  看卡检测脚必须在卷锁内：这样“失败”和“卡不在”是同一刻的结论，不会夹进卷任务的卸载。读脚是一次 GPIO
 *        输入寄存器读取，任务上下文安全，不碰 SDMMC 也不改任何状态。Flash 没有卡检测脚，不分类。
 */
Service_StatusTypeDef filesystem_entry_leave(Service_Filesystem_VolumeTypeDef volume, Service_StatusTypeDef status)
{
    if ((volume == SERVICE_FILESYSTEM_VOLUME_SD) && (status == SERVICE_ERROR) &&
        !filesystem_sd_transfer_card_present())
    {
        status = SERVICE_NO_MEDIA;
    }

    filesystem_lock_give(volume);
    return status;
}
