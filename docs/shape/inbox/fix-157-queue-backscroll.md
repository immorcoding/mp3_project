# Inbox: fix/157-queue-backscroll (#157 Queue 回滑跨窗顶行消失)

- 2026-10-28 · gui/music · friction · GUI-16 · #157：非首窗只留一行回滑行，弹性把手指拉过窗口首行，快速回滑时在上一窗 READY 前露出整行空白；改为留 3 行，并在窗口边沿之外还有曲目时按边沿拉回。先试过“只在首窗开弹性”，用户否决：列表首尾回弹不一致（首行回弹、末尾不回弹）。
- 2026-10-28 · gui/music · proposed · 改写 GUI-16：Queue 换窗回收 panel 并保留余像素；非首窗在可见首行之上留 3 行回滑行；窗口边沿之外还有曲目时滚动停在该边沿、不露空白，只有列表真正的首尾弹性回弹。 _Why:_ 换窗不能改变手指对应的像素位置或阻断反向滚动；窗口边沿之外的曲目尚未就绪时不能露出空白；列表首尾的回弹行为要一致。
- 2026-10-28 · gui/ownership · cite · GUI-4 · #157：`queue_long_scroll` 两帧哈希变化，逐帧看图，版式与文字不变，只有当前行长名循环滚动相位随末次 Apply 时刻变化；新增 `queue_back_scroll`（6 帧）、`queue_forward_fling`（4 帧）逐帧看过，行连续、顶部与底部无空白，`back_tail_bounce` 与 `back_top_bounce` 分别在末尾与首行露出回弹空隙；换回旧实现时 `back_mid_second` 变红，换回“只在首窗开弹性”时 `back_tail_bounce` 变红。
