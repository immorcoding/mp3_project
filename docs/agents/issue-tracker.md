# Issue tracker：GitHub

本仓库的事项、spec、ticket 与 wayfinder 地图都是 GitHub Issues（`immorcoding/mp3_project`），一律用 `gh` CLI 操作；`gh` 在仓库内会从 `git remote -v` 推断仓库。标准见 `docs/shape/workflow.md`。

## 约定

- **新建**：`gh issue create --title "..." --body "..."`；多行正文用 heredoc。
- **读取**：`gh issue view <number> --comments`，同时取标签。
- **列表**：`gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'`，按需加 `--label`、`--state`。
- **评论**：`gh issue comment <number> --body "..."`
- **标签**：`gh issue edit <number> --add-label "..."` / `--remove-label "..."`；标签表见 [triage-labels.md](triage-labels.md)。
- **关闭**：`gh issue close <number> --comment "..."`

## 固件专有约定

- 需要上板的 ticket 在软件验证通过后挂 `hw:pending`；板上证据（固件版本、观察现象、日志）作为评论贴出后移除标签并关闭。
- 置顶 issue「板上状态」只记设备此刻的事实：已烧录的固件提交、NOR 中的资源包版本、待上板项列表。每次烧录后更新正文，不写过程。
- 稳定事实仍进架构文档、Module README 与 ADR；issue 只放进度、决定过程与证据。

## PR 作为请求入口

**PRs as a request surface: no.**（若要让 `/triage` 处理外部 PR，改为 `yes`。）

为 `yes` 时，PR 与 issue 共用标签和状态：`gh pr view <number> --comments`、`gh pr diff <number>`；外部 PR 用 `gh pr list --state open --json number,title,body,labels,author,authorAssociation,comments` 后只保留 `authorAssociation` 为 `CONTRIBUTOR`、`FIRST_TIME_CONTRIBUTOR` 或 `NONE` 的项。GitHub 的 issue 与 PR 共用编号，裸 `#42` 先 `gh pr view 42`，失败再 `gh issue view 42`。

## Skill 说「发布到 issue tracker」时

新建一个 GitHub issue。

## Skill 说「读取相关 ticket」时

`gh issue view <number> --comments`。

## Wayfinding operations

供 `/wayfinder` 使用。**map** 是一个 issue，ticket 是它的 **child** issue。

- **Map**：一个带 `wayfinder:map` 标签的 issue，正文含 Destination / Notes / Decisions so far / Not yet specified / Out of scope。`gh issue create --label wayfinder:map`。
- **Child ticket**：以 GitHub sub-issue 挂到 map（`gh api` 的 sub-issues 端点）。未启用 sub-issue 时，把 child 列进 map 正文的任务列表，并在 child 正文首行写 `Part of #<map>`。标签 `wayfinder:<type>`（`research` / `prototype` / `grilling` / `task`）。认领后指派给驱动者。
- **Blocking**：用 GitHub 原生 issue dependencies：`gh api --method POST repos/<owner>/<repo>/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`，其中 `<blocker-db-id>` 是阻塞方的数字 **database id**（`gh api repos/<owner>/<repo>/issues/<n> --jq .id`），不是 `#number` 或 `node_id`。`issue_dependencies_summary.blocked_by` 给出仍开着的阻塞数。不可用时在 child 正文首行写 `Blocked by: #<n>, #<n>`。所有阻塞方关闭即解除阻塞。
- **Frontier 查询**：列出 map 的开放 child，去掉有开放阻塞方或已有指派人的，按 map 顺序取第一个。
- **认领**：`gh issue edit <n> --add-assignee @me`，作为本场第一次写入。
- **解决**：`gh issue comment <n> --body "<answer>"`，再 `gh issue close <n>`，然后在 map 的 Decisions so far 追加一行要点加链接。
