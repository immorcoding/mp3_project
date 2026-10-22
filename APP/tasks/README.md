# APP Tasks

本目录包含由 `app_task_start()` 创建的 FreeRTOS Task 入口和任务规格。任务创建顺序与名称在 `app_tasks.c`，栈深度和优先级集中在 `APP/app_config.h`，各叶目录只维护本任务的运行逻辑。

## APP 内部 Interface

- `app_task_start()`：由 `APP/app.c` 创建 bootstrap task 并启动调度器；
- `log_task()`、`storage_task()`、`playback_task()`、`monitor_task()`、`gui_task()`：仅作为 FreeRTOS `TaskFunction_t` 交给创建器。播放任务用 `xTaskCreateStatic` 创建（TCB 与栈在 `app_tasks.c` 的 DTCM `.bss`），其余任务仍用 `xTaskCreate`；
- `playback_command_send()`、`playback_snapshot_read()`：播放任务公开合同（`playback/playback_contract.h`），只给 GUI 音乐分区和模拟器脚本化后端使用，见 [播放任务](playback/README.md)。

本目录不是独立的跨 Module Interface；其头文件和任务入口只服务于顶层 `APP` Module，其他层不得包含或调用。

## 编译期依赖

- 同目录任务配置宏（栈与优先级在 `APP/app_config.h`）；
- `Service`、`Platform` 的公开 Interface；
- FreeRTOS Task、通知和诊断 Interface。

## 运行时请求与事件路径

Task 是 APP 创建后的运行时执行上下文：任务通过 Service/Platform 的公开 Interface 发起请求，硬件 ISR 只经已注册回调或 FromISR 原语唤醒对应任务。任务目录不拥有 HAL ISR 入口。

## 任务与优先级

| 任务 | 优先级 | 栈（word） | 创建方式 | 职责 |
|---|---|---|---|---|
| Playback Task | 3 | 5120 | `xTaskCreateStatic`，DTCM | 唯一的播放状态机；解码、PCM 半区补满与提交、快照发布 |
| Storage Task | 2 | 1024 | `xTaskCreate` | SD 热插拔、曲库、FatFs、Flash 回收、音频流读块 |
| GUI Task | 1 | 2048 | `xTaskCreate` | LVGL 唯一执行上下文 |
| Log Task | 1 | 512 | `xTaskCreate` | 日志消费 |
| Monitor Task | 1 | 256 | `xTaskCreate` | 结温与板级监控 |
| Boot（Create Task） | 0 | 128 | `xTaskCreate` | 创建其余任务后自删 |

排序依据（[#71](https://github.com/immorcoding/mp3_project/issues/71) A6、B1、B2；[#62](https://github.com/immorcoding/mp3_project/issues/62)）：

- **播放最高**：PCM 半区只有约 24 ms（1152 帧，48 kHz）的补满期限，期限只允许受中断与播放任务自身工作（有上界的一次唤醒，不含 FatFs/LVGL、不同步等待 Storage、发送不阻塞）影响。Storage 的一次 FatFs 读取（含 SD DMA 与可能的单分块 500 ms 超时）和 GUI 的一帧 `lv_timer_handler` 都可能远长于期限，所以播放任务必须能抢占它们；与这些长任务的关系只靠优先级，GUI 和 Storage 不加让出点，播放任务也不与低优先级任务共用互斥锁。
- **Storage 高于 GUI/Log/Monitor**：块池缓冲有限，Storage 要在播放消耗完块之前及时补块；GUI 的帧循环不应拖慢读块。
- **GUI、Log、Monitor 同级 1**：时间片轮转，互不依赖。
- **Boot 最低**：只在启动时创建任务。
- **定时器服务任务同为 3**：`configTIMER_TASK_PRIORITY` 为 3，应用目前没有软件定时器；引入软件定时器时回调须很短，否则复核两者优先级。`FreeRTOSConfig.h` 的优先级相关配置不改。

启动顺序：Playback Task 晚于 Storage Task 创建，使 Storage 已完成 `storage_audio_stream_init()`（播放任务此前调用请求信箱会被拒绝）；Playback Task 先于 GUI Task 创建，使命令 FIFO、状态信箱与回调注册先就绪。

## 约束

- 任务入口不得返回；
- Task 之间不能共享 FatFs 对象或绕过其资源所有者；播放任务不碰 FatFs，音频文件的打开、读取、关闭只在 Storage Task；
- ISR 只唤醒任务，不能把文件系统、日志格式化或设备初始化移入中断。

## 命名

任务入口和内部 Implementation 使用 `snake_case`；栈/优先级宏以 `APP_<TASK>_...` 命名。`APP/tasks` 是 APP 的私有分区，不因目录存在而形成面向其他 Module 的公开 Interface。
