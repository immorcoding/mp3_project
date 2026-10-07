# ResourcePack Component

本 Module 实现与 MCU、Flash 和业务无关的 RPKC1 只读解析。它显式读取小端字段，打开包时校验 Header CRC、Entry 排序、公共字段、范围、对齐和任意 Metadata/Data 重叠；大型 Data 只在使用前按需校验。

## 公开 Interface

- `ResourcePack_Open()` 打开一段调用者保证可读的内存窗口，不申请动态内存。
- `GetInfo/GetEntryCount/GetEntry/FindEntry` 查询公共头和严格升序 Entry。
- `GetEntryView` 返回映射窗口内的临时只读 Metadata/Data View。
- `VerifyMetadata/VerifyBuffer` 分别校验 Metadata 公共前缀与任意数据 CRC。
- `DecodeBinaryMetadata/DecodeImageMetadata` 解码首版启用的两种 Metadata V1。

## 编译期依赖和运行路径

Implementation 只依赖 ISO C 和本 Module 头文件，不包含 W25Qxx、HAL、Platform、Service、FatFs、LVGL 或 FreeRTOS。运行时由上层把可读内存窗口交给 Component；Component 不发起硬件请求，也不持有窗口生命周期。

## 类型裁剪与约束

`resource_pack_config.h` 只裁剪类型专用 Metadata 解码能力。BINARY 和 IMAGE 默认启用，FONT、AUDIO、MODEL、FIRMWARE 默认关闭；关闭或未知类型不阻止 Core 打开满足公共规则的包，实际解码返回 `RESOURCE_PACK_UNSUPPORTED_TYPE`。

所有整数显式按小端读取，禁止把落盘数据强转为 C 结构体。Handle 不复制 Entry 表，Find 使用二分查找。完整协议、编号空间和兼容规则见 [RPKC1 设计](../../docs/shape/resources.md)。
