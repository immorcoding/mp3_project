# Device error model

## 1. Four different values

The Audio, PMIC and SD modules use the same diagnostic model:

| Value | Lifetime | Purpose |
| --- | --- | --- |
| Function status | One call | Reports whether the current API call succeeded. |
| `State` | Persistent | Describes RESET, READY, BUSY, ERROR, and for SD, NOT_PRESENT. |
| `ErrorCode` | Until cleared/overwritten | Identifies the semantic stage that failed. |
| `LastBusStatus` / `LastPortStatus` | Until cleared/overwritten | Identifies the normalized lower-layer result. |

`ErrorCode` answers “which operation failed?”. The normalized transport status
answers “did the lower layer report ERROR, BUSY, TIMEOUT, NACK, or no card?”.
When a failure is entirely inside the Device layer, the transport status remains
`OK`.

Raw HAL or SoftI2C error bits are not copied into Device or Board handles. They
remain in the concrete Port handle and are inspected only for backend-specific
debugging:

- Audio: `hi2s2.ErrorCode`;
- PMIC SoftI2C Port: `hpmic_i2c.ErrorCode`;
- SD Port: `hsd1.ErrorCode`.

## 2. Audio ErrorCode

| Value | Symbol | Meaning |
| ---: | --- | --- |
| 0 | `AUDIO_ERROR_NONE` | No error. |
| 1 | `AUDIO_ERROR_INVALID_PARAM` | Invalid buffer, length, or handle-related parameter. |
| 2 | `AUDIO_ERROR_PORT_NOT_BOUND` | Required Ops, Context, or mute callback is missing. |
| 3 | `AUDIO_ERROR_NOT_READY` | Current lifecycle state does not allow the operation. |
| 4 | `AUDIO_ERROR_BUS_PREPARE` | I2S/backend preparation failed. |
| 5 | `AUDIO_ERROR_BUS_TRANSMIT` | Audio data transmission failed. |
| 6 | `AUDIO_ERROR_MUTE` | Mute/unmute operation failed. |

`Audio_BusStatusTypeDef` values are `AUDIO_BUS_OK`, `AUDIO_BUS_ERROR`,
`AUDIO_BUS_BUSY`, and `AUDIO_BUS_TIMEOUT`. I2S has no NACK concept.

## 3. PMIC ErrorCode

| Value | Symbol | Meaning |
| ---: | --- | --- |
| 0 | `PMIC_ERROR_NONE` | No error. |
| 1 | `PMIC_ERROR_INVALID_PARAM` | Invalid handle, address, or configuration. |
| 2 | `PMIC_ERROR_PORT_NOT_BOUND` | Bus Ops or Context is missing. |
| 3 | `PMIC_ERROR_NOT_READY` | Current lifecycle state does not allow the operation. |
| 4 | `PMIC_ERROR_BUS_PREPARE` | I2C/backend preparation failed. |
| 5 | `PMIC_ERROR_BUS_READ` | Register read failed. |
| 6 | `PMIC_ERROR_BUS_WRITE` | Register write failed. |
| 7 | `PMIC_ERROR_WRONG_CHIP_ID` | Register access succeeded, but the chip ID is not AXP2101. |

`PMIC_BusStatusTypeDef` values are `PMIC_BUS_OK`, `PMIC_BUS_ERROR`,
`PMIC_BUS_BUSY`, `PMIC_BUS_TIMEOUT`, and `PMIC_BUS_NACK`.
`LastFailedRegister` records the associated register address.

## 4. SD ErrorCode

| Value | Symbol | Meaning |
| ---: | --- | --- |
| 0 | `SDCARD_ERROR_NONE` | No error. |
| 1 | `SDCARD_ERROR_INVALID_PARAM` | Invalid buffer, count, or handle-related parameter. |
| 2 | `SDCARD_ERROR_PORT_NOT_BOUND` | Port Ops or Context is missing. |
| 3 | `SDCARD_ERROR_NOT_PRESENT` | No card is currently detected. |
| 4 | `SDCARD_ERROR_NOT_READY` | Current lifecycle state does not allow the operation. |
| 5 | `SDCARD_ERROR_OUT_OF_RANGE` | Requested logical-block range exceeds the medium. |
| 6 | `SDCARD_ERROR_INVALID_INFO` | Port returned invalid block geometry. |
| 7 | `SDCARD_ERROR_PORT_INIT` | Controller/card initialization failed. |
| 8 | `SDCARD_ERROR_PORT_DEINIT` | Controller deinitialization failed. |
| 9 | `SDCARD_ERROR_PORT_GET_INFO` | Reading medium information failed. |
| 10 | `SDCARD_ERROR_PORT_READ` | Block read failed. |
| 11 | `SDCARD_ERROR_PORT_WRITE` | Block write failed. |
| 12 | `SDCARD_ERROR_PORT_SYNC` | Waiting for the card to return to transfer-ready failed. |

`SDCard_PortStatusTypeDef` values are `SDCARD_PORT_OK`,
`SDCARD_PORT_ERROR`, `SDCARD_PORT_BUSY`, `SDCARD_PORT_TIMEOUT`, and
`SDCARD_PORT_NOT_PRESENT`.

## 5. Recommended debugger order

1. Inspect `State`.
2. Inspect `ErrorCode`.
3. Inspect `LastBusStatus` or `LastPortStatus`.
4. For PMIC, inspect `LastFailedRegister`.
5. Only if the normalized values are insufficient, inspect the concrete Port
   handle's raw `ErrorCode`.
