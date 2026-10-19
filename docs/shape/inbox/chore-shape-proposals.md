# Inbox: chore/shape-proposals

> 以下是 2026-10-19 对 0.6.0 工作（#71 播放后端、#72 音乐页交互）做 retro 后提出的规则草稿，均未获批准。drain 时逐条提给用户；已批准的提议才会分配规则编号。

- 2026-10-19 · git · proposed · exploring · 带板级验收清单的 spec，其集成 PR 保持 draft，直到清单被填写（未测项明确列出并保留 `hw:pending`）再由用户合并；板上发现的问题在集成分支上修。_Why:_ #89 先于 #81 板测合并，#90 改为先板测再合并，板测在合并前就发现了监控漏登记（`Playback Task` 栈高水位读不到）并在分支上修复；用户原话：“test it on the board first, then merge … catch and fix any issues before they hit master”。扩展 GIT-3（“按 spec 实现或需要上板的工作开分支、提 draft PR”）而不是新建并行规则；drain 时判断是并入 GIT-3 还是单列。
- 2026-10-19 · workflow · proposed · exploring · spec 的每个验收项写明证据来源，并且该来源在固件里存在（日志行、计数器、可读寄存器，或明确标“仅听感/外部仪器”）；缺出口的项在同一 spec 里列为任务。_Why:_ #71 板级验收清单要求“`Underruns` 日志”作证据，但播放路径除错误外不打任何日志、`Underruns` 没有输出口，板测只能凭听感；固件没有版本输出，测试记录里每项只能写“48f97f0 或 859f307”。与 WF-3（板上证据作为 issue 评论贴出）相邻，drain 时判断并入还是单列。
- 2026-10-19 · code-style · proposed · exploring · 注释不用 spec 或 ticket 内部编码（如 B5、K3、A7、D7、E8）作为唯一说明；先用一句话写出含义，再按需链到 spec。_Why:_ #71/#72 集成分支评审指出这些编码对没读过 ticket 的读者不可解，在 `playback_task_config.h`、`app_tasks.c` 等多处出现。判断类项，只靠 reviewer，不适合自动检查。
- 2026-10-19 · code-style · friction · 无对应规则 · `_Static_assert` 的消息用英文还是中文没有规定：#71/#72 实施里两者都有（`playback_task.c`、`gui_music_transport.c` 用英文）；STY 只规定“注释、文档用中文”。是否把断言消息算作注释需要用户决定。
