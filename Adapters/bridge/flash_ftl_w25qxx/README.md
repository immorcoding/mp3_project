# Flash FTL W25Qxx Bridge

本 Bridge 把 W25Qxx Component 已提供的原始 NOR 操作转换为 Flash FTL Component 所拥有的 `FlashFTL_RawOps`。当前仅建立目录和架构约束，尚未创建源代码。

## 预期公开 Interface

- `FlashFTL_W25QxxBridge_Bind()`；
- Bridge 私有的状态转换辅助逻辑。

## 编译期依赖

- `Components/flash_ftl` 定义的 Raw Ops、状态和类型；
- `Components/w25qxx` 的公开 Device Interface、状态和类型。

## 运行时请求路径

Flash FTL 经 `FlashFTL_RawOps` 请求读、编程、擦除、就绪等待与几何信息；本 Bridge 以由 Platform 注入的 W25Qxx Handle 为 Context 调用 W25Qxx 的公开 Interface，并将结果转换为 FTL 的稳定语义。

## 约束

- 不包含 STM32 HAL、CMSIS、CubeMX Handle、FreeRTOS、FatFs、USB MSC、Platform、Service 或 APP；
- 不持有芯片实例、QSPI Context 或 FTL Handle，两个长期对象均由 `Platform/flash` 持有；
- 只在两个真实的 Component Interface 间转换，不能吸收 NOR 映射策略或 QSPI 后端细节。
