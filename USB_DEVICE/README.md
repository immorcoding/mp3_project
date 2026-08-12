# USB_DEVICE

本目录由 CubeMX 生成 USB Device glue，包括 CDC 类回调和描述符。产品日志使用 `Adapters/stm32_hal/log_usb_cdc` 通过其公开 CDC Interface 接入。

## 接缝与约束

- 自维护修改仅放入 USER CODE 区；
- `CDC_Transmit_FS()` 与 `CDC_IsReady_FS()` 是 USB 日志 Adapter 的下层 Interface；
- 不在生成的 CDC 回调中直接调用 LogService、FatFs 或产品任务；
- 重新生成后验证 DTR/就绪检测的 USER CODE 是否仍完整。
