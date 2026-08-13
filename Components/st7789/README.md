# ST7789 Device

本 Module 封装 ST7789 显示控制器的协议语义。当前实现只提供硬件复位与 `RDDID (0x04)` 的 24 位芯片标识读取；不包含初始化寄存器表、帧缓冲、DMA 或 LVGL。

## 公开 Interface

- `ST7789_Init()`：按数据手册执行硬件复位时序；
- `ST7789_ReadID()`：读取并处理一个 dummy clock 后的 24 位 RDDID 返回值；
- `ST7789_PortOpsTypeDef`：由具体 Adapter 实现的串行传输、控制引脚和延时 Interface。

## 编译期依赖

仅调用自身定义的 `ST7789_PortOpsTypeDef`，不包含 HAL、CMSIS、FreeRTOS 或板级头文件。

## 运行时请求与事件路径

ST7789 Device 通过自身拥有的 `ST7789_PortOpsTypeDef` 发起串行传输、控制线和延时请求；具体 Adapter 实现 Ops，Platform 负责绑定 Context。当前没有控制器异步事件或 ISR 路径。

## 禁止依赖

不得包含 STM32 Handle、GPIO、SPI、任务通知、日志或本板 LCD 引脚；它们分别属于 Adapter、Platform 和 APP。

## 命名

跨 Module 的 Device Interface 使用 `ST7789_*`；文件内私有 Implementation 使用 `st7789_*`。
