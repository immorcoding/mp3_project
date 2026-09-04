# Resource Pack 主机测试

本目录通过 `Components/resource_pack` 的公开 Interface 验证 RPKC1 只读解析行为。测试在内存中构造包数据，当前覆盖合法 BINARY、合法 IMAGE、未知类型、Header CRC 损坏，以及 Metadata/Data 重叠。

测试只编译 Resource Pack Component，不访问 Flash 映射窗口，也不依赖 HAL、Platform、Service、FatFs、LVGL 或 FreeRTOS。Host PASS 只证明协议解析，不证明资源烧录和真实映射生命周期。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module resource_pack
```
