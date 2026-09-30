# Triage 标签

GitHub 标签字符串与 `triage` skill 的规范角色同名。每个经过 triage 的 issue 恰好带一个分类与一个状态。

## 分类

| 标签 | 含义 |
| --- | --- |
| `bug` | 行为错误或回归。 |
| `enhancement` | 新功能或改进。 |

## 状态

| 标签 | 含义 |
| --- | --- |
| `needs-triage` | 新建事项，尚未完成范围和优先级判断。 |
| `needs-info` | 缺少复现条件、需求细节或技术信息。 |
| `ready-for-agent` | 范围和验收标准明确，可交由执行代理处理。 |
| `ready-for-human` | 需要用户做决策、提供材料或实施人工验证。 |
| `wontfix` | 已决定不处理，并记录原因。 |

## 附加标签

| 标签 | 含义 |
| --- | --- |
| `hw:pending` | 软件验证已过，等待板上证据（WF-3）；与状态标签并存。 |
| `wayfinder:map` | wayfinder 地图 issue。 |
| `wayfinder:research` / `wayfinder:prototype` / `wayfinder:grilling` / `wayfinder:task` | wayfinder ticket 类型。 |
