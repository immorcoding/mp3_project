# CURRENT.md

> 章程：工程现场。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 刚收口：假唱盘第一帧板上确认通过（深灰盘 + 青中心）。Pack v2 已由用户 External Loader 写入 NOR。HEX DFU 不更新该包。
- 下一活动：固件解包 ResourceID 4，用唱片底图真正替换 `MusicPlayerVinylImage` 的假纯色圆。旋转未做，本刀不做。禁止手改 `GUI/`。TimeLabel 仍不接线。
- 本主线必读：`docs/gui_ui_design.md` **10.5.4**、`docs/resource_pack_design.md`、`Service/resource/README.md`、`Service/gui/canvas/README.md`、`Service/gui/main/vinyl/README.md`、ADR-0007。跨层先读 `docs/architecture_standard.md`。
- verify：打包器 `Tools/package_maker/tests/test_rpkc_pack.py` 6/6 PASS。FULL 未过：工作树删除了生成文件 `GUI/images/ui_img_wallpaper_indigo_mist_png.c`。按需查词：**资源包**。

## 本场交接

- 分支：`main`。禁止手改 `GUI/`。SquareLine 不加事件。不建 `Service/playback/`。助手不代提交。
- Pack：`build/external-resources/resource_pack.bin`，`package_version` 2，长度 `0x77000`，映射 `0x90401000`。PackageVersion 在 `0x90401028`（8 B 小端），应为 `2`。
- 入包：ID 1/2 BINARY CP936；ID 3 IMAGE 壁纸 240×320；ID 4 IMAGE `vinyl_original_144px` 144×144 TRUE_COLOR_ALPHA（62208 B，stride 432）。扫描 `Resources/imgs/*.c` 文件名排序，忽略 PNG。
- 脏文件未提交：`CURRENT.md`、`Tools/package_maker/{extract_project_resources.py,main.py,resource_pack.json,tests/test_rpkc_pack.py}`、`docs/resource_pack_design.md`、`Resources/imgs/{ui_img_wallpaper_indigo_mist_soft_dark_png.c,vinyl_original_144px.c,vinyl_original_144px.png}`；删除 `GUI/images/ui_img_wallpaper_indigo_mist_png.c`、`Resources/imgs/wallpaper_indigo_mist.png`。
- 固件仍只加载 ID 1–3。`canvas/` 仍合成假圆 `#202020` / `#00B0DE`。直径 `SERVICE_GUI_MUSIC_VINYL_DIAMETER` 须与 Image 宽高一致。不把唱盘 C 数组编进内部 Flash；不要 `KEEP` 该数组。不读 ID3。假封面与真封面本刀不换。
- D-Cache 行只在 `cortex_m7_dcache_adapter_config.h`。Slider 假 SEEK、切歌/CLEAR 归零仍有效。电量/时间栏、Library、真解码/seek 仍不做。
- 下场第一刀：Resource Service 加载 ID 4 到 SDRAM 槽，Canvas 用底图合成再叠假封面，`main/vinyl/` `set_src`。旋转另刀；不要塞进 `transport/`。
