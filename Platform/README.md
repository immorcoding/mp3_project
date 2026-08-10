# Platform

Platform 是当前 PCB 的对象装配 Module。它长期持有 Component Handle、Adapter Context 和板级回调节点，将 CubeMX 的实例、GPIO 极性与供电顺序转为上层可理解的产品硬件能力。

## 公开 Interface

- `Platform_Init()`：整机强依赖硬件初始化；
- `Platform_Audio_*`、`Platform_Log_Init()`、`Platform_Power_*`、`Platform_SD_*`：当前板级能力。

## 调用的 Interface

- `Components` 的公开 Interface；
- `Adapters` 的 Bind、注册等装配 Interface；
- CubeMX 的 `h*` Handle 和 `main.h` GPIO 定义。

## 禁止依赖

- 不包含 `APP` 或 `Service` 头，不决定任务调度或文件系统策略；
- 不向上泄漏 HAL Handle、Adapter Context、GPIO 或寄存器；
- 不把短生命周期栈对象绑定到长期持有的 Component Handle。

## 命名

面向上层的产品能力使用 `Platform_<Capability>_<Verb>`；文件内对象和辅助函数使用 `snake_case`。
