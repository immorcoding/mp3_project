# SD Card architecture

## 1. Purpose

This document describes the removable SD-card stack implemented before FatFs is
introduced. The current version provides a reusable block-oriented Device Module,
one STM32H743 SDMMC1 Adapter, and a Board facade for the single physical slot.

The design deliberately does **not** add a generic `BlockDevice` Module yet. SD is
currently the only concrete block Adapter; a common seam should be extracted only
after QSPI Flash provides a second real implementation and its erase/write rules are
known.

## 2. Directory ownership

```text
APP/
  app.c                         optional-media startup policy and logging

BSP/Devices/sd/
  sd.h                          reusable Device Interface and public types
  sd.c                          state machine, validation and error handling
  port/
    sd_port.h                   concrete Port binding function
    sd_port.c                   hsd1, SDMMC1, PC7 and STM32 HAL implementation

BSP/Board/sd/
  board_sd.h                    Board Interface used by APP/future Storage
  board_sd.c                    private Device instance and Board status mapping

Core/Inc/sdmmc.h
Core/Src/sdmmc.c                CubeMX-owned hsd1 and HAL MSP init/deinit
```

`sd.c` must never include `sdmmc.h`, `main.h` or an STM32 HAL header. Hardware
knowledge remains local to `sd_port.c`.

## 3. Runtime call chain

### 3.1 Startup with a card

```text
app_init()
  -> Board_Init()                         mandatory board devices
  -> Board_SD_Init()                      optional removable medium
     -> SDCard_Port_Bind(&hboard_sd)      install Ops + Context
     -> SDCard_Init(&hboard_sd)
        -> IsPresent(PC7)
        -> Port.Init(&hsd1)
           -> HAL_SD_Init(&hsd1)
              -> generated HAL_SD_MspInit()
        -> Port.GetInfo(&hsd1)
        -> cache normalized block information
        -> State = READY
```

### 3.2 Startup without a card

```text
Board_SD_Init()
  -> SDCard_Init()
     -> IsPresent() == false
     -> invalidate cached information
     -> State = NOT_PRESENT
     -> return SDCARD_OK
  -> return BOARD_OK
```

No card is a normal persistent state, not a firmware failure. A later attempt to
read blocks or obtain card information while in `NOT_PRESENT` still fails because
that particular operation cannot be completed.

### 3.3 Synchronous block read

```text
Board_SD_ReadBlocks()
  -> SDCard_ReadBlocks()
     -> validate pointer/count/current state
     -> verify start/count without integer overflow
     -> State = BUSY
     -> Port.ReadBlocks()
        -> HAL_SD_ReadBlocks()
     -> Port.Sync()
        -> poll HAL_SD_GetCardState() until TRANSFER
     -> State = READY
```

Write follows the same chain. Waiting for `TRANSFER` is especially important after
writes because the card may still be programming internal flash after the data phase.

## 4. Status, state and diagnostics

These three concepts have different meanings:

| Value | Meaning |
| --- | --- |
| `SDCard_StatusTypeDef` | Whether one Device function call succeeded. |
| `SDCard_StateTypeDef` | Persistent lifecycle state observed by later calls/debugger. |
| `SDCard_ErrorTypeDef` | Which Device stage most recently failed. |

The Device handle additionally keeps:

| Field | Purpose |
| --- | --- |
| `LastPortStatus` | Backend-independent control-flow category. |
| `PortErrorDetail` | Raw HAL error bits captured at failure time. |
| `Info` | Cached normalized geometry. |
| `IsInfoValid` | Whether `Info` may be copied to a caller. |
| `IsPortInitialized` | Whether deinitialization must release Port resources. |

`Board_SD_GetDiagnostics()` copies the error fields without exposing the private
Device handle.

## 5. State model

```text
RESET
  |-- Init, no card --------------------> NOT_PRESENT
  |-- Init, card and HAL success ------> READY
  `-- Init failure ---------------------> ERROR

NOT_PRESENT
  `-- Refresh after stable insertion --> READY or ERROR

READY
  |-- Read/Write/Sync ------------------> BUSY --> READY
  |-- Port failure ---------------------> ERROR
  `-- Refresh after removal -----------> NOT_PRESENT

ERROR
  |-- explicit Init retry -------------> READY / NOT_PRESENT / ERROR
  `-- Refresh after removal -----------> NOT_PRESENT
```

`SDCard_Refresh()` does not debounce the mechanical card-detect contact and must not
be called from an EXTI ISR. An interrupt may only record an event. APP or a future
Storage task must wait for a stable level before calling Refresh.

## 6. Why the Device Module is not a HAL wrapper

The Device Module earns Depth by hiding rules that would otherwise be duplicated by
APP, FatFs DiskIO and USB MSC callers:

- legal lifecycle ordering;
- card-presence handling;
- cached information validity;
- overflow-safe block-range checks;
- BUSY transitions;
- post-transfer synchronization;
- normalized errors plus raw backend diagnostics;
- cleanup after partial initialization.

If `SDCard_ReadBlocks()` only forwarded to `HAL_SD_ReadBlocks()`, deleting the module
would remove complexity instead of concentrating it, and the module would be shallow.

## 7. CubeMX interaction

CubeMX still owns `hsd1`, `HAL_SD_MspInit()` and `HAL_SD_MspDeInit()` in
`Core/Src/sdmmc.c`. Automatic invocation of `MX_SDMMC1_SD_Init()` remains disabled.

The SD Card Port intentionally calls `HAL_SD_Init()` directly because the generated
`MX_SDMMC1_SD_Init()` returns `void` and enters `Error_Handler()` on failure, which is
not acceptable for removable media.

Consequently the following settings appear in `sdcard_port_init()` and must be
reviewed whenever the CubeMX SDMMC configuration changes:

- clock edge;
- clock power-save setting;
- 4-bit bus width;
- hardware flow control;
- clock divider.

No CubeMX-owned file is modified by this implementation.

## 8. Future FatFs Adapter

FatFs is intentionally not implemented in this version. Its future DiskIO Adapter can
map the existing Board Interface as follows:

| FatFs DiskIO operation | Board SD Interface |
| --- | --- |
| `disk_initialize` | stable detect followed by `Board_SD_Init/Refresh` |
| `disk_status` | `Board_SD_GetState` and `Board_SD_IsPresent` |
| `disk_read` | `Board_SD_ReadBlocks` |
| `disk_write` | `Board_SD_WriteBlocks` |
| `CTRL_SYNC` | `Board_SD_Sync` |
| `GET_SECTOR_COUNT` | `Board_SD_GetInfo().BlockCount` |
| `GET_SECTOR_SIZE` | `Board_SD_GetInfo().BlockSize` |

Filesystem mount state does not belong in `SDCard_StateTypeDef`. Mount/unmount,
open-file invalidation, and ownership arbitration between local FatFs and USB MSC
belong to a future `System/Storage` Module.

## 9. Current limitations

1. Transfers use blocking HAL polling; DMA and interrupt modes are not implemented.
2. No D-Cache maintenance or DMA-accessible-buffer policy exists yet.
3. `SDCard_Refresh()` is available, but APP does not yet implement debounce or EXTI
   event processing.
4. There is no RTOS mutex; callers must not access one Device handle concurrently.
5. Transfer and synchronization timeouts are fixed inside the Device Implementation.
6. The SDMMC initialization values in the SD Card Port must be kept consistent with
   CubeMX configuration.

## 10. Public interfaces

Application code should normally use only `Board_SD_*` functions. The `SDCard_*`
Interface exists for Board assembly, isolated Device tests, and future alternative
Adapters. `hsd1` remains an implementation detail of the SD Card Port.

When the architecture changes, update this document together with `CONTEXT.md`, with
special attention to the call chain, state model, diagnostics, FatFs mapping and
current limitations.
