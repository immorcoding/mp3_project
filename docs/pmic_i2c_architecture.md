# AXP2101 与板级电源架构

> 适用工程：`version0.1.2`
>
> 当前器件：AXP2101
>
> 当前通信后端：阻塞式 SoftI2C
>
> 原则：芯片协议、通信适配和 PCB 电源策略分别归属 Device、Adapter 和 Platform

## 1. 重构目的

旧实现以“通用 PMIC”为名，但实际公开类型、寄存器、启动配置和 ALDO 控制全部来自
AXP2101。这会形成虚假的通用层：调用者看似依赖 PMIC 接口，实际上仍被 AXP2101
语义绑定。

当前实现采用诚实的三层结构：

```text
APP
  -> Platform Power
       -> AXP2101 Device
       -> AXP2101 SoftI2C Adapter
            -> SoftI2C Component
                 -> STM32 GPIO Adapter
```

- AXP2101 Device 明确表达芯片型号及寄存器语义；
- Adapter 只把 SoftI2C 转换成 AXP2101 Bus Ops；
- Platform Power 拥有本板实例、引脚、总线参数和电源启动策略；
- APP 只使用 Audio、LCD 等板级供电语义。

只有以后出现第二种真实 PMIC，并且两种芯片确实需要共同的上层电源能力时，才从
`Platform_Power_*` 需求中提取通用 `Power Manager Interface`。当前不提前制造抽象。

## 2. 目录和所有权

```text
Components/
  soft_i2c/
    soft_i2c.c
    soft_i2c.h
  axp2101/
    axp2101.c
    axp2101.h
    axp2101_regs.h

Adapters/
  soft_i2c/
    soft_i2c_stm32_hal_adapter.c
    soft_i2c_stm32_hal_adapter.h
  axp2101_soft_i2c/
    axp2101_soft_i2c_adapter.c
    axp2101_soft_i2c_adapter.h

Platform/
  power/
    platform_power.c
    platform_power.h
```

| 模块 | 拥有 | 不拥有 |
| --- | --- | --- |
| SoftI2C Component | GPIO 开漏时序、普通收发、寄存器收发、恢复和原始错误位 | STM32 类型、具体引脚、AXP2101 地址和寄存器 |
| STM32 GPIO Adapter | 把 SCL/SDA 的拉低、释放和读电平映射到 HAL GPIO | I2C 协议、AXP2101、板级启动策略 |
| AXP2101 Device | 芯片 ID、寄存器访问、ALDO 控制、Device 状态和稳定错误 | GPIO、HAL Handle、本板供电策略 |
| AXP2101 SoftI2C Adapter | Bus Ops 表、SoftI2C 到 AXP2101 状态的转换 | SoftI2C 实例、引脚、启动配置 |
| Platform Power | 本板唯一实例、引脚、时序参数、启动配置、Audio/LCD 电源映射 | SoftI2C 位操作、应用日志和产品流程 |

`axp2101_regs.h` 属于芯片驱动，不能放在 Adapter。Adapter 替换后，芯片寄存器定义
仍然有效；这正是判断所有权的简单方法。

## 3. 上电调用链

```text
Platform_Init()
  -> Platform_Power_Init()
       -> SoftI2C_STM32HALAdapter_Bind(&hplatform_power_i2c,
                                       &hplatform_power_gpio,
                                       delay,
                                       timeout)
            -> hplatform_power_i2c.GPIOOps = STM32 GPIO Ops
            -> hplatform_power_i2c.GPIOContext = &hplatform_power_gpio
       -> AXP2101_SoftI2CAdapter_Bind(&hplatform_power,
                                      &hplatform_power_i2c)
            -> hplatform_power.BusOps = &axp2101_soft_i2c_ops
            -> hplatform_power.BusContext = &hplatform_power_i2c
       -> AXP2101_Init(&hplatform_power)
            -> BusOps->Prepare()
                 -> SoftI2C_Init()
            -> 读取 IC_TYPE
            -> 校验芯片 ID
            -> State = READY
       -> AXP2101_ApplyConfiguration(...)
            -> 按 Platform 启动表逐项直接写或读改写
  -> Platform_Power_SetAudio(true)
       -> AXP2101_SetALDO1Enabled(...)
```

两个绑定函数都只安装 Ops、Context 和静态配置，不产生 I2C 波形。GPIO 模式仍由
CubeMX 初始化；真正访问总线从 `AXP2101_Init()` 调用 `Prepare()` 开始。

## 4. C 语言对象模型

### 4.1 Handle 是对象实例

`AXP2101_HandleTypeDef` 保存：

| 字段 | 含义 |
| --- | --- |
| `BusOps` | 当前通信后端的函数表。 |
| `BusContext` | 与该函数表配套的实例指针。 |
| `Address7Bit` | AXP2101 的 7 位地址。 |
| `State` | RESET、READY、BUSY 或 ERROR。 |
| `ErrorCode` | 最近失败发生在哪个 Device 语义阶段。 |
| `LastBusStatus` | 最近失败的归一化底层原因。 |
| `LastFailedRegister` | 最近失败的寄存器地址。 |

Platform 私有的 `hplatform_power` 是本板上的 AXP2101 对象。它使用 `static`，应用不能
直接改写其内部字段。

### 4.2 Ops 表实现运行期多态

`AXP2101_BusOpsTypeDef` 包含 `Prepare`、`MemRead`、`MemWrite`。Device 只通过
这些函数指针访问总线，因此同一份 `axp2101.c` 可以配合：

- 当前 SoftI2C Adapter；
- 未来 HAL I2C Adapter；
- 单元测试使用的内存模拟 Adapter。

函数指针相当于 C 语言的虚函数，`BusContext` 相当于传给实现的 `this`。Ops 和
Context 必须成对绑定，否则函数实现会按错误的类型解释 Context。

### 4.3 组合代替伪继承

当前没有通过“基类结构体首成员”模拟继承。Platform Power 组合一个 AXP2101 Handle、
一个 SoftI2C Handle 和一个 STM32 GPIO Adapter Context，关系更清晰：

```text
Platform Power owns AXP2101
Platform Power owns SoftI2C
Platform Power owns STM32 GPIO Adapter Context
AXP2101 refers to SoftI2C through Adapter Ops + Context
SoftI2C refers to STM32 GPIO through GPIO Ops + Context
```

## 5. 启动配置为何属于 Platform

`platform_power_boot_profile[]` 包含输入限制、充电、ADC、中断、DCDC/LDO 使能和
电压预设。相同 AXP2101 放到另一块板上时，这些值可能完全不同，所以它不是芯片
驱动的默认行为。

配置项结构：

| 字段 | 含义 |
| --- | --- |
| `Register` | 目标 AXP2101 寄存器。 |
| `Mask` | 允许修改的位；`0xFF` 表示完整覆盖。 |
| `Value` | 需要写入 Mask 覆盖位的值。 |

`AXP2101_ApplyConfiguration()` 按数组顺序执行。Mask 不是 `0xFF` 时采用：

```text
new = (old & ~mask) | (value & mask)
```

修改启动表属于硬件电源策略变更，必须同步核对原理图、AXP2101 数据手册、负载
允许电压、上电顺序和实测结果，不能按普通代码重构处理。

## 6. Audio 与 LCD 电源语义

当前 PCB 映射：

| Platform 语义 | AXP2101 电源轨 |
| --- | --- |
| `Platform_Power_SetAudio()` | ALDO1 |
| `Platform_Power_SetLCD()` | ALDO2 |

APP 不应直接调用 `AXP2101_SetALDO1Enabled()`。如果下一版 PCB 将音频电源改接
到其他电源轨，只修改 Platform Power 映射，上层 Audio 流程不需要变化。

## 7. 状态与错误模型

一次 API 调用的返回值只表示本次成功或失败；Handle 中的字段用于持续诊断：

```text
State
  -> 当前生命周期

ErrorCode
  -> 哪个 AXP2101 语义步骤失败

LastBusStatus
  -> 底层大致因为什么失败

LastFailedRegister
  -> 哪个寄存器发生失败
```

`AXP2101_ErrorTypeDef`：

| 值 | 含义 |
| --- | --- |
| `AXP2101_ERROR_NONE` | 无错误。 |
| `AXP2101_ERROR_INVALID_PARAM` | 参数、地址或配置项非法。 |
| `AXP2101_ERROR_PORT_NOT_BOUND` | Ops 或 Context 未绑定。 |
| `AXP2101_ERROR_NOT_READY` | 当前状态不允许执行操作。 |
| `AXP2101_ERROR_BUS_PREPARE` | 后端准备失败。 |
| `AXP2101_ERROR_BUS_READ` | 寄存器读取失败。 |
| `AXP2101_ERROR_BUS_WRITE` | 寄存器写入失败。 |
| `AXP2101_ERROR_WRONG_CHIP_ID` | 总线成功，但芯片 ID 不匹配。 |

`AXP2101_BusStatusTypeDef` 只保留稳定类别：OK、ERROR、BUSY、TIMEOUT、NACK。
SoftI2C 的原始位图仍留在 Platform 私有的 `hplatform_power_i2c.ErrorCode`，不会跨越
Adapter 泄漏给 Device 或 APP。

APP 如需记录错误，只调用 `Platform_Power_GetDiagnostics()` 复制只读快照，不公开
私有 Handle。

## 8. 对外接口和使用范围

### 8.1 APP 通常只使用

```c
Platform_Power_Init();
Platform_Power_SetAudio(true);
Platform_Power_SetLCD(true);
Platform_Power_GetDiagnostics(&diagnostics);
```

### 8.2 Platform 或 Device 独立测试使用

```c
AXP2101_SoftI2CAdapter_Bind(&device, &soft_i2c);
AXP2101_Init(&device);
AXP2101_ApplyConfiguration(&device, profile, count);
AXP2101_SetALDO1Enabled(&device, true);
AXP2101_SetALDO2Enabled(&device, true);
```

`AXP2101_ApplyConfiguration()` 是受控配置接口，不应直接暴露给普通 APP。APP 不应
知道寄存器地址。

## 9. 切换为硬件 I2C

新增 `Adapters/axp2101_hal_i2c/axp2101_hal_i2c_adapter.c/.h`，实现同一组 Bus Ops：

1. Context 使用 `I2C_HandleTypeDef *`；
2. `Prepare` 检查 HAL I2C Handle 是否可用；
3. `MemRead/MemWrite` 调用 HAL I2C；
4. Adapter 内把 `HAL_StatusTypeDef` 和必要的 HAL 错误位转换成
   `AXP2101_BusStatusTypeDef`；
5. Platform Power 改为拥有或引用具体 HAL I2C Handle，并调用新的 Bind。

不需要修改 `axp2101.c`，也不需要修改 APP 的 Platform Power 调用。

## 10. FreeRTOS 并发约束

当前 AXP2101 与 SoftI2C 都是同步阻塞实现，没有 Mutex。同一 Handle 不能被多个
任务同时调用，否则可能出现：

- 两个寄存器事务交叉；
- `State`、`ErrorCode` 被另一任务覆盖；
- 读改写期间其他任务改变同一寄存器。

接入多个电源使用者后，建议由 Power Service 单任务串行处理请求，或在 Platform Power
入口使用 Mutex。不要在 Device 内硬编码 FreeRTOS API，以保持驱动可复用。

## 11. SoftI2C 为什么已经是纯 Component

`soft_i2c.h` 的 Handle 不再保存 `GPIO_TypeDef *` 和 GPIO Pin，而是保存：

```text
GPIOOps
GPIOContext
DelayCycles
ClockStretchTimeout
State
ErrorCode
```

GPIO Ops 只有两项：

- `Write(context, SCL/SDA, LOW/RELEASED)`；
- `Read(context, SCL/SDA)`。

这种接口只抽象软件 I2C 算法真正需要的最小能力，不制造通用 GPIO HAL。
`soft_i2c.c` 也不再调用 CMSIS `__NOP()`，而是使用可移植的 volatile 忙等待循环。

## 12. CMake 发现规则

根 `CMakeLists.txt` 从以下目录递归收集自维护源文件：

```text
Components/*.c
Adapters/*.c
Platform/*.c
```

工程根目录作为唯一私有 include path，因此代码使用完整、无歧义的路径，例如：

```c
#include "Components/axp2101/axp2101.h"
#include "Adapters/axp2101_soft_i2c/axp2101_soft_i2c_adapter.h"
#include "Platform/power/platform_power.h"
```

## 13. 当前限制与后续演进

1. Platform 启动表仍使用原始寄存器值，优点是保持已验证硬件行为，缺点是语义不够直观；
2. 当前只封装 ALDO1/2 开关，尚无通用电压、电流、ADC 和中断接口；
3. SoftI2C 使用忙等待，不适合高频访问；
4. 尚无电源服务任务、请求队列和并发保护；
5. 尚未加入关机顺序、低电量策略和故障恢复；
6. 只有一个真实 PMIC，不建立虚假的通用 PMIC 基类。

当增加第二种 PMIC 时，先比较真实公共需求，优先抽象板级能力，例如：

```text
Power_SetRail(POWER_RAIL_AUDIO, enabled)
Power_SetRail(POWER_RAIL_LCD, enabled)
Power_GetBatteryStatus(...)
```

不要把 AXP2101 的 ALDO、DCDC 或寄存器名搬进所谓的通用接口。

## 14. 代码索引

- 板级电源实现：[`../Platform/power/platform_power.c`](../Platform/power/platform_power.c)
- 板级电源接口：[`../Platform/power/platform_power.h`](../Platform/power/platform_power.h)
- AXP2101 驱动：[`../Components/axp2101/axp2101.c`](../Components/axp2101/axp2101.c)
- AXP2101 接口：[`../Components/axp2101/axp2101.h`](../Components/axp2101/axp2101.h)
- 寄存器定义：[`../Components/axp2101/axp2101_regs.h`](../Components/axp2101/axp2101_regs.h)
- SoftI2C Adapter：[`../Adapters/axp2101_soft_i2c/axp2101_soft_i2c_adapter.c`](../Adapters/axp2101_soft_i2c/axp2101_soft_i2c_adapter.c)
- SoftI2C Component：[`../Components/soft_i2c/soft_i2c.c`](../Components/soft_i2c/soft_i2c.c)
- STM32 GPIO Adapter：[`../Adapters/soft_i2c/soft_i2c_stm32_hal_adapter.c`](../Adapters/soft_i2c/soft_i2c_stm32_hal_adapter.c)
