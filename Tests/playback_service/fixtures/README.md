# 夹具说明

四个 MP3 夹具全部由 [generate_fixtures.py](generate_fixtures.py) 合成，不含任何第三方或受版权保护的录音，码流归本仓库所有：

- 音频内容是脚本逐采样计算的确定性正弦和弦加缓慢幅度包络（峰值 12000/32768），没有采样自任何录音；
- 编码用 LAME，经 Python 包 `lameenc` 1.9.0（内嵌 LAME）调用；`lameenc` 只是生成时的一次性工具，不属于固件或主机测试的依赖，生成结果直接提交；
- LAME 的流式接口不写 Xing/Info 或 VBRI，脚本按文件格式手工拼出这两种元数据帧并前置到码流前。

| 文件 | 字节 | 音频帧 | 规格 | 用途 |
|---|---:|---:|---|---|
| `cbr_44100_stereo.mp3` | 12538 | 30 | MPEG-1 L3，44.1 kHz 立体声，CBR 128 kbps，无元数据帧 | 主路径、按字节数估算总时长 |
| `vbr_xing_44100_stereo.mp3` | 6507 | 30 | MPEG-1 L3，44.1 kHz 立体声，LAME VBR（40–256 kbps），前置 Xing 帧（帧数 30） | VBR 与 Xing 总时长、元数据帧跳过 |
| `vbri_44100_stereo.mp3` | 9194 | 24 | MPEG-1 L3，44.1 kHz 立体声，CBR 112 kbps，前置 VBRI 帧（帧数 24） | VBRI 总时长、元数据帧跳过 |
| `mpeg2_22050_mono.mp3` | 4284 | 41 | MPEG-2 L3，22.05 kHz 单声道，CBR 32 kbps，每帧 576 采样 | 单声道复制到左右、MPEG-2、异频帧丢弃 |

“音频帧”不含前置的 Xing/VBRI 元数据帧。测试里的期望数字（帧数、总时长）由这些值推出；重新生成后若数字变化，须同步 `Tests/playback_service` 的常量。

重新生成（需要 `pip install lameenc`，在仓库根目录）：

```powershell
python Tests/playback_service/fixtures/generate_fixtures.py
```

脚本会打印每个夹具的字节数与音频帧数。LAME 版本不同可能产生字节不同的码流，所以夹具以提交的文件为准，脚本用来说明来源与规格。
