# ADR-0017：MP3 解码库选用 minimp3，解码接口与容器解析分离

- 状态：已接受。
- 日期：2026-10-07
- 相关 ticket：[研究：MP3 解码库候选对比](https://github.com/immorcoding/mp3_project/issues/30)、[解码器选型与 ADR](https://github.com/immorcoding/mp3_project/issues/31)、[支持的 MP3 范围与总时长来源](https://github.com/immorcoding/mp3_project/issues/32)
- 相关技术文档：[Storage](../shape/storage.md)、[Resources](../shape/resources.md)、[Hardware](../shape/hardware.md)

## 背景

0.6.0 的终点是 SD 上的单首 MP3 经解码、I2S DMA 送到 PCM5102A 真正出声。仓库目前没有解码器。约束如下：

- 仓库以 MIT 发布。所选解码库只接受宽松许可（CC0、公有领域、MIT、BSD、Apache 一类）；GPL、LGPL、RPSL、RCSL 不在候选内。
- 项目对新增堆有审查要求（[HWD-2](../shape/hardware.md)），FreeRTOS 堆也不大。解码器状态与缓冲应能由调用方静态持有。
- 解码输出要能直接对接 I2S 的 16 位 PCM。

## 决定

1. MP3 解码库采用 **minimp3**（CC0，单头文件）。
2. 第三方代码放在 `Middlewares/Third_Party/minimp3/`，沿用 FatFs、FreeRTOS、LVGL 的目录惯例；版本固定到具体提交，升级作为独立变更。
3. 解码器状态 `mp3dec_t` 与帧输出缓冲由播放模块（Service 层）静态持有，不使用堆。每帧的 scratch 是栈上局部变量，其栈占用须实测后计入解码任务的栈预算。
4. 解码输出使用 minimp3 的默认 16 位 PCM。
5. 播放链路通过一层**解码接口**与具体实现隔离：上层只依赖"输入音频帧字节 → PCM 帧与帧信息"这一抽象，不直接调用 minimp3。接口的具体名称在实施 ticket 中确定。
6. **容器解析不属于解码接口。** 文件层负责定位音频数据、跳过 ID3v2 等非音频头，解码器只接收 MP3 音频帧流。0.6.0 只支持 MP3。

## 未采用的方案

- **dr_mp3（dr_libs）**：与 minimp3 同源，双许可（公有领域或 MIT-0）。但默认用 `DRMP3_MALLOC` 等宏分配内存，与第 3 条的静态持有冲突。替换分配器可以做，但会把 dr_libs 的内部结构变成本仓库要维护的部分。
- **Helix MP3（RealNetworks）**：RPSL/RCSL 许可，与宽松许可的约束冲突，排除。
- **libMAD（ESP8266Audio 的 MP3 路线）**：GPL，排除。
- **不设解码接口，直接调用 minimp3**：实现最少，但换库或加入新格式时要改播放链路，违背第 5 条。

## 后果

- 0.6.0 只支持 MP3。AAC、MP4/M4A 需要 AAC 专利许可（Via LA 等专利池的收费条件）与法务意见，另立 ADR，不在本决定内。
- 解码耗时和栈占用没有公开数据可依赖。须在板上实测。参照预算：44.1 kHz 下一帧（1152 个采样）约 26.1 ms。
- minimp3 代码层面支持 MPEG-2/2.5 Layer III，但这是从代码推断的，尚未用真实文件验证。支持范围见"支持的 MP3 范围"ticket。
- 许可证：minimp3 为 CC0。CC0 只覆盖代码版权，不覆盖 MP3 格式专利。按一般了解主要专利已到期，但这一点待法务确认；本 ADR 不作法律结论。
