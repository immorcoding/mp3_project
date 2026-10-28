Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Claude Code 与 Codex 共用的 Agent Hook 判定（HAR-3/HAR-4）。受保护路径复用 Git Hook 的同一份规则。
. (Join-Path -Path (Split-Path -Parent $PSScriptRoot) -ChildPath 'check-generated-write.ps1')
. (Join-Path -Path (Split-Path -Parent $PSScriptRoot) -ChildPath 'check-layer-includes.ps1')
. (Join-Path -Path (Split-Path -Parent $PSScriptRoot) -ChildPath 'shape_writer.ps1')

# Codex 的 apply_patch 不支持 permissionDecision=ask（会放行），需要确认的写入一律拒绝。
$script:AgentHookAskUnsupportedTools = @('apply_patch')

$script:AgentHookCubeMxSourcePatterns = @(
    '(?i)\.ioc$'
    '(?i)(^|/)startup_[^/]*\.s$'
    '(?i)^\.mxproject$'
)

# 完整路径：Tools/external_loader 下的同名头文件是自维护的，不能按文件名匹配。
$script:AgentHookGeneratorConfigHeaders = @(
    'Middlewares/Third_Party/LVGL/lv_conf.h'
    'Middlewares/Third_Party/FreeRTOS/Config/FreeRTOSConfig.h'
    'FATFS/Target/ffconf.h'
    'Core/Inc/stm32h7xx_hal_conf.h'
)

$script:AgentHookUserCodeRoots = @('Core/', 'FATFS/', 'USB_DEVICE/')

# 链接脚本自维护（含手加的资源与字体段），但改动影响内存布局与启动（HWD-2），须逐次经用户确认。
$script:AgentHookLinkerScriptPatterns = @('(?i)\.ld$')

function Get-AgentHookProperty {
    param(
        $Object,
        [Parameter(Mandatory)][string]$Name
    )

    if ($null -eq $Object) {
        return $null
    }

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }

    return $property.Value
}

# Claude 的 Edit/Write/MultiEdit 给 file_path，NotebookEdit 给 notebook_path；Codex 的 apply_patch 给补丁全文。
function Get-AgentHookEditPath {
    param(
        $ToolInput
    )

    $paths = New-Object System.Collections.Generic.List[string]
    foreach ($name in @('file_path', 'notebook_path')) {
        $value = Get-AgentHookProperty -Object $ToolInput -Name $name
        if (-not [string]::IsNullOrWhiteSpace([string]$value)) {
            [void]$paths.Add([string]$value)
        }
    }

    $patch = Get-AgentHookProperty -Object $ToolInput -Name 'command'
    if (-not [string]::IsNullOrWhiteSpace([string]$patch)) {
        $pattern = '(?m)^\*\*\* (?:(?:Add|Update|Delete) File|Move to): (.+?)\s*$'
        foreach ($match in [regex]::Matches([string]$patch, $pattern)) {
            [void]$paths.Add($match.Groups[1].Value)
        }
    }

    return $paths.ToArray()
}

# 相对路径以 BaseDirectory（Hook 输入的 cwd，缺省为仓库根）为基准；规范化后消去 `..`。
# Git Bash 形式 /e/... 视为 E:/...。仓库外路径返回 $null，不归本仓库 Hook 管。
function ConvertTo-AgentHookRelativePath {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$Path,
        [string]$BaseDirectory
    )

    $candidate = ($Path.Trim() -replace '^/([a-zA-Z])/', '$1:/')
    if ([string]::IsNullOrWhiteSpace($BaseDirectory)) {
        $BaseDirectory = $RepositoryRoot
    }
    $BaseDirectory = ($BaseDirectory -replace '^/([a-zA-Z])/', '$1:/')
    if (-not [System.IO.Path]::IsPathRooted($candidate)) {
        $candidate = Join-Path -Path $BaseDirectory -ChildPath $candidate
    }

    $full = [System.IO.Path]::GetFullPath($candidate) -replace '\\', '/'
    $rootPrefix = ([System.IO.Path]::GetFullPath($RepositoryRoot) -replace '\\', '/').TrimEnd('/') + '/'
    if ($full.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($rootPrefix.Length)
    }

    return $null
}

# Core/、FATFS/、USB_DEVICE/ 下的已有文件：含 USER CODE 区返回 'usercode'，
# 仅带 ST 生成声明返回 'generated'，其余（含新文件与自维护的 bsp_driver_user_diskio.*）返回 $null。
function Get-AgentHookCubeMxFileKind {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$RelativePath
    )

    if ($RelativePath -match '(?i)^FATFS/Target/bsp_driver_user_diskio\.') {
        return $null
    }

    $underRoot = $false
    foreach ($root in $script:AgentHookUserCodeRoots) {
        if ($RelativePath.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
            $underRoot = $true
            break
        }
    }
    if (-not $underRoot) {
        return $null
    }

    $fullPath = Join-Path -Path $RepositoryRoot -ChildPath $RelativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        return $null
    }

    if (Select-String -LiteralPath $fullPath -Pattern 'USER CODE BEGIN' -SimpleMatch -Quiet) {
        return 'usercode'
    }
    $header = @(Get-Content -LiteralPath $fullPath -TotalCount 40) -join "`n"
    if ($header -match 'STMicroelectronics') {
        return 'generated'
    }

    return $null
}

# 返回 @{ Decision = 'allow'|'ask'|'deny'; Reason = '...' }。
function Get-AgentHookEditDecision {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$RelativePath,
        [string]$ToolName = '',
        # 给出两者时检查 shape 写入分支；分离 HEAD 传 $null 表示不判断。
        [string]$CurrentBranch,
        [string]$WriterBranch
    )

    $normalized = ($RelativePath -replace '\\', '/').TrimStart('/')
    $askOrDeny = if ($script:AgentHookAskUnsupportedTools -contains $ToolName) { 'deny' } else { 'ask' }

    if ($script:AgentHookGeneratorConfigHeaders -icontains $normalized) {
        return @{ Decision = $askOrDeny; Reason = "$normalized 是生成器配置头，改动前须经用户确认（AGENTS.md 硬规则）。" }
    }

    if (Test-GeneratedWriteProtectedPath -RelativePath $normalized) {
        return @{ Decision = 'deny'; Reason = "$normalized 属于 CubeMX/Vendor 生成目录，只认生成器（ARC-2）；请给出源工程配置步骤，由用户导出。" }
    }

    foreach ($pattern in $script:AgentHookLinkerScriptPatterns) {
        if ($normalized -match $pattern) {
            return @{ Decision = $askOrDeny; Reason = "$normalized 是自维护链接脚本，改动影响内存布局与启动（HWD-2），须经用户确认。" }
        }
    }

    foreach ($pattern in $script:AgentHookCubeMxSourcePatterns) {
        if ($normalized -match $pattern) {
            return @{ Decision = 'deny'; Reason = "$normalized 由 CubeMX 维护（ARC-2）；参数改 io_sheet.ioc 后由用户重新生成。" }
        }
    }

    $cubeMxKind = Get-AgentHookCubeMxFileKind -RepositoryRoot $RepositoryRoot -RelativePath $normalized
    if ($cubeMxKind -eq 'usercode') {
        return @{ Decision = $askOrDeny; Reason = "$normalized 是 CubeMX 生成文件，只准在 USER CODE 区放对自维护入口的调用或转发（ARC-3），须经用户确认。" }
    }
    if ($cubeMxKind -eq 'generated') {
        return @{ Decision = 'deny'; Reason = "$normalized 带 ST 生成声明且没有 USER CODE 区，只认生成器（ARC-2）。" }
    }

    if ((-not [string]::IsNullOrWhiteSpace($CurrentBranch)) -and (-not [string]::IsNullOrWhiteSpace($WriterBranch)) -and
        ($CurrentBranch -ne $WriterBranch) -and (Test-ShapeAreaPath -RelativePath $normalized)) {
        return @{ Decision = 'deny'; Reason = (Get-ShapeWriterViolationMessage -Branch $CurrentBranch -WriterBranch $WriterBranch -Path @($normalized)) }
    }

    return @{ Decision = 'allow'; Reason = '' }
}

# gh 文本参数（--body/--title/--comment/--notes/-b/-t）与 api 字段（-f/-F/--field/--raw-field 的 body=、title=）
# 用双引号包裹且内含反引号。\\?" 容许外层 `-Command "…"` 里转义的内层引号；--body-file 与单引号参数不匹配。
$script:AgentHookGhTextFlagPattern = '(?<![\w-])(?:--(?:body|title|comment|notes)|-[bt])(?:\s+|=)\\?"(?:[^"\\`]|\\[^`])*`'
$script:AgentHookGhApiFieldPattern = '(?<![\w-])(?:-[fF]|--field|--raw-field)(?:\s+|=)' +
    '(?:\\?"(?:body|title)=(?:[^"\\`]|\\[^`])*`|(?:body|title)=\\?"(?:[^"\\`]|\\[^`])*`)'

# 按引号外的 && || ; | & 与换行把命令切成段；单、双引号内的分隔符不切。
function Split-AgentHookCommandSegment {
    param(
        [AllowEmptyString()][string]$Command
    )

    $segments = New-Object System.Collections.Generic.List[string]
    $current = New-Object System.Text.StringBuilder
    $quote = [char]0
    for ($i = 0; $i -lt $Command.Length; $i++) {
        $c = $Command[$i]
        if ($quote -ne [char]0) {
            [void]$current.Append($c)
            if (($quote -eq '"') -and ($c -eq '\') -and ($i + 1 -lt $Command.Length)) {
                $i++
                [void]$current.Append($Command[$i])
            }
            elseif ($c -eq $quote) {
                $quote = [char]0
            }
            continue
        }
        if (($c -eq '"') -or ($c -eq "'")) {
            $quote = $c
            [void]$current.Append($c)
        }
        elseif (($c -eq '\') -and ($i + 1 -lt $Command.Length)) {
            [void]$current.Append($c)
            $i++
            [void]$current.Append($Command[$i])
        }
        elseif ($c -in @(';', '|', '&', "`n", "`r")) {
            [void]$segments.Add($current.ToString())
            [void]$current.Clear()
        }
        else {
            [void]$current.Append($c)
        }
    }
    [void]$segments.Add($current.ToString())
    return $segments.ToArray()
}

# 返回违规原因；命令合规时返回 $null。
function Get-AgentHookShellViolation {
    param(
        [AllowEmptyString()][string]$Command
    )

    if ([string]::IsNullOrWhiteSpace($Command)) {
        return $null
    }

    # --no-verify：git 接受唯一前缀（--no-veri…），也可能带引号；宁可误拦含该字样的提交信息。
    if ($Command -match '\bgit\b' -and $Command -match '(?i)(^|[\s''"=])--no-veri') {
        return '禁止跳过 Git Hook（--no-verify）：FAST/CHANGED 是唯一自动闸门（GIT-4）。'
    }

    # gh 的 --body/--title/-b 等用双引号包裹且内含反引号：bash 当命令替换、PowerShell 当转义，正文会被吃掉。
    # 先摘掉 heredoc 正文（引号化的 heredoc 里反引号是字面量）；--body-file、单引号参数不受影响。
    $withoutHeredoc = [regex]::Replace($Command,
        '(?s)<<-?[ \t]*(?<q>[''"]?)(?<tag>\w+)\k<q>(?<rest>[^\r\n]*)\r?\n.*?\r?\n[ \t]*\k<tag>[ \t]*(?=\r?\n|$|\))',
        '<<${tag}${rest}')
    # 按引号外的 && || ; | & 换行切段，只检查含 gh 的那一段，别的命令段（如 git tag -t "`x`"）不受影响。
    foreach ($ghSegment in (Split-AgentHookCommandSegment -Command $withoutHeredoc)) {
        if (($ghSegment -match '(?i)(^|[\s/\\(])gh(\.exe|-retry\.ps1)?(\s|$)') -and
            (($ghSegment -match $script:AgentHookGhTextFlagPattern) -or ($ghSegment -match $script:AgentHookGhApiFieldPattern))) {
            return 'gh 的 --body/--title/-b 或 api 的 -f body=/title= 等双引号参数里不能含反引号（bash 当命令替换、PowerShell 当转义）；把正文写进临时文件（Write 工具，或 bash 里 cat > 文件 <<''EOF'' 单引号 heredoc），再用 --body-file <临时文件>。'
        }
    }

    # 其余规则先去掉引号内的内容（提交信息等），再按 ; & | 换行 切分命令段。
    $unquoted = $Command -replace '"(?:[^"\\]|\\.)*"', '""' -replace "'[^']*'", "''"
    foreach ($segment in ($unquoted -split '[;&|\r\n]+')) {
        if ($segment -notmatch '\bgit\b') {
            continue
        }
        if ($segment -match '\bcommit\b' -and $segment -cmatch '\s-[a-zA-Z]*n[a-zA-Z]*(\s|$)') {
            return '禁止跳过 Git Hook（commit -n）：FAST/CHANGED 是唯一自动闸门（GIT-4）。'
        }
        # 只看真正的 `git [全局选项] push` 调用，且只检查 push 之后的参数；否则
        # `powershell -File x.ps1 -Push ... repo.git` 这类命令会因 -File 与 .git 被误判为强推。
        # git 可带路径前缀；带值的全局选项两种写法（--opt=v / --opt v）都要跳过。
        # --for 覆盖 git 接受的长选项唯一前缀（--forc、--force-with-lease 等）。
        $forcePattern = '(\s--for|\s-[a-zA-Z]*f[a-zA-Z]*(\s|$)|\s\+\S)'
        $pushPattern = '(?i)(^|[\s/\\])git(\.exe)?\s+' +
            '(?:(?:-C|-c|--git-dir|--work-tree|--namespace|--super-prefix|--config-env|--exec-path)(?:=\S+|\s+\S+)\s+|--?[\w.-]+(?:=\S+)?\s+)*' +
            'push\b(?<args>.*)$'
        if ($segment -match $pushPattern) {
            if ($Matches['args'] -match $forcePattern) {
                return '禁止 Agent 强推任何分支（GIT-4）；确需覆盖远端时由用户本人执行。'
            }
        }
        # `-c alias.x=push` 让子命令名不再是 push，按整段保守判定。
        if ($segment -match '(?i)\balias\.\S*=\S*push\b' -and $segment -match $forcePattern) {
            return '禁止 Agent 强推任何分支（GIT-4）；确需覆盖远端时由用户本人执行。'
        }
        # 只拦写入（赋值、--unset、-c 临时覆盖）；--get/--list/`git config get` 只读放行。
        $hooksPathRead = ($segment -match '(?i)\bconfig\s+(?:--\S+\s+)*(?:--get(?:-all|-regexp)?|--list|-l|get)\b') -and
            ($segment -notmatch '(?i)\s-c\s+core\.hooksPath|--unset|--replace-all|--add\b')
        if (($segment -match '\bcore\.hooksPath\b') -and -not $hooksPathRead) {
            return '禁止改写 core.hooksPath；Hook 安装只走 scripts/install-git-hooks.ps1。'
        }
    }

    # 只放行对 ALLOW_GENERATED_UPDATE 的读取；任何赋值、setx、Set-Item/New-Item env:、export、cmd set 都拦。
    if ($Command -match '(?i)ALLOW_GENERATED_UPDATE' -and
        $Command -match '(?i)ALLOW_GENERATED_UPDATE\s*=|\bsetx\b|\bSet-Item\b|\bNew-Item\b|\bSet-Content\b|SetEnvironmentVariable|\bexport\b|(^|[\s;&])set\s+ALLOW_GENERATED_UPDATE') {
        return '禁止 Agent 设置 ALLOW_GENERATED_UPDATE；只有维护者本人在当前会话为重新导出设置。'
    }

    return $null
}

function Read-AgentHookInput {
    $stdin = [Console]::In.ReadToEnd().TrimStart([char]0xFEFF)
    if ([string]::IsNullOrWhiteSpace($stdin)) {
        return $null
    }

    return $stdin | ConvertFrom-Json
}

function Initialize-AgentHookConsole {
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [Console]::InputEncoding = $utf8
    [Console]::OutputEncoding = $utf8
}

function Get-AgentHookRepositoryRoot {
    return (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}
