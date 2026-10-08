# Inbox · worktree-wayfinder-37-glossary

分支用途：记录 [进度上报与 GUI 通信](https://github.com/immorcoding/mp3_project/issues/37) 的 grilling 批准。本分支不改 `GLOSSARY.md`；合并到 `main` 后按 HAR-8 drain。

## 批准（2026-10-08，用户当场确认）

- **词条**：GLOSSARY 新增「播放位置」。
- **位置**：`GLOSSARY.md` 的「音频解码与容器」分节，放在「音频帧流（Frame Stream）」之后。
- **原因**：「进度」现在同时指解码时间、界面 0..100 百分比、或假进度，意思不一致；#36 改写假进度规则时需要一个稳定的名字。

### 待写入的词条

```markdown
**播放位置（Playback Position）**：
解码器已输出的已播时间（毫秒），是进度唯一的事实来源；界面进度条的 0..100 只是它的显示换算。
_Avoid_: 墙钟累加的假进度、界面百分比、DMA 当前读取地址
```

## Drain 时

合并到 `main` 后，在 `main` 上把上方词条插入 `GLOSSARY.md`，然后删除本文件。
