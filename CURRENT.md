# CURRENT.md

> 章程：工程现场索引。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。本刀合同与板上事实在活动 slug 目录，工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 主线：假唱盘第一帧已上板通过；下一刀换真唱片底图，旋转不做。
- 活动 slug：`.scratch/vinyl-id4/`。范围见 `freeze.md`；板上状态见 `board.md`。
- 阻塞：无。
- verify：打包器 `Tools/package_maker/tests/test_rpkc_pack.py` 6/6 PASS（提交 `9087794`）。FULL 自那之后未复跑。
- 按需查词：**资源包**。

## 本场交接

- 分支：`main`，领先 `origin/main` 3 提交。助手不代提交。
- 脏文件：`AGENTS.md`、`CURRENT.md`、`docs/agents/{domain.md,issue-tracker.md}`、未跟踪 `.scratch/vinyl-id4/{freeze.md,board.md}`；另有用户在改的 `stm32h743zgtx_flash.ld`（生成目录，助手不改）。
- `.ld` 现状：外部资源去掉 `KEEP`；vinyl 槽为 `. += 0xF300` 并带尺寸 `ASSERT`；cp936/壁纸仍按 section 放入并 `ASSERT`。未提交、未随固件下板。
- 固件仍只加载 ID 1–3；`canvas/` 仍假圆。Pack v2 已在 NOR，见 `board.md`。
- 下场第一刀：Resource 加载 ID 4 → Canvas 底图合成叠假封面 → `main/vinyl/` `set_src`。新会话从本索引进 slug，约束以 `freeze.md` 为准。
