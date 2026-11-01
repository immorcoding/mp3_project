# Inbox: impl-184

- 2026-11-01 · gui · proposed · GUI-4 的“先看截图”改为指向 `run-scenarios.ps1 -BaselineRef <提交>`：在会话临时目录的临时 worktree 渲染基线提交，与当前工作树逐帧对比，输出变化帧清单与“基线 | 当前 | 差异掩膜”拼图，不用 stash、不改仓库文件（#184）。建议 GUI-4 文字删去隐含的“stash 再渲染旧图”做法，改写“用 -BaselineRef 对比看图”。
