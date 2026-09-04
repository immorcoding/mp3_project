# External Loader 主机测试

本目录把 `Tools/external_loader` 的地址窗口几何逻辑接入统一 host Harness。测试只编译 `loader_geometry.c` 与既有 `loader_geometry_test.c`，验证 CubeProgrammer 映射地址、长度和越界转换。

它不编译 `.stldr`、不加载 STM32 HAL，也不访问真实 W25Q256。生产 Loader 的源码、头、CMake、工具链或链接脚本变化仍需交叉构建，并保留 CubeProgrammer 真机验证状态。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module external_loader
```
