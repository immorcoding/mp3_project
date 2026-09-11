# CURRENT.md

> 章程：工程现场索引。开场必读。进度跨会话；本场交接在任务替换或硬切前重写。合计约 80 行，只放指针。本刀合同与板上事实在活动 slug 目录，工作方式见 `AGENTS.md`，术语见 `CONTEXT.md`。

## 进度

- 主线：真底图叠假封面已接线，未上板；旋转不做。
- 活动 slug：`.scratch/vinyl-id4/`。范围见 `freeze.md`；板上状态见 `board.md`。
- 阻塞：`./scripts/verify.ps1` FULL 被工作树 SquareLine 脏文件挡住；ID 4 显示待上板。
- verify：分层 include、固件 Debug/Release、全部 host 通过。打包器 6/6 仍为提交 `9087794`。FULL 入口未通过。
- 按需查词：**资源包**。

## 本场交接

- 分支：`main`，与 `origin/main` 对齐。助手不代提交。
- 脏文件：用户 SquareLine 三文件（生成目录，助手不改）。本刀 Resource/Canvas/vinyl 已纳入本提交。
- 固件已加载 ID 1–4；Canvas 复制 ID 4 底图再叠假封面，`main/vinyl/` `set_src`。Pack v2 已在 NOR。新固件未下板。
- `./scripts/verify.ps1` FULL 被工作树 SquareLine 脏文件挡住。分层 include、固件 Debug/Release、全部 host 测试已通过。
- 下一动作：烧录本刀固件，上板看真底图叠假封面，证据记 `board.md`。
