/**
 ******************************************************************************
 * @file    test_ui_cell.c
 * @brief   ui_cell 的主机行为测试：整份拷贝、序号、没变不拷贝、拿不到锁、叫醒。
 *
 * @details
 *          只经 `ui_cell.h` 的公开函数观察行为。模块把唯一的互斥锁藏在内部，测试经共用桩的
 *          `freertos_stub_last_created_semaphore()` 取得句柄，用“让接下来 N 次拿取失败”
 *          和阻塞钩子脚本化别的任务占着锁。主机测试是单线程的，通过不证明真实并发。
 ******************************************************************************
 */

#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos_stub.h"
#include "APP/ui_cell/ui_cell.h"

#define CELL_SIZE 64U

/** @brief 测试里的一个格子加它的存储与读者副本。 */
typedef struct
{
    UiCellTypeDef cell;
    uint8_t storage[CELL_SIZE];
} Fixture;

static SemaphoreHandle_t module_lock; /* 模块唯一的锁；第一次初始化时创建，之后跨用例保留。 */

static uint32_t wake_calls;     /* count_wake 被调用的次数。 */
static bool wake_saw_free_lock; /* 最近一次 count_wake 调用时模块锁是否空闲。 */

/**
 * @brief 测试用叫醒函数：计数，并检查调用时模块锁已放开。
 */
static void count_wake(void)
{
    wake_calls++;
    if (xSemaphoreTake(module_lock, 0U) == pdTRUE)
    {
        wake_saw_free_lock = true;
        assert(xSemaphoreGive(module_lock) == pdTRUE);
    }
}

/**
 * @brief 把缓冲区填成一个可辨认的图案。
 * @param[out] buffer 缓冲区，`CELL_SIZE` 字节。
 * @param[in] seed 图案种子。
 */
static void fill_pattern(uint8_t *buffer, uint8_t seed)
{
    for (size_t i = 0U; i < CELL_SIZE; i++)
    {
        buffer[i] = (uint8_t)(seed + i);
    }
}

/**
 * @brief 准备一个用例：重置桩、初始化格子、取得模块锁句柄并清掉上个用例的脚本。
 * @param[out] fixture 格子与存储。
 * @param[in] wake 叫醒函数，可为 NULL。
 */
static void setup(Fixture *fixture, UiCell_WakeCallback_t wake)
{
    freertos_stub_reset();
    memset(fixture, 0, sizeof(*fixture));
    ui_cell_init(&fixture->cell, fixture->storage, CELL_SIZE, wake);
    if (module_lock == NULL)
    {
        module_lock = freertos_stub_last_created_semaphore();
    }
    assert(module_lock != NULL);
    freertos_stub_semaphore_fail_next_takes(module_lock, 0U);
}

/** @brief 新读者拿到格子的初值一次，之后没变化就不再拷贝。 */
static void test_new_reader_gets_initial_value_once(void)
{
    Fixture f;
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;

    setup(&f, NULL);
    fill_pattern(f.storage, 7U); /* 初值在初始化之后、第一次读之前放好；init 不改存储。 */

    memset(copy, 0xEE, sizeof(copy));
    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(seen != 0U);
    assert(memcmp(copy, f.storage, CELL_SIZE) == 0);

    memset(copy, 0xEE, sizeof(copy));
    assert(!ui_cell_read_if_changed(&f.cell, copy, &seen));
}

/** @brief 写入后读者拿到整份数据，一个字节都不少。 */
static void test_write_then_read_copies_the_whole_value(void)
{
    Fixture f;
    uint8_t src[CELL_SIZE];
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;

    setup(&f, NULL);
    fill_pattern(src, 0x10U);
    ui_cell_write(&f.cell, src);

    memset(copy, 0, sizeof(copy));
    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(memcmp(copy, src, CELL_SIZE) == 0);
}

/** @brief 写入时拷走数据：之后改动调用者的源缓冲不影响格子。 */
static void test_write_takes_a_copy_of_the_source(void)
{
    Fixture f;
    uint8_t src[CELL_SIZE];
    uint8_t expected[CELL_SIZE];
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;

    setup(&f, NULL);
    fill_pattern(src, 0x20U);
    memcpy(expected, src, CELL_SIZE);
    ui_cell_write(&f.cell, src);
    memset(src, 0xFF, sizeof(src));

    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(memcmp(copy, expected, CELL_SIZE) == 0);
}

/** @brief 没变不拷贝：读者副本不被改写，已见序号不变，而且不碰锁。 */
static void test_unchanged_cell_is_not_copied_and_lock_is_untouched(void)
{
    Fixture f;
    uint8_t src[CELL_SIZE];
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;
    uint32_t seen_after_first;
    uint32_t takes_before;

    setup(&f, NULL);
    fill_pattern(src, 0x30U);
    ui_cell_write(&f.cell, src);
    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    seen_after_first = seen;

    memset(copy, 0xA5, sizeof(copy));
    takes_before = freertos_stub_semaphore_stats(module_lock).take_calls;
    assert(!ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(!ui_cell_read_if_changed(&f.cell, copy, &seen));

    for (size_t i = 0U; i < CELL_SIZE; i++)
    {
        assert(copy[i] == 0xA5U);
    }
    assert(seen == seen_after_first);
    assert(freertos_stub_semaphore_stats(module_lock).take_calls == takes_before);
}

/** @brief 每次写入序号都前进；连写两次读者只读到最新的一份。 */
static void test_sequence_advances_and_reader_sees_latest_only(void)
{
    Fixture f;
    uint8_t first[CELL_SIZE];
    uint8_t second[CELL_SIZE];
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;
    uint32_t seen_after_first_write;

    setup(&f, NULL);
    fill_pattern(first, 0x40U);
    fill_pattern(second, 0x50U);

    ui_cell_write(&f.cell, first);
    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    seen_after_first_write = seen;

    ui_cell_write(&f.cell, first);
    ui_cell_write(&f.cell, second);
    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(seen != seen_after_first_write);
    assert(memcmp(copy, second, CELL_SIZE) == 0);
    assert(!ui_cell_read_if_changed(&f.cell, copy, &seen));
}

/** @brief 内容相同的写入也算变化：序号才是判据，不是内容比较。 */
static void test_rewriting_same_bytes_still_counts_as_a_change(void)
{
    Fixture f;
    uint8_t src[CELL_SIZE];
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;

    setup(&f, NULL);
    fill_pattern(src, 0x60U);
    ui_cell_write(&f.cell, src);
    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));

    ui_cell_write(&f.cell, src);
    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
}

/** @brief 已见序号由读者持有：两个读者互不影响。 */
static void test_readers_track_their_own_seen_sequence(void)
{
    Fixture f;
    uint8_t src[CELL_SIZE];
    uint8_t copy_a[CELL_SIZE];
    uint8_t copy_b[CELL_SIZE];
    uint32_t seen_a = 0U;
    uint32_t seen_b = 0U;

    setup(&f, NULL);
    fill_pattern(src, 0x70U);
    ui_cell_write(&f.cell, src);

    assert(ui_cell_read_if_changed(&f.cell, copy_a, &seen_a));
    assert(!ui_cell_read_if_changed(&f.cell, copy_a, &seen_a));
    assert(ui_cell_read_if_changed(&f.cell, copy_b, &seen_b));
    assert(memcmp(copy_b, src, CELL_SIZE) == 0);
}

/** @brief 拿不到锁时不等的写返回假：数据和序号都不变，不叫醒；锁恢复后写成功。 */
static void test_try_write_without_lock_fails_and_leaves_cell_unchanged(void)
{
    Fixture f;
    uint8_t old_value[CELL_SIZE];
    uint8_t new_value[CELL_SIZE];
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;

    setup(&f, count_wake);
    fill_pattern(old_value, 0x01U);
    fill_pattern(new_value, 0x02U);
    ui_cell_write(&f.cell, old_value);
    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    wake_calls = 0U;

    freertos_stub_semaphore_fail_next_takes(module_lock, 1U);
    assert(!ui_cell_try_write(&f.cell, new_value));

    assert(memcmp(f.storage, old_value, CELL_SIZE) == 0);
    assert(!ui_cell_read_if_changed(&f.cell, copy, &seen)); /* 序号没动。 */
    assert(wake_calls == 0U);
    assert(freertos_stub_semaphore_stats(module_lock).last_timeout_ticks == 0U); /* 不等。 */

    assert(ui_cell_try_write(&f.cell, new_value));
    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(memcmp(copy, new_value, CELL_SIZE) == 0);
}

/** @brief 读时拿不到锁返回假：读者副本和已见序号都不变，下一次再试能读到。 */
static void test_read_without_lock_fails_and_keeps_reader_state(void)
{
    Fixture f;
    uint8_t src[CELL_SIZE];
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;

    setup(&f, NULL);
    fill_pattern(src, 0x03U);
    ui_cell_write(&f.cell, src);

    memset(copy, 0x5A, sizeof(copy));
    freertos_stub_semaphore_fail_next_takes(module_lock, 1U);
    assert(!ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(seen == 0U);
    for (size_t i = 0U; i < CELL_SIZE; i++)
    {
        assert(copy[i] == 0x5AU);
    }
    assert(freertos_stub_semaphore_stats(module_lock).last_timeout_ticks == 0U); /* 不等。 */

    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(memcmp(copy, src, CELL_SIZE) == 0);
}

/**
 * @brief 阻塞钩子：替“持锁的另一个任务”放锁。
 * @param[in] wait 将要进行的等待。
 * @param[in,out] context 调用计数（uint32_t）。
 */
static void release_module_lock(const FreertosStub_Wait *wait, void *context)
{
    uint32_t *calls = (uint32_t *)context;

    assert(wait->kind == FREERTOS_STUB_WAIT_SEMAPHORE);
    assert(wait->semaphore == module_lock);
    (*calls)++;
    assert(xSemaphoreGive(module_lock) == pdTRUE);
}

/** @brief 等到拿到锁的写：别的任务占着锁时一直等，锁放开后写入成功。 */
static void test_write_waits_for_a_held_lock(void)
{
    Fixture f;
    uint8_t src[CELL_SIZE];
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;
    uint32_t hook_calls = 0U;

    setup(&f, NULL);
    fill_pattern(src, 0x04U);
    assert(xSemaphoreTake(module_lock, 0U) == pdTRUE); /* 别的任务占着锁。 */
    freertos_stub_set_block_hook(release_module_lock, &hook_calls);

    ui_cell_write(&f.cell, src);

    assert(hook_calls == 1U);
    assert(freertos_stub_semaphore_stats(module_lock).last_timeout_ticks == portMAX_DELAY);
    assert(freertos_stub_unsatisfied_infinite_waits() == 0U);
    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(memcmp(copy, src, CELL_SIZE) == 0);
}

/** @brief 每个操作返回时锁都已放开（持锁只在函数内部）。 */
static void test_lock_is_free_after_every_operation(void)
{
    Fixture f;
    uint8_t src[CELL_SIZE];
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;

    setup(&f, NULL);
    fill_pattern(src, 0x05U);

    ui_cell_write(&f.cell, src);
    assert(xSemaphoreTake(module_lock, 0U) == pdTRUE);
    assert(xSemaphoreGive(module_lock) == pdTRUE);

    assert(ui_cell_try_write(&f.cell, src));
    assert(xSemaphoreTake(module_lock, 0U) == pdTRUE);
    assert(xSemaphoreGive(module_lock) == pdTRUE);

    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(xSemaphoreTake(module_lock, 0U) == pdTRUE);
    assert(xSemaphoreGive(module_lock) == pdTRUE);
}

/** @brief 全模块只有一把锁：多个格子共用它，第二个格子初始化不再创建新锁。 */
static void test_all_cells_share_one_lock(void)
{
    Fixture a;
    Fixture b;
    uint8_t src[CELL_SIZE];
    uint32_t takes_before;

    setup(&a, NULL);
    memset(&b, 0, sizeof(b));
    fill_pattern(src, 0x06U);
    ui_cell_init(&b.cell, b.storage, CELL_SIZE, NULL);
    assert(freertos_stub_last_created_semaphore() == NULL); /* reset 之后没有创建过新锁。 */

    takes_before = freertos_stub_semaphore_stats(module_lock).take_calls;
    ui_cell_write(&a.cell, src);
    assert(ui_cell_try_write(&b.cell, src));
    assert(freertos_stub_semaphore_stats(module_lock).take_calls == takes_before + 2U);

    /* 一把锁被占，两个格子的不等写都拿不到。 */
    assert(xSemaphoreTake(module_lock, 0U) == pdTRUE);
    assert(!ui_cell_try_write(&a.cell, src));
    assert(!ui_cell_try_write(&b.cell, src));
    assert(xSemaphoreGive(module_lock) == pdTRUE);
}

/** @brief 写后调用叫醒函数：每次成功写入一次，且在锁已放开之后。 */
static void test_write_calls_wake_once_after_releasing_the_lock(void)
{
    Fixture f;
    uint8_t src[CELL_SIZE];

    setup(&f, count_wake);
    wake_calls = 0U;
    wake_saw_free_lock = false;
    fill_pattern(src, 0x08U);

    ui_cell_write(&f.cell, src);
    assert(wake_calls == 1U);
    assert(wake_saw_free_lock);

    wake_saw_free_lock = false;
    assert(ui_cell_try_write(&f.cell, src));
    assert(wake_calls == 2U);
    assert(wake_saw_free_lock);
}

/** @brief 读不叫醒：只有写会叫醒。 */
static void test_read_does_not_call_wake(void)
{
    Fixture f;
    uint8_t src[CELL_SIZE];
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;

    setup(&f, count_wake);
    fill_pattern(src, 0x09U);
    ui_cell_write(&f.cell, src);
    wake_calls = 0U;

    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(!ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(wake_calls == 0U);
}

/** @brief 叫醒函数传空（结果格）：写入照常成功，不叫醒任何东西。 */
static void test_null_wake_writes_without_waking(void)
{
    Fixture f;
    uint8_t src[CELL_SIZE];
    uint8_t copy[CELL_SIZE];
    uint32_t seen = 0U;

    setup(&f, NULL);
    wake_calls = 0U;
    fill_pattern(src, 0x0AU);

    ui_cell_write(&f.cell, src);
    assert(ui_cell_try_write(&f.cell, src));
    assert(ui_cell_read_if_changed(&f.cell, copy, &seen));
    assert(memcmp(copy, src, CELL_SIZE) == 0);
    assert(wake_calls == 0U);
}

static jmp_buf abort_jump;
static volatile sig_atomic_t abort_seen;

/**
 * @brief SIGABRT 处理：把断言失败变成可检查的事件。
 * @param[in] signal_number 信号编号。
 */
static void on_abort(int signal_number)
{
    (void)signal_number;
    abort_seen = 1;
    longjmp(abort_jump, 1);
}

/** @brief 第二个写者任务写同一个格子触发调试版断言；同一写者反复写、不同格子各有写者都不触发。 */
static void test_second_writer_task_trips_the_debug_assertion(void)
{
    Fixture f;
    Fixture g;
    uint8_t src[CELL_SIZE];
    TaskHandle_t writer_a;
    TaskHandle_t writer_b;
    void (*previous)(int);

    setup(&f, NULL);
    memset(&g, 0, sizeof(g));
    ui_cell_init(&g.cell, g.storage, CELL_SIZE, NULL);
    fill_pattern(src, 0x0BU);
    writer_a = freertos_stub_task_create();
    writer_b = freertos_stub_task_create();

    freertos_stub_set_current_task(writer_a);
    ui_cell_write(&f.cell, src);
    assert(ui_cell_try_write(&f.cell, src)); /* 同一写者，两种写都行。 */
    freertos_stub_set_current_task(writer_b);
    ui_cell_write(&g.cell, src);             /* 另一个格子，另一个写者。 */

    previous = signal(SIGABRT, on_abort);
    assert(previous != SIG_ERR);
    abort_seen = 0;
    if (setjmp(abort_jump) == 0)
    {
        ui_cell_write(&f.cell, src); /* writer_b 写 writer_a 的格子。 */
    }
    assert(abort_seen == 1);

    abort_seen = 0;
    assert(signal(SIGABRT, on_abort) != SIG_ERR); /* Windows 运行库在投递信号时把处理函数复位成默认。 */
    if (setjmp(abort_jump) == 0)
    {
        (void)ui_cell_try_write(&f.cell, src);
    }
    assert(abort_seen == 1);
    (void)signal(SIGABRT, previous);

    /* 断言中途跳出时锁可能仍被持有；让下一个用例从干净状态开始。 */
    freertos_stub_reset();
}

/**
 * @brief 测试入口。
 * @return 0 表示全部通过；失败由断言终止进程。
 */
int main(void)
{
    test_new_reader_gets_initial_value_once();
    test_write_then_read_copies_the_whole_value();
    test_write_takes_a_copy_of_the_source();
    test_unchanged_cell_is_not_copied_and_lock_is_untouched();
    test_sequence_advances_and_reader_sees_latest_only();
    test_rewriting_same_bytes_still_counts_as_a_change();
    test_readers_track_their_own_seen_sequence();
    test_try_write_without_lock_fails_and_leaves_cell_unchanged();
    test_read_without_lock_fails_and_keeps_reader_state();
    test_write_waits_for_a_held_lock();
    test_lock_is_free_after_every_operation();
    test_all_cells_share_one_lock();
    test_write_calls_wake_once_after_releasing_the_lock();
    test_read_does_not_call_wake();
    test_null_wake_writes_without_waking();
    test_second_writer_task_trips_the_debug_assertion();
    puts("ui_cell tests passed");
    return 0;
}
