# MCU 内部温度采样架构

## 目标与边界

本功能提供 STM32H743 MCU 的**结温**诊断值，单位为毫摄氏度。它不表示环境温度，也不实现风扇、降频、过温保护或任何热管理策略；这些若有需要，应由未来 Service 或 APP 在消费结温后决定。

当前仅存在 STM32H7 的一种硬件实现，因此采用 `Platform + Adapter` 的最小边界，不预先建立 `Components/temp`。当出现第二种 MCU 后端，或有独立的热管理领域能力需要统一时，再把稳定 Interface 提炼为 Component。

## 职责与请求路径

```text
Monitor Task
  -> Platform_Temp_Init()
    -> Temp_STM32HAL_Calibrate(hadc3)
  -> Platform_Temp_Read()
    -> Temp_STM32HAL_Read(hadc3)
      -> HAL ADC3 + 芯片工厂标定数据
```

- `APP/tasks/monitor`：启动时校准一次、决定诊断频率并记录结温；
- `Platform/temp`：选择当前 PCB 的 CubeMX `hadc3`，只暴露统一 Platform 状态；
- `Adapters/stm32_hal/temp`：校准 ADC、轮询两个 Rank、修正 VDDA、按工厂标定换算温度；
- `Core/adc.c`：CubeMX 拥有 ADC3 的时钟、通道和 Handle 配置。

该图是运行时请求路径，不代表所有源文件的 `#include` 方向。Platform 通过 Adapter Interface 进行装配，Adapter 则依赖 HAL 和 ST 的芯片标定常量。

## ADC3 配置不变量

CubeMX 必须保持以下 ADC3 配置：

| 项目 | 要求 |
| --- | --- |
| ADC 实例 | ADC3 |
| Rank 1 | Temperature Sensor |
| Rank 2 | VREFINT |
| 转换触发 | Software start |
| 连续转换 | 关闭 |
| EOC 选择 | End of single conversion |
| Low Power Auto Wait | 开启 |
| 分辨率 | 16 bit |
| 温度传感器采样时间 | 不低于数据手册要求；当前 10 MHz ADC 时钟下为 810.5 cycles |

ADC 的 `DR` 只有一个数据寄存器。启用 Low Power Auto Wait 后，Rank 1 完成时硬件暂停；Adapter 的 `HAL_ADC_GetValue()` 读取温度样本后，Rank 2 才开始转换。因此 Adapter 可用两次 `HAL_ADC_PollForConversion()` / `HAL_ADC_GetValue()` 可靠取得两个 Rank，而不会发生后一个结果覆盖前一个结果。该机制仅用于当前低频 HAL 轮询，不与 ADC 中断或 DMA 混用。

## 换算与校准

Monitor Task 启动时执行一次 ADC offset calibration；之后每次低频诊断完成一次双 Rank 转换：

1. 读取内部温度传感器原始值 `raw_temp`；
2. 读取内部参考电压原始值 `raw_vrefint`；
3. 以芯片 `VREFINT_CAL` 工厂值求出实际 `VDDA`；
4. 将 `raw_temp` 缩放到温度传感器工厂校准电压；
5. 用 `TS_CAL1` / `TS_CAL2` 的两个温度标定点线性插值，输出毫摄氏度。

`VREFINT` 用于补偿实际模拟供电电压与工厂校准电压的差异；若直接拿温度原始 ADC 值插值，VDDA 波动会被误认为温度变化。

## 生命周期与失败语义

- `MX_ADC3_Init()` 在 FreeRTOS Task 创建前已由 CubeMX 执行，因此内部温度传感器缓冲稳定时间已满足；
- ADC offset calibration 在 Monitor Task 启动时完成一次；`HAL_ADC_Stop()` 不会丢失结果，只有 ADC DeInit、进入 deep-power-down 或复位后才需要重校准；
- Adapter 不保存 ADC Handle，也不创建任务、日志或通知；
- HAL 校准、启动、轮询、停止、原始值或工厂数据异常都会使 `Platform_Temp_Read()` 返回 `PLATFORM_TEMP_ERROR`；
- Monitor 连续失败时只输出第一条错误，成功一次后恢复正常记录，避免错误刷屏。

## 后续演进

若将来需要实时温度曲线、多通道 ADC DMA，或温度触发的产品策略，应新增明确的 Service，并重新评估 ADC 的资源仲裁与连续采样设计；不能在当前低频诊断 Adapter 内累积任务策略。
