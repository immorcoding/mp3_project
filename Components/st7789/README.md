# ST7789 Device

本 Module 封装 ST7789 显示控制器的协议语义。当前实现提供硬件复位、最小 RGB565 显示初始化、`RDDID (0x04)` 读取、画点、纯色矩形填充，以及“地址窗口 + RAMWR 像素 DMA”的异步事务生命周期；不拥有帧缓冲、DMA Handle、Cache 或 LVGL。

## 公开 Interface

- `ST7789_Init()`：按数据手册执行硬件复位时序；
- `ST7789_DisplayInit()`：退出休眠、配置 RGB565、扫描方向和约 40 Hz 的正常模式帧率，并打开显示输出；
- `ST7789_ReadID()`：读取并处理一个 dummy clock 后的 24 位 RDDID 返回值；
- `ST7789_DrawPixel()`、`ST7789_FillRect()`：写入单点或包含边界的 RGB565 矩形；内部私有地使用 CASET、RASET 与 RAMWR；
- `ST7789_SetTransferCallback()`：安装一个异步像素传输最终结果订阅者；回调由 Adapter 的 SPI ISR 触发，只能执行常数时间工作；
- `ST7789_StartWrite()`：同步设置 CASET/RASET/RAMWR，再异步启动完整 RGB565 矩形的连续像素写入；成功返回仅表示 DMA 已开始，最终结果由回调发布；
- `ST7789_PortOpsTypeDef`：由具体 Adapter 实现的串行传输、控制引脚和延时 Interface。

## 私有配置

- `st7789_config.h` 集中保存当前 ST7789V 的命令字、RGB565 初始化参数、复位/显示稳定时序和阻塞纯色填充块大小；仅 `st7789.c` 可包含它。
- `ST7789_WIDTH`、`ST7789_HEIGHT` 留在 `st7789.h`，因为它们描述调用者可见的稳定显示几何，而不是具体初始化策略。

## 编译期依赖

仅调用自身定义的 `ST7789_PortOpsTypeDef`，不包含 HAL、CMSIS、FreeRTOS 或板级头文件。

## 运行时请求与事件路径

ST7789 Device 通过自身拥有的 `ST7789_PortOpsTypeDef` 发起串行传输、控制线和延时请求；具体 Adapter 实现 Ops，Platform 负责绑定 Context。异步像素路径为：调用者请求 `ST7789_StartWrite()` → Device 保持 CS、设置地址窗口并启动 Adapter → Adapter 在最终 SPI EOT 回调中发布结果 → Device 释放 CS、恢复状态并调用唯一订阅者。该事件链不包含任务通知或 GUI 逻辑。

## 禁止依赖

不得包含 STM32 Handle、GPIO、SPI、任务通知、日志或本板 LCD 引脚；它们分别属于 Adapter、Platform 和 APP。

## 命名

跨 Module 的 Device Interface 使用 `ST7789_*`；文件内私有 Implementation 使用 `st7789_*`。
