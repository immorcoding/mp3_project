# minimp3

MP3 解码库（CC0），选用依据见 [ADR-0017](../../../docs/adr/0017-mp3-decoder-minimp3.md)，流式输入合同见 [#46](https://github.com/immorcoding/mp3_project/issues/46)。

- 上游：<https://github.com/lieff/minimp3>
- 固定提交：`ea99364f61c14656440e8d77e9c233ccf3124633`（短哈希 `ea99364`）
- 内容：`minimp3.h`、`LICENSE` 为上游该提交的原文件，不修改；升级作为独立变更（ADR-0017 第 2 条）。
- 只有 `Service/playback/playback_service_decoder_minimp3.c` 包含并定义 `MINIMP3_IMPLEMENTATION`；不使用 `minimp3_ex.h`。
