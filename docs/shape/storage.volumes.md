# Storage · volumes

可移除状态、卷就绪及文件对象失效。

[返回 Storage](storage.md)

### Rules

- **STOR-8** · settled · 无卡是可正常启动的持续状态；检测边沿重开任务消抖，并暂缓窗口请求、音频流请求与补块（已打开的流保持打开），稳定后才改变介质状态，消抖不等于卸载；事件等待兼顾消抖和回收期限，传输完成槽与主循环事件槽分开，挂载后继续使用初始化注入的完成槽。_Why:_ 抖动不能伪装传输结果或饿死后台回收，暂不可请求也不等于曲库已消失。_Source:_ [Storage 接口](../../APP/tasks/storage/README.md)、[SD 生命周期](../../Components/sd/README.md)、[音频流协议](https://github.com/immorcoding/mp3_project/issues/48) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及热插拔/通知回归。
- **STOR-9** · settled · SD 与 Flash 独立初始化、挂载和报告错误；SD 无设备侧格式化，Flash 显式格式化或恢复成功时须已重新挂载并使本卷旧 Token 失效；APP 自动格式化策略只处理无文件系统结果，损坏、不兼容等其他失败保留可诊断状态。_Why:_ 一个卷的故障不能污染另一个卷或令旧对象透明继续写。_Source:_ [Filesystem 接口](../../Service/filesystem/README.md)、[APP 策略](../../APP/tasks/storage/flash/storage_flash.c) _Check:_ [CODING_STANDARDS](../../CODING_STANDARDS.md) 存储审阅及独立卷/Token/恢复回归。

### Signals

- 2026-10-09 · friction · STOR-8 · 插卡消抖后同步挂载并扫描（最多 32000 首，可能数秒），期间不处理流请求，插卡后立即播放会触发 T_data 停止（已接受）；改为分步扫描须同时改写主循环结构与 T_data 的依据。
