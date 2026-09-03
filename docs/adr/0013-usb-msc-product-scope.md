# ADR-0013：USB MSC 不纳入当前产品范围

- 状态：已接受
- 日期：2026-09-02

## 背景

USB Full-Speed 的物理上限和 MSC 协议开销使大文件写入速度远低于读卡器。实现 MSC 还需要本地 FatFs 卸载、介质所有权切换、USB Task 与 Storage Task 的同步请求、异常断开和文件系统一致性维护。

这些复杂度不符合当前播放器以 SD 卡为主介质、批量导入可使用读卡器的产品取舍。

## 决定

当前主线只提供 USB CDC 日志与命令能力，不提供 USB MSC。批量导入音乐和其他大文件使用读卡器。

不在主线预建 MSC 所有权仲裁、原始 SD 块 Interface、USB Task 到 Storage Task 的 MSC 请求链路或 USB CDC + MSC Composite 配置。先前的 MSC 实验实现保留在 `codex_msc` 分支，不构成主线支持承诺。

## 后果

- Storage Task 只管理本地 FatFs 和 SD 生命周期，无须切换卷所有权。
- Filesystem 保持卷、文件和诊断三个公开 Interface，不增加 MSC 专用接缝。
- 若未来硬件具备 USB High-Speed 能力且产品需要免读卡器的文件维护，需重新评估性能、主机兼容性和所有权语义；不能直接恢复旧实验实现。
- 设备内从 SD 复制到 Flash FTL（资源管理器或安装暂存）不属于 MSC，见 [ADR-0015](0015-volume-roles-and-resource-install.md)。

ADR-0012 中关于“MSC 首版范围”的前瞻性内容由本 ADR 取代；其余 Filesystem 公开接缝决定保持有效。
