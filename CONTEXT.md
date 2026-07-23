# Domain context

## Power application

The **Power application** is the early firmware slice that proves board power, initializes the AXP2101, and keeps lightweight diagnostics running before higher-level MP3 features exist.

It owns startup order and periodic work through `APP/app.c`, while hardware details remain in BSP and reusable services remain in `System`.

Related terms: **Board PMIC**, **PMIC boot profile**, **deferred startup log**.

Example dialogue:

> “Add SD card initialization to the Power application after PMIC setup, but keep the SDMMC driver out of APP.”

## Board PMIC

The **Board PMIC** is the single AXP2101 instance physically fitted to this PCB, represented by the private `hpmic` object in `BSP/Board/board_pmic.c`.

It is not the generic AXP2101 driver and not the software-I2C instance. The Board layer binds the selected bus Port to the generic PMIC Device driver and exposes only `Board_PMIC_Init()` upward.

Related terms: **PMIC I2C Port**, **PMIC boot profile**.

Example dialogue:

> “The Board PMIC should use hardware I2C, but the PMIC Device API must remain unchanged.”

## PMIC I2C Port

The **PMIC I2C Port** is the adapter in `BSP/Devices/pmic/port` that translates one concrete I2C backend into `PMIC_BusOpsTypeDef` plus its matching `BusContext`.

The current backend is `SoftI2C`. Replacing it with HAL I2C should be confined to the marked backend regions; Board and Device layers continue to call the same interface.

Related terms: **Board PMIC**, **normalized transport status**.

Example dialogue:

> “Map HAL_TIMEOUT to PMIC_BUS_TIMEOUT inside the PMIC I2C Port.”

## PMIC boot profile

The **PMIC boot profile** is the ordered, driver-private AXP2101 register policy applied by `PMIC_Init()` after the chip ID has been verified.

It controls input limits, charging, interrupt state, ADC channels, DCDC/LDO enable state, and preset voltages. It is a power-policy change, not merely a software refactor, so changes require datasheet, schematic, load-voltage, and bench verification.

Related terms: **Board PMIC**, **Power application**.

Example dialogue:

> “Change the PMIC boot profile so ALDO2 starts disabled but retains its 3.3 V preset.”

## Normalized transport status

The **normalized transport status** is the backend-independent result stored as
`LastBusStatus` or `LastPortStatus`, such as OK, BUSY, TIMEOUT, NACK, or
NOT_PRESENT.

Device `ErrorCode` identifies the semantic failure stage, while the normalized
transport status explains the broad lower-layer reason. Backend-specific raw error
bits stay in the concrete Port handle (`hpmic_i2c`, `hsd1`, or `hi2s2`) and do not
cross the Port seam.

Related terms: **PMIC I2C Port**, **SD Card Port**, **Board PMIC**.

Example dialogue:

> “PMIC_ERROR_BUS_READ identifies the stage; PMIC_BUS_NACK identifies the
> portable transport reason; the SoftI2C raw bit remains private to the Port.”

## Deferred startup log

A **deferred startup log** is a formatted message generated during initialization and copied into the fixed RAM queue before the host has opened USB CDC.

`LOG_Process()` later submits it after enumeration, DTR assertion, the port-open settle interval, and CDC transmit-idle checks all pass. Enqueue success does not mean the PC has already displayed the message.

Related terms: **Power application**, **USB CDC log backend**.

Example dialogue:

> “Keep the deferred startup logs queued until VSCode opens the COM port.”

## USB CDC log backend

The **USB CDC log backend** is the logging Adapter in
`System/Log/backends/log_backend_usb_cdc.c`. It translates the ST USB Device
status returned by `CDC_Transmit_FS()` into `LOG_OutputStatusTypeDef`, owns the
persistent asynchronous transmit buffer, adds optional ANSI color, and preserves
the last submitted message for debugger inspection.

The logging Core crosses the Backend seam only through `LOG_OutputOpsTypeDef`.
USB enumeration, DTR, `TxState`, and ST status values remain private to this
Adapter. `LOG_Backend_BindDefault()` is the internal composition point that binds
the current USB CDC Adapter and HAL millisecond time source to the private default
log handle.

Related terms: **Deferred startup log**, **Normalized transport status**.

Example dialogue:

> “Map USBD_BUSY to LOG_OUTPUT_BUSY inside the USB CDC log backend without
> exposing ST USB types to log.c.”

## Board SD

The **Board SD** is the single removable SD slot fitted to this PCB, represented by
a private Device handle in `BSP/Board/sd/board_sd.c` and connected to SDMMC1 with an
active-low card-detect signal on PC7.

Its current bare-metal hotplug path uses a binary ISR notification and restartable
30 ms deferred debounce. `Board_SD_Process()` converts only stable Device state
changes into `INSERTED` or `REMOVED`; mechanical edge counts are not part of the
domain. Marked regions in `board_sd.c` form the future RTOS scheduling seam, where
FreeRTOS task notification can replace polling without changing Device refresh or
Board event semantics.

Related terms: **SD Card Device**, **SD Card Port**, **Power application**.

Example dialogue:

> “Board SD reports NOT_PRESENT as a normal removable-media state, so the Power application can continue booting without a card.”

## SD Card Device

The **SD Card Device** is the reusable block-oriented module that owns SD media state, normalized information, range validation, synchronous block access, and error snapshots without depending on STM32 HAL types.

Related terms: **Board SD**, **SD Card Port**.

Example dialogue:

> “FatFs should eventually consume the SD Card Device's block semantics through a storage adapter rather than call HAL_SD_ReadBlocks directly.”

## SD Card Port

The **SD Card Port** is the adapter in `BSP/Devices/sd/port` that binds the SD Card Device to this PCB's `hsd1`, SDMMC1 configuration, active-low PC7 card detect, and normalized STM32 HAL results.

Related terms: **Board SD**, **SD Card Device**.

Example dialogue:

> “A future SPI SD implementation can replace the SD Card Port backend while preserving the SD Card Device interface.”
