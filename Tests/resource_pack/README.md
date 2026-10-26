# Resource Pack 主机测试

本目录通过 `Components/resource_pack` 的公开 Interface 验证 RPKC1 只读解析行为。测试在内存中构造包数据，当前覆盖合法 BINARY、合法 IMAGE、合法 FONT、未知类型、Header CRC 损坏，以及 Metadata/Data 重叠。

FONT 用例用测试内合成的 lv_font_conv bin（16 位与 32 位 loca、零长度 notdef、空盒子字形）覆盖：合法解码；MetadataSize 不是 32 B；未知 FontFormat、CodepointEncoding 与 MetadataVersion；Metadata 与 bin 的字形数、字号、行高不一致；bin 被截断或标签损坏；位图总量恰为 1 MiB 被拒、少 1 B 被接受、空盒子字形不计入。另有一例读取已入库的 `Resources/fonts/noto_sans_cjk_sc_12_4bpp.bin`，核对 7541 个字形条目（95 个 ASCII、682 个符号、6763 个汉字与 notdef）按设计的 Metadata 解码，防止合成数据与真实 bin 的排布漂移。

测试只编译 Resource Pack Component，不访问 Flash 映射窗口，也不依赖 HAL、Platform、Service、FatFs、LVGL 或 FreeRTOS。Host PASS 只证明协议解析和 FONT 结构核对，不证明字形能被 LVGL 正确渲染，也不证明资源烧录和真实映射生命周期。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module resource_pack
```
