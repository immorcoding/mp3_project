/**
 ******************************************************************************
 * @file    ui_cell.h
 * @brief   界面数据的短锁模块：GUI 与任务之间交接一份固定大小的数据（一个“格子”）。
 *
 * @details
 *          APP 私有，`Service/gui` 不得包含（`scripts/check-layer-includes.ps1`）。
 *          只用于 GUI 与任务之间；任务之间的通知不走这里。
 *
 *          语义：格子里永远是完整的一份数据，读者读不到写了一半的内容；GUI 永远不等锁。
 *          全模块只有一把互斥锁，不对外提供锁函数，持锁期间只拷贝内存（`memcpy`），
 *          不调用文件接口、不调用回调、不等待别的对象，所以“持有界面数据锁时调用文件接口”
 *          写不出来。叫醒函数在锁外调用。
 *
 *          序号：每次成功写入递增。读者自己持有“已见序号”，先不拿锁比较序号，变了才拿锁
 *          拷贝，没有变化的帧不碰锁。序号起点为 1，所以全新读者（已见序号 0）会拿到一次
 *          格子的初值。
 *
 * @par 使用边界
 *          1. 需求是状态，不是动作。连按翻页由 GUI 算出目标页再写；真有不能合并的动作，
 *             带递增的动作序号或另用队列。
 *          2. 一个格子只有一个写者任务，可以有多个读者；调试版（未定义 NDEBUG；固件 Debug
 *             不带、Release 带 `-DNDEBUG`）用 `configASSERT` 检查，Release 不带这部分字段和代码。
 *          3. 约 8 KiB 以内进格子（初值，ARC-25，exploring）；更大的数据由应用持有两块
 *             缓冲，格子只传该看哪一块。本模块不限制大小，持锁时长随大小线性增长。
 *          4. 只在结果过期就会显示错内容时才比序号做“是否属于当前需求”的判断（曲名和封面）；
 *             列表窗口自带起点、代次和条数，又是整份拷贝，不比。那是应用的事，本模块只提供
 *             “变了才读”。
 *
 *          存储由调用者提供，大小由调用者的缓冲决定；本模块不在栈上放整份数据拷贝，
 *          也不认识应用和内容。
 ******************************************************************************
 */

#ifndef UI_CELL_H
#define UI_CELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 叫醒函数：写入成功后在锁外调用一次，用来叫醒读者所在的任务。
 * @note  可能在 GUI 任务或后台任务里被调用，不得阻塞；Boot Task 准备格子时读者任务还没创建，
 *        所以初始化传函数而不是任务句柄。
 */
typedef void (*UiCell_WakeCallback_t)(void);

/** @brief 一个格子。字段只由本模块读写，调用者只持有它并传给各函数。 */
typedef struct
{
    void *storage;                 /**< 数据存放处，由调用者提供，大小固定。 */
    size_t size;                   /**< 数据大小（字节）。 */
    UiCell_WakeCallback_t wake;    /**< 写入成功后调用；NULL 表示不叫醒（结果格）。 */
    volatile uint32_t sequence;    /**< 写入序号，起点 1；读者可不拿锁读取。 */
#ifndef NDEBUG
    bool writer_known;             /**< 调试版：是否已记下写者。 */
    const void *writer;            /**< 调试版：第一次写入时的任务句柄，之后其他任务写入即断言。 */
#endif
} UiCellTypeDef;

/* 初始化：传存储、大小与叫醒函数（结果格传 NULL）；只在 Boot Task 或单线程环境调用。 */
void ui_cell_init(UiCellTypeDef *cell, void *storage, size_t size, UiCell_WakeCallback_t wake);

/* 等到拿到锁的写（前台、后台任务用）：整份拷入、序号加一、锁外叫醒。 */
void ui_cell_write(UiCellTypeDef *cell, const void *src);

/* 不等的写（GUI 用）：拿不到锁返回 false，格子和序号都不变。 */
bool ui_cell_try_write(UiCellTypeDef *cell, const void *src);

/* 不等的“变了才读”：序号与 *seen 相同不碰锁；拿不到锁返回 false，dst 和 *seen 不变。 */
bool ui_cell_read_if_changed(const UiCellTypeDef *cell, void *dst, uint32_t *seen);

#endif /* UI_CELL_H */
