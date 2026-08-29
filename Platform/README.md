# Platform

Platform 是当前 PCB 的对象装配 Module。它长期持有 Component Handle、Adapter Context 和板级回调节点，将 CubeMX 的实例、GPIO 极性与供电顺序转为上层可理解的产品硬件能力。

## 公开 Interface

- `Platform_Init()`：整机硬件初始化；当前顺序为 SDRAM、PMIC、W25Q256 Flash、音频、LCD、LED、Touch，其中 LCD 与 Touch 共用 ALDO2，必须先 LCD 后 Touch；LED 失败只降级诊断能力；
- `Platform_Audio_*`、`Platform_Flash_*`、`Platform_LCD_*`、`Platform_LED_*`、`Platform_Log_Init()`、`Platform_Power_*`、`Platform_SD_*`、`Platform_SDRAM_*`、`Platform_Temp_*`、`Platform_Touch_*`：当前板级能力。

## 编译期依赖与装配

- `Components` 的公开 Interface；
- `Adapters` 的 Bind、注册等装配 Interface；
- CubeMX 的 `h*` Handle 和 `main.h` GPIO 定义。

Platform 的 `.c` 可以包含 Component、Adapter 与 CubeMX 的公开/装配头，以持有长期实例并执行 Bind；这种编译期依赖不等于上层业务调用 Platform。

## 运行时请求路径

Platform 对 APP 和 Service 提供 `Platform_*` 产品能力；它在内部向下调用 Component，Component 再通过已绑定 Adapter 到达具体后端。`Platform_Init()` 是启动期唯一的硬件生命周期入口，APP 只消费其结果并决定设备失败是否致命；Platform 装配实例不是每次请求的必经步骤。

## 事件/ISR 路径

Platform 可以长期持有 Adapter 回调节点，并把源特定硬件事件转发给已注册上层回调。它不解释任务通知索引，不调用 FreeRTOS、FatFs 或产品业务；事件语义与任务处理权属于订阅者。

## 禁止依赖

- 不包含 `APP` 或 `Service` 头，不决定任务调度或文件系统策略；
- 不向上泄漏 HAL Handle、Adapter Context、GPIO 或寄存器；
- 不把短生命周期栈对象绑定到长期持有的 Component Handle。

## 命名

面向上层的产品能力使用 `Platform_<Capability>_<Verb>`；文件内对象和辅助函数使用 `snake_case`。
