[CmdletBinding()]
param(
    [switch]$Check
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

# HAR-2：Skill 与审阅 Agent 的正文只在 .agents/；.claude/ 与 .codex/ 下的对应文件由本脚本生成。
$script:AgentConfigGeneratedRoots = @('.claude/agents', '.claude/skills', '.codex/agents')

function ConvertTo-AgentConfigText {
    param(
        [AllowEmptyString()][string]$Text
    )

    return ($Text.TrimStart([char]0xFEFF) -replace "`r`n", "`n")
}

function Read-AgentConfigText {
    param(
        [Parameter(Mandatory)][string]$Path
    )

    return ConvertTo-AgentConfigText -Text ([System.IO.File]::ReadAllText($Path, (New-Object System.Text.UTF8Encoding($false))))
}

# 解析 `---` 包围的 name/description 与其后的正文。
function Get-AgentReviewerDefinition {
    param(
        [Parameter(Mandatory)][string]$Path
    )

    $text = Read-AgentConfigText -Path $Path
    if ($text -notmatch '(?s)^---\n(.*?)\n---\n(.*)$') {
        throw "审阅者定义缺少 frontmatter：$Path"
    }

    $frontmatter = $Matches[1]
    $body = $Matches[2].Trim()
    $fields = @{}
    foreach ($line in $frontmatter -split "`n") {
        if ($line -match '^([a-z_]+):\s*(.*)$') {
            $fields[$Matches[1]] = $Matches[2].Trim()
        }
    }

    foreach ($required in @('name', 'description')) {
        if (-not $fields.ContainsKey($required) -or [string]::IsNullOrWhiteSpace($fields[$required])) {
            throw "审阅者定义缺少 ${required}：$Path"
        }
    }
    if ($body.Contains("'''")) {
        throw "审阅者正文不得包含 ''' （Codex TOML 字面量字符串的定界符）：$Path"
    }

    return @{ Name = $fields['name']; Description = $fields['description']; Body = $body; Source = $Path }
}

# 返回 仓库相对路径 → 期望内容（LF）的有序表。
function Get-AgentConfigExpectedFile {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot
    )

    $expected = [ordered]@{}

    $reviewerRoot = Join-Path -Path $RepositoryRoot -ChildPath '.agents/reviewers'
    if (Test-Path -LiteralPath $reviewerRoot -PathType Container) {
        foreach ($file in @(Get-ChildItem -LiteralPath $reviewerRoot -Filter '*.md' -File | Sort-Object Name)) {
            $reviewer = Get-AgentReviewerDefinition -Path $file.FullName
            $source = ".agents/reviewers/$($file.Name)"
            $tomlDescription = $reviewer.Description -replace '\\', '\\' -replace '"', '\"'

            $expected[".codex/agents/$($reviewer.Name).toml"] = (@(
                "# 由 scripts/sync-agent-config.ps1 从 $source 生成，勿手改。"
                "name = `"$($reviewer.Name)`""
                "description = `"$tomlDescription`""
                'sandbox_mode = "read-only"'
                "developer_instructions = '''"
                $reviewer.Body
                "'''"
            ) -join "`n") + "`n"

            $expected[".claude/agents/$($reviewer.Name).md"] = (@(
                '---'
                "name: $($reviewer.Name)"
                "description: $($reviewer.Description)"
                'tools: Read, Grep, Glob, Bash'
                '---'
                ''
                "<!-- 由 scripts/sync-agent-config.ps1 从 $source 生成，勿手改。 -->"
                ''
                $reviewer.Body
            ) -join "`n") + "`n"
        }
    }

    $skillRoot = Join-Path -Path $RepositoryRoot -ChildPath '.agents/skills'
    if (Test-Path -LiteralPath $skillRoot -PathType Container) {
        $skillPrefix = (Resolve-Path -LiteralPath $skillRoot).Path.TrimEnd('\') + '\'
        foreach ($file in @(Get-ChildItem -LiteralPath $skillRoot -Recurse -File | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($skillPrefix.Length) -replace '\\', '/'
            $expected[".claude/skills/$relative"] = Read-AgentConfigText -Path $file.FullName
        }
    }

    return $expected
}

function Get-AgentConfigActualPath {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot
    )

    $rootPrefix = (Resolve-Path -LiteralPath $RepositoryRoot).Path.TrimEnd('\') + '\'
    $paths = New-Object System.Collections.Generic.List[string]
    foreach ($generatedRoot in $script:AgentConfigGeneratedRoots) {
        $fullRoot = Join-Path -Path $RepositoryRoot -ChildPath $generatedRoot
        if (-not (Test-Path -LiteralPath $fullRoot -PathType Container)) {
            continue
        }
        foreach ($file in @(Get-ChildItem -LiteralPath $fullRoot -Recurse -File)) {
            [void]$paths.Add(($file.FullName.Substring($rootPrefix.Length) -replace '\\', '/'))
        }
    }

    return $paths.ToArray()
}

# 返回漂移描述；一致时为空数组。
function Get-AgentConfigDrift {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot
    )

    $expected = Get-AgentConfigExpectedFile -RepositoryRoot $RepositoryRoot
    $drift = New-Object System.Collections.Generic.List[string]
    foreach ($relative in $expected.Keys) {
        $fullPath = Join-Path -Path $RepositoryRoot -ChildPath $relative
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            [void]$drift.Add("缺少生成文件：$relative")
            continue
        }
        if ((Read-AgentConfigText -Path $fullPath) -cne $expected[$relative]) {
            [void]$drift.Add("生成文件与 .agents/ 正文不一致：$relative")
        }
    }
    foreach ($relative in @(Get-AgentConfigActualPath -RepositoryRoot $RepositoryRoot)) {
        if (-not $expected.Contains($relative)) {
            [void]$drift.Add("多余文件（.agents/ 中无对应正文）：$relative")
        }
    }

    return $drift.ToArray()
}

function Invoke-AgentConfigSync {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot
    )

    $expected = Get-AgentConfigExpectedFile -RepositoryRoot $RepositoryRoot
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    foreach ($relative in $expected.Keys) {
        $fullPath = Join-Path -Path $RepositoryRoot -ChildPath $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $fullPath) -Force | Out-Null
        [System.IO.File]::WriteAllText($fullPath, $expected[$relative], $utf8)
    }
    foreach ($relative in @(Get-AgentConfigActualPath -RepositoryRoot $RepositoryRoot)) {
        if (-not $expected.Contains($relative)) {
            Remove-Item -LiteralPath (Join-Path -Path $RepositoryRoot -ChildPath $relative) -Force
        }
    }

    return $expected.Count
}

$isDotSourced = $MyInvocation.InvocationName -eq '.'
if (-not $isDotSourced) {
    Complete-Utf8EntryScript -Action {
        $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
        if ($Check) {
            $drift = @(Get-AgentConfigDrift -RepositoryRoot $repositoryRoot)
            if ($drift.Count -gt 0) {
                throw ("Agent 配置与 .agents/ 正文不一致，运行 ./scripts/sync-agent-config.ps1 重新生成：" + [Environment]::NewLine + ($drift -join [Environment]::NewLine))
            }
            Write-NativeUtf8Line -Text 'Agent 配置一致：.claude/ 与 .codex/ 的生成文件与 .agents/ 正文相同。'
        }
        else {
            $count = Invoke-AgentConfigSync -RepositoryRoot $repositoryRoot
            Write-NativeUtf8Line -Text "已从 .agents/ 生成 $count 个 Agent 配置文件。"
        }
    }
}
