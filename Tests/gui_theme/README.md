# GUI 调色板主机测试

本目录验证五个 SquareLine 占位 hex 到 Default/Solid 调色板的映射、上电外观索引，以及壁纸/毛玻璃标志。只编译 `Service/gui/theme/gui_service_theme.c`，不链接 LVGL、FreeRTOS、HAL 或过滤器。Host PASS 不证明真机 RGB565 取色与壁纸显隐。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module gui_theme
```
