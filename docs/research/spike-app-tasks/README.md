# #191 板测原始日志

[上板验证：加锁、前台任务、GUI 睡眠](https://github.com/immorcoding/mp3_project/issues/191) 的证据，只在可丢弃分支 `spike/app-tasks` 上，不合入。结论写在票上，这里只留原始材料。时间戳是本机时间，括号里是板上启动后的毫秒数。

| 文件 | 内容 |
|---|---|
| `board-global-2.log` | 全局一把锁，`d970ce4`：第一遍，发现拔出事件被吞（05:46:06 至 05:46:10）。P0、P1 的日志因抓取脚本和监视抢文件丢失 |
| `board-global-3.log` | 全局一把锁：`bc01237` 一遍（拔卡修复生效，06:33:22），`ca34390` 两遍；最后一次开机（06:49）的 P3 含 5 次切歌 |
| `board-pervol.log` | 开头是上面那次全局运行的 P3 末尾、P4 和 P5；06:54:50 起是每卷一把锁，`ca34390`，含切歌和拔卡 |
| `capture-shared.ps1` | 抓串口的脚本：与 `Tools/board_test_card/capture-serial.ps1` 相同，但日志文件允许边写边读 |
| `extract.py`、`extract2.py` | 按阶段提取终值的脚本（路径是当时的临时目录） |
| `comment-191-round1.md` | 第一遍发现的票评论原文 |
