# Component Bridge Adapters

本目录放置两个 Component Interface 之间的 Adapter。它们不依赖具体 MCU、HAL、CMSIS 或 FreeRTOS；职责是把一个 Component 已提供的能力，转换为另一个 Component 所拥有的 Ops Interface。

## 公开 Interface

- 各子目录声明的 `*_Bind()` Interface；
- 需要时由 Platform 长期持有、用于连接两个 Component Handle 的 Adapter Context；若 Bridge 不需要额外状态，则源 Component Handle 本身就是回调 Context。

## 编译期依赖

- 上游 Component 提供的操作和状态类型；
- 下游 Component 定义的 Ops Interface；
- 同一 Bridge Module 的私有状态转换辅助函数。

## 运行时请求与事件路径

Bridge 在目标 Component 经其 Ops 发起调用时，转而调用源 Component 的公开 Interface；Platform 只在装配期注入两个长期 Handle。Bridge 不拥有硬件 ISR 或任务事件。

## 约束

- 不包含 HAL、CMSIS、CubeMX Handle、FreeRTOS、`APP` 或 `Service` 头文件；
- 不选择具体 MCU 后端或本 PCB 实例，两个 Handle 都由 Platform 注入；
- 只在两个真实、可替换的 Component Interface 之间建立 Adapter；不得把单纯的函数转调包装成无收益目录。
