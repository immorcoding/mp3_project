# ID3 读取 Component

本 Module 实现与介质、RTOS 和业务无关的 ID3v2 读取：给定调用者读出的字节块及其文件偏移，定位 TIT2（标题）与 TPE1（歌手）帧，把文本负载解码成可显示的 UTF-8。它不认识 SD、FatFs 或任务，不读文件、不分配内存；按格式命名，日后扩展 TALB、APIC、ID3v1 不改名。规则来源是 STOR-15、STOR-16 与 0.8.0 spec（#135 C–E 节）。

## 公开 Interface

- `Id3Reader_Open()`：校验标签头（`ID3`、主版本 2/3/4、syncsafe 大小）并初始化 `Id3Reader_TypeDef`。结果为 `OK`、`NO_TAG`（不是 ID3v2）或 `INVALID`（版本不支持、大小字段非法、v2.2 压缩位）。v2.2/v2.3 的标签级去同步在此就地还原，之后遍历只能在这一块内进行（`SingleBlock`）；v2.3/v2.4 扩展头在此跳过。标签末尾夹到文件长度内。
- `Id3Reader_NextFrame()`：在调用者给的块内逐帧向前，非目标帧只按帧头跳过（不要求负载在块内）。返回 `FRAME`（目标帧，含 `Kind`、`Readable`、编码字节、文本指针与长度）、`NEED_DATA`（给出应从哪个文件偏移重新读一块）或 `END`（标签尾、填充区、非法帧 ID、帧越过标签末尾或 v2.4 帧大小不是 syncsafe）。目标帧负载不完整在块内时要求从帧头重读；重读后仍放不下（负载大于一块）、压缩或加密、负载为空，则 `Readable = false`。v2.4 帧级去同步、分组字节和数据长度指示在帧完整位于块内时还原。支持 v2.2（TT2、TP1）、v2.3（普通大小）、v2.4（syncsafe）。
- `Id3Reader_DecodeField()`：把一个文本帧的负载解码并清理成字段。编码字节 0（Latin-1，或整体合法的 GBK）、1（UTF-16，每个值各自识别并去 BOM，缺 BOM 按 LE）、2（UTF-16BE）、3（UTF-8，开头 BOM 去掉）全收，大于 3 失败；非法 UTF-8、孤立代理、UTF-16 奇数字节长度、C0/C1 控制字符整帧失败，不替换也不续显。清理顺序：按 NUL 拆多值、各值去首尾空白（ASCII 空白与 U+3000）、丢空项、以 ` / ` 连写；超出 `out_bytes` 按字符边界截断、不加省略号，截断后仍继续校验剩余文本。结果 `OK`、`EMPTY`（清理后为空）或 `INVALID`。
- `Id3Reader_GbkLookupFunc`：GBK 双字节码转 Unicode 的查表回调，由调用者提供；`NULL` 表示不做 GBK 判定。编码 0 含 ≥ 0x80 字节时，只有全部高位字节都组成结构合法（首 0x81–0xFE，尾 0x40–0x7E / 0x80–0xFE）且查表非零的双字节才按 GBK 解，否则按 Latin-1。编码 3 不做 GBK 回退。

## 编译期依赖和运行路径

Implementation 只依赖 ISO C 与本 Module 头文件。运行时由上层（Storage 的 `storage_catalog_text`）读文件并驱动 `Open`/`NextFrame` 循环，再对每个目标帧调用 `DecodeField`；GBK 表由上层经 Filesystem Service 的 `Service_Filesystem_GbkToUnicode()` 注入，使 FatFs 仍只在 Filesystem Service 内（ARC-12）。不读 ID3v1/v1.1，不持有状态之外的内存。

## 约束

- 块与输出由调用者持有；`NextFrame` 与 `Open` 会就地改写块（去同步还原），帧指针随块失效。
- v2.4 帧大小严格按 syncsafe 解析，不实现 iTunes 非 syncsafe 回退；遇到时按非法帧结束。
- 本 Module 没有独立测试工程：它只经 `Tests/storage_catalog` 的曲目文本函数接缝被测，见 [主机测试说明](../../Tests/storage_catalog/README.md)。主机 PASS 不证明 FatFs 实际读取、SD 耗时，也不证明板上 CP936 表已就绪。
