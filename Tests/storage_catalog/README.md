# Storage 曲库主机测试

本目录通过 `storage_catalog_name.h` 验证曲目显示名纯函数：取最后一个 `/` 之后的部分，只去掉末尾 `.mp3`（`m`、`p` 两位大小写任意，与曲库扫描 `storage_catalog_music_file_strcheck` 接受的形式一致），其余原样保留；只有扩展名的文件（如 `.mp3`）保留整名；主名超过 251 B 或不是合法 UTF-8 时失败，不截断、不写半个字符，失败时输出缓冲为空串。只编译 `storage_catalog_name.c`，不链接 FreeRTOS、FatFs、HAL 或曲库字符串池。

保真边界：PASS 只证明提取规则本身。曲库扫描（`storage_catalog.c`）、名称单槽握手与 Storage 任务外壳不做主机测试，进板级验收；扩展名大小写与扫描代码的一致性靠人工对照，不由测试自动保证。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module storage_catalog
```
