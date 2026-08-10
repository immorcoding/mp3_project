# 设备错误模型

> 适用工程：`version0.2.3`
>
> 覆盖模块：Audio、AXP2101、SD Card Device
>
> 原则：公共错误表达稳定，后端原始错误不跨越 Port 边界

## 1. 四类值分别表达什么

Audio、AXP2101 和 SD 模块采用相同的诊断结构：

| 值 | 生命周期 | 作用 |
| --- | --- | --- |
| 函数返回状态 | 单次调用 | 表示当前 API 调用成功或失败。 |
| `State` | 持续保存 | 表示 `RESET`、`READY`、`BUSY`、`ERROR`；SD 还包含 `NOT_PRESENT`。 |
| `ErrorCode` | 清除或覆盖前持续保存 | 表示 Device 的哪个语义阶段失败。 |
| `LastBusStatus` / `LastPortStatus` | 清除或覆盖前持续保存 | 表示归一化后的底层结果。 |

`ErrorCode`回答“哪一步失败”；归一化状态回答“底层大致因为什么失败”，例如 `ERROR`、`BUSY`、`TIMEOUT`、`NACK` 或无卡。若错误完全发生在 Device 内部，底层状态保持 `OK`。

HAL 或 SoftI2C 的原始错误位不会复制进 Device 或 Platform 句柄，而是留在具体 Port 句柄中，仅在需要分析特定后端时查看：

- Audio：`hi2s2.ErrorCode`；
- AXP2101 SoftI2C 后端：Platform Power 私有的 `hplatform_power_i2c.ErrorCode`；
- SD Port：`hsd1.ErrorCode`。

各 Port 都先用稳定的 `int32_t native_status` 承接当前 SDK 的立即返回值，再在转换函数内部还原为当前后端的状态类型并映射到 Device 可理解的归一化状态。使用有符号类型也兼容以负数表示错误的其他 SDK。

## 2. Audio 的 `ErrorCode`

| 数值 | 符号 | 含义 |
| ---: | --- | --- |
| 0 | `AUDIO_ERROR_NONE` | 无错误。 |
| 1 | `AUDIO_ERROR_INVALID_PARAM` | 句柄、缓冲区或长度参数非法。 |
| 2 | `AUDIO_ERROR_PORT_NOT_BOUND` | 必需的 Ops、Context 或静音回调没有绑定。 |
| 3 | `AUDIO_ERROR_NOT_READY` | 当前生命周期状态不允许执行该操作。 |
| 4 | `AUDIO_ERROR_BUS_PREPARE` | I2S 或其他音频后端准备失败。 |
| 5 | `AUDIO_ERROR_BUS_TRANSMIT` | 音频数据发送失败。 |
| 6 | `AUDIO_ERROR_MUTE` | 静音或解除静音操作失败。 |

`Audio_BusStatusTypeDef` 包含：

- `AUDIO_BUS_OK`
- `AUDIO_BUS_ERROR`
- `AUDIO_BUS_BUSY`
- `AUDIO_BUS_TIMEOUT`

I2S 没有 I2C 的 NACK 概念，因此 Audio 的归一化总线状态不包含 NACK。

## 3. AXP2101 的 `ErrorCode`

| 数值 | 符号 | 含义 |
| ---: | --- | --- |
| 0 | `AXP2101_ERROR_NONE` | 无错误。 |
| 1 | `AXP2101_ERROR_INVALID_PARAM` | 句柄、地址或配置参数非法。 |
| 2 | `AXP2101_ERROR_PORT_NOT_BOUND` | Bus Ops 或 Bus Context 没有绑定。 |
| 3 | `AXP2101_ERROR_NOT_READY` | 当前生命周期状态不允许执行该操作。 |
| 4 | `AXP2101_ERROR_BUS_PREPARE` | I2C 或其他总线后端准备失败。 |
| 5 | `AXP2101_ERROR_BUS_READ` | 寄存器读取失败。 |
| 6 | `AXP2101_ERROR_BUS_WRITE` | 寄存器写入失败。 |
| 7 | `AXP2101_ERROR_WRONG_CHIP_ID` | 寄存器访问成功，但芯片 ID 不是预期的 AXP2101。 |

`AXP2101_BusStatusTypeDef` 包含：

- `AXP2101_BUS_OK`
- `AXP2101_BUS_ERROR`
- `AXP2101_BUS_BUSY`
- `AXP2101_BUS_TIMEOUT`
- `AXP2101_BUS_NACK`

`LastFailedRegister` 保存最近一次总线失败对应的寄存器地址。

## 4. SD 的 `ErrorCode`

| 数值 | 符号 | 含义 |
| ---: | --- | --- |
| 0 | `SDCARD_ERROR_NONE` | 无错误。 |
| 1 | `SDCARD_ERROR_INVALID_PARAM` | 句柄、缓冲区或块数量参数非法。 |
| 2 | `SDCARD_ERROR_PORT_NOT_BOUND` | Port Ops 或 Port Context 没有绑定。 |
| 3 | `SDCARD_ERROR_NOT_PRESENT` | 当前没有检测到 SD 卡。 |
| 4 | `SDCARD_ERROR_NOT_READY` | 当前生命周期状态不允许执行该操作。 |
| 5 | `SDCARD_ERROR_OUT_OF_RANGE` | 请求的逻辑块范围超出介质容量。 |
| 6 | `SDCARD_ERROR_INVALID_INFO` | Port 返回的块数量或块大小非法。 |
| 7 | `SDCARD_ERROR_PORT_INIT` | 控制器或 SD 卡初始化失败。 |
| 8 | `SDCARD_ERROR_PORT_DEINIT` | 控制器反初始化失败。 |
| 9 | `SDCARD_ERROR_PORT_GET_INFO` | 获取介质信息失败。 |
| 10 | `SDCARD_ERROR_PORT_READ` | 逻辑块读取失败。 |
| 11 | `SDCARD_ERROR_PORT_WRITE` | 逻辑块写入失败。 |
| 12 | `SDCARD_ERROR_PORT_SYNC` | 等待 SD 卡恢复到可传输状态失败。 |

`SDCard_PortStatusTypeDef` 包含：

- `SDCARD_PORT_OK`
- `SDCARD_PORT_ERROR`
- `SDCARD_PORT_BUSY`
- `SDCARD_PORT_TIMEOUT`
- `SDCARD_PORT_NOT_PRESENT`

“没有插卡”可以是正常的持续状态：`SDCard_Init()` 在没有检测到卡时返回 `SDCARD_OK`，并把 `State` 设为 `SDCARD_STATE_NOT_PRESENT`。但在无卡状态下调用读块等无法完成的操作时，函数仍会失败并设置对应错误。

## 5. 推荐的调试器查看顺序

1. 查看 `State`，确认设备当前处于什么生命周期阶段；
2. 查看 `ErrorCode`，定位失败发生在哪个 Device 语义步骤；
3. 查看 `LastBusStatus` 或 `LastPortStatus`，判断底层属于错误、忙、超时、NACK 或无卡；
4. AXP2101 额外查看 `LastFailedRegister`；
5. 只有归一化信息不够时，再查看具体 Port 私有句柄的原始 `ErrorCode`。

上层业务不应依据 HAL 或 SoftI2C 原始错误位编写控制逻辑，因为这样会破坏 Port 隔离。原始错误只用于特定后端的深入调试。
