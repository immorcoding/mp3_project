Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'sync-agent-config.ps1')

function Assert-SyncEqual {
    param(
        [Parameter(Mandatory)] $Expected,
        [AllowNull()] $Actual,
        [Parameter(Mandatory)][string]$Message
    )

    if ([string]$Expected -ne [string]$Actual) {
        throw "$Message 期望 '$Expected'，实际 '$Actual'。"
    }
}

function Write-FixtureText {
    param([string]$Root, [string]$Relative, [string]$Text)
    $path = Join-Path -Path $Root -ChildPath $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
    [System.IO.File]::WriteAllText($path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

$fixtureRoot = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ('agent-config-' + [Guid]::NewGuid().ToString('N'))
try {
    Write-FixtureText -Root $fixtureRoot -Relative '.agents/reviewers/probe.md' -Text "---`nname: probe`ndescription: 探针 `"审阅`" 者`n---`n`n正文第一段。`n"
    Write-FixtureText -Root $fixtureRoot -Relative '.agents/skills/demo/SKILL.md' -Text "---`nname: demo`n---`n`n技能正文`n"
    Write-FixtureText -Root $fixtureRoot -Relative '.agents/skills/demo/references/list.md' -Text "清单`n"

    $missing = @(Get-AgentConfigDrift -RepositoryRoot $fixtureRoot)
    Assert-SyncEqual -Expected 4 -Actual $missing.Count -Message '未生成时应报 4 个缺失文件'

    Assert-SyncEqual -Expected 4 -Actual (Invoke-AgentConfigSync -RepositoryRoot $fixtureRoot) -Message '应生成 2 个审阅者包装与 2 个 Skill 文件'
    Assert-SyncEqual -Expected 0 -Actual @(Get-AgentConfigDrift -RepositoryRoot $fixtureRoot).Count -Message '同步后应无漂移'

    $toml = [System.IO.File]::ReadAllText((Join-Path $fixtureRoot '.codex/agents/probe.toml'))
    if ($toml -notmatch 'description = "探针 \\"审阅\\" 者"' -or $toml -notmatch "developer_instructions = '''\n正文第一段。\n'''") {
        throw "Codex TOML 生成不正确：$toml"
    }

    # 行尾差异（Git autocrlf）不算漂移。
    $skillCopy = Join-Path $fixtureRoot '.claude/skills/demo/SKILL.md'
    [System.IO.File]::WriteAllText($skillCopy, ([System.IO.File]::ReadAllText($skillCopy) -replace "`n", "`r`n"))
    Assert-SyncEqual -Expected 0 -Actual @(Get-AgentConfigDrift -RepositoryRoot $fixtureRoot).Count -Message 'CRLF 不应视为漂移'

    $skillText = [System.IO.File]::ReadAllText($skillCopy) -replace "`r`n", "`n"
    if ($skillText -notmatch "(?s)^---\nname: demo\n---\n\n<!-- 由 scripts/sync-agent-config\.ps1 .*? -->\n\n技能正文\n$") {
        throw "Skill 副本应在 frontmatter 后插入生成标记：$skillText"
    }

    # 不带标记的本地私有 agent / skill 不算漂移，也不被删除。
    Write-FixtureText -Root $fixtureRoot -Relative '.claude/agents/my-private.md' -Text "---`nname: mine`n---`n私有`n"
    Write-FixtureText -Root $fixtureRoot -Relative '.claude/skills/my-skill/SKILL.md' -Text "---`nname: my-skill`n---`n私有`n"
    Assert-SyncEqual -Expected 0 -Actual @(Get-AgentConfigDrift -RepositoryRoot $fixtureRoot).Count -Message '本地私有配置不应视为漂移'

    # 手改生成物、带标记的多余文件都是漂移。
    $managedPrivate = [System.IO.File]::ReadAllText((Join-Path $fixtureRoot '.codex/agents/probe.toml')) -replace 'name = "probe"', 'name = "stale"'
    Write-FixtureText -Root $fixtureRoot -Relative '.claude/agents/probe.md' -Text '手改'
    Write-FixtureText -Root $fixtureRoot -Relative '.codex/agents/stale.toml' -Text $managedPrivate
    $drift = @(Get-AgentConfigDrift -RepositoryRoot $fixtureRoot)
    Assert-SyncEqual -Expected 2 -Actual $drift.Count -Message '手改与带标记的多余文件应各报一处'

    [void](Invoke-AgentConfigSync -RepositoryRoot $fixtureRoot)
    Assert-SyncEqual -Expected 0 -Actual @(Get-AgentConfigDrift -RepositoryRoot $fixtureRoot).Count -Message '重新同步应恢复并清掉多余文件'
    if (Test-Path -LiteralPath (Join-Path $fixtureRoot '.codex/agents/stale.toml')) {
        throw '重新同步后带标记的多余文件应被删除'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $fixtureRoot '.claude/skills/my-skill/SKILL.md'))) {
        throw '重新同步不得删除本地私有 Skill'
    }

    # 正文 Skill 删除后，残留的受管副本被检出并在同步时清掉。
    Remove-Item -LiteralPath (Join-Path $fixtureRoot '.agents/skills/demo') -Recurse -Force
    Assert-SyncEqual -Expected 2 -Actual @(Get-AgentConfigDrift -RepositoryRoot $fixtureRoot).Count -Message '已删 Skill 的两个副本文件应报漂移'
    [void](Invoke-AgentConfigSync -RepositoryRoot $fixtureRoot)
    Assert-SyncEqual -Expected 0 -Actual @(Get-AgentConfigDrift -RepositoryRoot $fixtureRoot).Count -Message '同步后应清掉已删 Skill 的副本'

    # 最后一个 reviewer 退役后，应清除两个受管包装并保留本地私有配置。
    Remove-Item -LiteralPath (Join-Path $fixtureRoot '.agents/reviewers/probe.md')
    Assert-SyncEqual -Expected 2 -Actual @(Get-AgentConfigDrift -RepositoryRoot $fixtureRoot).Count -Message '已删 reviewer 的两个包装应报漂移'
    Assert-SyncEqual -Expected 0 -Actual (Invoke-AgentConfigSync -RepositoryRoot $fixtureRoot) -Message '空正文集合应生成零文件'
    Assert-SyncEqual -Expected 0 -Actual @(Get-AgentConfigDrift -RepositoryRoot $fixtureRoot).Count -Message '空正文集合清理后应无漂移'
    if ((Test-Path -LiteralPath (Join-Path $fixtureRoot '.claude/agents/probe.md')) -or
        (Test-Path -LiteralPath (Join-Path $fixtureRoot '.codex/agents/probe.toml'))) {
        throw '已退役 reviewer 的受管包装仍存在'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $fixtureRoot '.claude/agents/my-private.md'))) {
        throw '清理 reviewer 不得删除本地私有 Agent'
    }

    # 定义不合法时拒绝生成。
    Write-FixtureText -Root $fixtureRoot -Relative '.agents/reviewers/bad.md' -Text "---`nname: bad`n---`n正文`n"
    $threw = $false
    try { [void](Get-AgentConfigExpectedFile -RepositoryRoot $fixtureRoot) } catch { $threw = $true }
    Assert-SyncEqual -Expected $true -Actual $threw -Message '缺 description 应报错'

    Write-FixtureText -Root $fixtureRoot -Relative '.agents/reviewers/bad.md' -Text "---`nname: bad`ndescription: x`n---`n含 ''' 的正文`n"
    $threw = $false
    try { [void](Get-AgentConfigExpectedFile -RepositoryRoot $fixtureRoot) } catch { $threw = $true }
    Assert-SyncEqual -Expected $true -Actual $threw -Message "正文含 ''' 应报错"
}
finally {
    if (Test-Path -LiteralPath $fixtureRoot) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}

Write-Output 'sync-agent-config 函数测试通过。'
