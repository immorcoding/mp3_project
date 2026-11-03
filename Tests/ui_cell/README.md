# ui_cell 主机测试

验证 `APP/ui_cell/ui_cell.c`（[#193](https://github.com/immorcoding/mp3_project/issues/193) 草案的 spike 实现，可丢弃分支 `spike/app-tasks`，[#191](https://github.com/immorcoding/mp3_project/issues/191)）：

- 整份拷贝：写入后读者拿到完整内容，序号随写入前进；
- 没变不拷贝：序号没变的帧不碰锁，即使锁被别的任务占着也不记尝试、不记失败；
- 拿不到锁：`ui_cell_try_write` 返回假，数据与序号不变并记一次失败；`ui_cell_read_if_changed` 保持原样并记一次失败，锁释放后下一帧再试能成功；
- 写后叫醒：登记了读者任务时置通知位，未登记不叫醒；
- 统计：持锁次数、总和、最大值，重置后清零但不动序号与内容。

编译真实的 `ui_cell.c`；FreeRTOS 头、周期计数器、tick 与任务通知替身沿用 `Tests/flash_ftl/stubs/`。主机是单线程的，用“测试先拿住格子里的锁”模拟另一个任务持锁。**真实并发下的持锁时长、GUI 拿不到锁的频率、优先级继承和体感只能上板**，Host PASS 不证明它们。

从仓库根目录运行：

```powershell
./scripts/test-host.ps1 -Module ui_cell
```
