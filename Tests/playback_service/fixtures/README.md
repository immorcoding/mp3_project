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

## `xing/`：跳转精度夹具（#174）

给 [#175](https://github.com/immorcoding/mp3_project/issues/175) 的跳转精度测试用：逐帧扫描算出偏移处第一帧的真实时间，与映射反算的落点比较。四个文件同一规格：MPEG-1 L3、44.1 kHz 立体声、20 s（767 个音频帧）、约 160 KB，内容是样本卡同一段确定性和弦（`make_card.tile_pcm`），不含第三方录音。CBR 两个共用同一段音频码流，VBR 两个共用另一段；带头的文件 = 头帧（417 B）+ 无头文件的全部字节。

| 文件 | 字节 | 音频帧 | 头 | 说明 |
|---|---:|---:|---|---|
| `xing/cbr_nohdr.mp3` | 160287 | 767 | 无 | CBR 64 kbps，线性映射（`lin`） |
| `xing/cbr_info.mp3` | 160704 | 767 | `Info`，flags 0x03（帧数 767、字节数 160287），无 TOC | CBR 64 kbps，`lin` |
| `xing/vbr_toc.mp3` | 161036 | 767 | `Xing`，flags 0x07（帧数 767、字节数 160619），100 项 TOC | LAME VBR（`vbr_quality=2`），`toc` 映射 |
| `xing/vbr_nohdr.mp3` | 160619 | 767 | 无 | 同一段 VBR 音频，`lin`，误差只记录 |

- **字节数字段与 TOC 口径**：只计音频帧，不含头帧本身，TOC 偏移从第一个音频帧起算（与上面三个旧夹具的 Xing 一致）；真实 LAME/ffmpeg 的字节数含头帧，差 417 B，对 TOC 精度可忽略。TOC 第 i 项 = `min(255, 256 × 第 floor(i × 帧数 / 100) 帧的偏移 ÷ 音频总字节)`，逐项取整。
- **头帧**：128 kbps、无 CRC、无填充的 417 B 帧，边信息区与未用部分全零；`Info`/`Xing` 标识在帧内偏移 36。
- **生成命令**（仓库根目录，`pip install lameenc` 且版本为 1.9.0）：

  ```powershell
  python Tools/board_test_card/make_card.py Tests/playback_service/fixtures/xing --set host-fixtures
  ```

  参数：CBR 为 `bitrate=64`，VBR 为 `vbr_quality=2`（`set_vbr(4)`），均 `quality=2`；头由 [card_xing.py](../../../Tools/board_test_card/card_xing.py) 写，只依赖码流，确定性。`python Tools/board_test_card/test_card_seek.py` 检查提交文件的规格，并（装有 `lameenc` 时）验证重新生成与提交文件逐字节一致。
- FULL 只读这些提交的文件，不依赖 `lameenc` 或 ffmpeg。LAME 版本不同可能产生不同码流，此时需整体重新生成并同步 #175 的常量。
- **ffmpeg 对照样本**（不提交、不进确定性测试）：取 #163 真实曲库里一首 CBR 曲，转成带 Info 头的 CBR 文件，用来对照自写头的字段口径：

  ```powershell
  ffmpeg -i <曲库中的 CBR 曲>.mp3 -vn -c:a libmp3lame -b:a 320k -write_xing 1 -id3v2_version 0 -write_id3v1 0 ref-ffmpeg-cbr-info.mp3
  ```

  ffmpeg 写的 `Info` flags 为 0x0F（带 TOC 与质量字段），与本目录 `cbr_info.mp3` 的 0x03 不同，所以它对照的是“CBR 带 Info 与 TOC”的真实形态。
