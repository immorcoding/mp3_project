# Storage Catalog 主机测试

本目录通过 `storage_playback_cursor.h` 验证播放列表游标的建立、作废、代次、`set` 与环形上一首/下一首。只编译游标源文件，不链接曲库扫描、SDRAM 段、FreeRTOS、HAL 或 GUI。Host PASS 不证明插拔后 Queue 高亮或点按假切歌的真机时序。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module storage_catalog
```
