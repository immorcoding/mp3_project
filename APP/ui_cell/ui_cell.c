/**
 ******************************************************************************
 * @file    ui_cell.c
 * @brief   ui_cell 的实现：一把全模块互斥锁，持锁只拷贝内存。
 *
 * @details
 *          锁在第一次 `ui_cell_init` 时用静态存储创建，之后所有格子共用。持锁区间里只有
 *          `memcpy` 和序号推进；叫醒函数、断言检查都在锁外。
 ******************************************************************************
 */

#include "ui_cell.h"

#include "Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h"
#include "Middlewares/Third_Party/FreeRTOS/Source/include/task.h"

#include <string.h>

static StaticSemaphore_t lock_buffer; /* 全模块唯一的锁，静态存储。 */
static SemaphoreHandle_t lock_handle; /* 第一次 ui_cell_init 时创建。 */

/**
 * @brief 调试版：断言写者唯一。第一次写入的任务成为写者，之后其他任务写同一格子即失败。
 * @param[in,out] cell 格子。
 * @note  固件 Release 带 `-DNDEBUG`（CMake 的 Release 默认参数），这段连同头里的两个字段整体编译掉，
 *        不占时间也不占字段；Debug 没有 `NDEBUG`，保留。断言用产品的 `configASSERT`。
 */
static void assert_single_writer(UiCellTypeDef *cell)
{
#ifndef NDEBUG
    const void *current = (const void *)xTaskGetCurrentTaskHandle();

    if (!cell->writer_known)
    {
        cell->writer_known = true;
        cell->writer = current;
    }
    configASSERT(cell->writer == current);
#else
    (void)cell;
#endif
}

/**
 * @brief 在锁内把整份数据拷入格子并推进序号。
 * @param[in,out] cell 格子，调用者已持锁。
 * @param[in] src 新数据，`cell->size` 字节。
 */
static void store_locked(UiCellTypeDef *cell, const void *src)
{
    memcpy(cell->storage, src, cell->size);
    cell->sequence = cell->sequence + 1U;
}

/**
 * @brief 写入成功后在锁外叫醒读者。
 * @param[in] cell 格子。
 */
static void wake_reader(const UiCellTypeDef *cell)
{
    if (cell->wake != NULL)
    {
        cell->wake();
    }
}

/**
 * @brief 初始化一个格子；全模块的互斥锁在第一次调用时创建（静态存储，不占堆）。
 * @param[out] cell 格子。
 * @param[in,out] storage 数据存放处；本函数不改写内容，调用者在这之前放好初值（静态存储为全零）。
 * @param[in] size 数据大小（字节），非 0。
 * @param[in] wake 写入成功后的叫醒函数；结果格传 NULL。
 * @note  只在 Boot Task 里、其他使用者任务创建之前调用，不与其他 ui_cell 函数并发。
 */
void ui_cell_init(UiCellTypeDef *cell, void *storage, size_t size, UiCell_WakeCallback_t wake)
{
    configASSERT(cell != NULL);
    configASSERT(storage != NULL);
    configASSERT(size > 0U);

    if (lock_handle == NULL)
    {
        lock_handle = xSemaphoreCreateMutexStatic(&lock_buffer);
        configASSERT(lock_handle != NULL);
    }

    cell->storage = storage;
    cell->size = size;
    cell->wake = wake;
    cell->sequence = 1U;
#ifndef NDEBUG
    cell->writer_known = false;
    cell->writer = NULL;
#endif
}

/**
 * @brief 等到拿到锁的写：整份拷入格子、序号加一，随后在锁外叫醒读者。
 * @param[in,out] cell 格子。
 * @param[in] src 新数据，`size` 字节；返回后可立即复用。
 * @note  可能阻塞到拿到锁（持锁者只拷贝内存，时间有界）；前台、后台任务用，GUI 不用。
 */
void ui_cell_write(UiCellTypeDef *cell, const void *src)
{
    BaseType_t taken;

    configASSERT(cell != NULL);
    configASSERT(src != NULL);
    assert_single_writer(cell);

    taken = xSemaphoreTake(lock_handle, portMAX_DELAY);
    configASSERT(taken == pdTRUE); /* portMAX_DELAY 在真实内核里不会失败；桩里要靠钩子放行。 */
    if (taken != pdTRUE)
    {
        return;
    }
    store_locked(cell, src);
    (void)xSemaphoreGive(lock_handle);

    wake_reader(cell);
}

/**
 * @brief 不等的写：拿不到锁就放弃，格子和序号都不变。
 * @param[in,out] cell 格子。
 * @param[in] src 新数据，`size` 字节。
 * @retval true 已写入、序号加一、已叫醒读者。
 * @retval false 锁被占用，什么都没做；调用者保留意图，下一帧再试。
 * @note  永不阻塞；GUI 用。
 */
bool ui_cell_try_write(UiCellTypeDef *cell, const void *src)
{
    configASSERT(cell != NULL);
    configASSERT(src != NULL);
    assert_single_writer(cell);

    if (xSemaphoreTake(lock_handle, 0U) != pdTRUE)
    {
        return false;
    }
    store_locked(cell, src);
    (void)xSemaphoreGive(lock_handle);

    wake_reader(cell);
    return true;
}

/**
 * @brief 不等的“变了才读”：序号和 `*seen` 相同就什么都不做，不碰锁。
 * @param[in] cell 格子。
 * @param[out] dst 读者的副本，`size` 字节；只在返回 true 时被改写。
 * @param[in,out] seen 读者持有的已见序号；只在返回 true 时更新为拷到的那份的序号。
 * @retval true 拷到了新的一份。
 * @retval false 没有变化，或锁被占用（此时 `dst` 和 `*seen` 都不变，下一帧再试）。
 * @note  永不阻塞。
 */
bool ui_cell_read_if_changed(const UiCellTypeDef *cell, void *dst, uint32_t *seen)
{
    configASSERT(cell != NULL);
    configASSERT(dst != NULL);
    configASSERT(seen != NULL);

    if (cell->sequence == *seen) /* 先不拿锁比较序号：没变化的帧不碰锁。 */
    {
        return false;
    }
    if (xSemaphoreTake(lock_handle, 0U) != pdTRUE)
    {
        return false;
    }
    memcpy(dst, cell->storage, cell->size);
    *seen = cell->sequence; /* 序号在锁内读取，与拷到的那份一致。 */
    (void)xSemaphoreGive(lock_handle);
    return true;
}
