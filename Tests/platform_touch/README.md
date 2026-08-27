# Platform Touch 主机行为测试

本测试只编译 `Platform/touch/platform_touch.c` 与测试目录下的 HAL/FT6X36 Fake，不参与 STM32 目标固件构建，也不访问真实硬件。

它通过 `Platform_Touch_*` 公共 Interface 验证以下可观察行为：

- 初始化成功后，触摸能力可用；
- 初始化失败后，触摸能力不可用，读点不会进入底层 I2C；
- 第一次运行期读点失败后，触摸能力被熔断，后续读点不会重复进入底层 I2C；
- 能力正常时，Platform 原样发布第一触点的按下状态与 X/Y。

在项目根目录执行：

```powershell
cmake -S Tests/platform_touch -B build/host-tests/platform_touch -G Ninja
cmake --build build/host-tests/platform_touch
ctest --test-dir build/host-tests/platform_touch --output-on-failure
```
