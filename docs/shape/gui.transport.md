# GUI · transport

播放控制与唱盘呈现。

[返回 GUI](gui.md)

### Rules

- **GUI-10** · provisional · 时间、进度与三键仅在 Now Playing；transport 绑定 CLICKED/RELEASED，Task 消费输入；拖动不被 Apply 覆盖，松手假 seek 不改 playing，切歌/拔卡归零，假 playing 不按墙钟走表。_Why:_ 尚无真实解码时维持明确的输入/状态边界。_Source:_ [播放控制基线](../../Tools/gui_simulator/scenarios/music_transport.expected)
- **GUI-14** · provisional · 唱盘底图从 Pack 载入 SDRAM，由 vinyl 合成封面后绑定无事件 Image，图像尺寸与 view 一致；图标复用 LVGL 符号，图片不另编入内部 Flash。_Why:_ 区分资源加载、像素合成和输入，避免重复静态资源。_Source:_ [GUI 模块](../../Service/gui/README.md)、[资源 Service](../../Service/resource/README.md)
