# Inbox: fix/157-queue-backscroll (#157 Queue 回滑跨窗顶行消失)

- 2026-10-28 · gui/music · friction · GUI-16 · #157：非首窗只留一行回滑行且开着弹性，快速回滑时手指在上一窗 READY 前越过窗口首行，弹性露出整行空白；改为留 3 行并只在首窗开弹性。
- 2026-10-28 · gui/music · proposed · 改写 GUI-16：Queue 换窗回收 panel 并保留余像素；非首窗在可见首行之上留 3 行回滑行，且只有首窗开弹性滚动。 _Why:_ 换窗不能改变手指对应的像素位置或阻断反向滚动；窗口上沿之外还有曲目时，弹性会在上一窗 READY 前露出空白。
- 2026-10-28 · gui/ownership · cite · GUI-4 · #157：`queue_long_scroll` 两帧哈希变化，逐帧看图，版式与文字不变，只有当前行长名循环滚动相位随末次 Apply 时刻变化；新增 `queue_back_scroll`、`queue_forward_fling` 两个场景的 8 帧逐帧看过，行连续、顶部与底部无空白；改回旧实现时 `queue_back_scroll` 的 `back_mid_second` 变红。
