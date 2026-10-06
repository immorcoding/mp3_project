# ADR-0002：分层关系分离与 Interface 所有权

- 状态：已接受（按现有代码追溯记录）
- 日期：2026-08-29
- 相关实现说明：[../architecture_standard.md](../architecture_standard.md)、[../../GLOSSARY.md](../../GLOSSARY.md)

## 背景

固件同时存在功能/抽象所有权、编译期依赖和运行时调用路径。若把它们当作同一张图，容易得出“Component 不能被 Adapter 包含”或“每次请求必须逐层经过所有 Module”的错误结论。

## 决定

1. `Vendor/HAL → Adapters → Components → Platform → Service → APP` 只表达功能与抽象所有权。
2. 需要外部能力并经 Ops 调用它的 Component 拥有该 Interface；Adapter 实现该 Interface；Platform 长期持有并绑定 Ops 与 Context。
3. `Adapters/bridge/` 只连接两个 Component Interface；`Adapters/stm32_hal/` 集中 HAL、CubeMX Handle 与 HAL 全局回调实现；两者不得混用。
4. Component 不包含 Adapter、Platform、HAL、Service 或 APP；Platform 可以包含 Component、Adapter 与 CubeMX 头，仅用于本板装配。
5. Ops 与 Context 必须成对绑定，且 Context 生命周期覆盖整个 Component 调用期。

## 后果

- Component 可以绑定 Fake Adapter 进行主机测试，替换 MCU 后端时保持局部修改；
- 跨 Component Bridge 与 HAL Adapter 的目录含义明确，避免为了“分层”额外创建无收益的转调 Module；
- 技术文档的运行时箭头不再被误读为 `#include` 方向。
