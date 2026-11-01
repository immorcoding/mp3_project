Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Write-NativeUtf8Line 与 Invoke-ExternalCommand 来自 build_helpers.ps1。
. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

# 工作树路径过长时把仓库根 subst 到空闲盘符再运行（#183）。
#
# 背景：Windows 的 MAX_PATH 为 260。`.claude/worktrees/<较长名字>` 下，构建产物（如
# build/firmware-debug/CMakeFiles/.../flash_ftl.c.obj.d）的路径会超限，编译器报
# `opening dependency file ... No such file or directory`。以前靠手动 `subst` 绕过。
#
# 用法：入口脚本把真正的工作放进 Invoke-HarnessInShortRoot 的 -Action；路径不长时动作直接收到原根目录。
# 动作内所有路径都应从传入的根目录派生（脚本互调时 $PSScriptRoot 自然随之变成盘符路径）。
# 证据目录、metadata 等由调用方按真实路径写，不经本函数。

# 默认阈值：仓库根超过这个长度才映射。CMake 把源文件的完整路径嵌进 .obj 目录，所以构建树里的最长路径
# 约为 2R+138（R 为仓库根长度；最长的是 build/host/gui_canvas/ 下的 .obj，实测 R=57 时 252）。
# 超过 259 即失败，对应 R≥61；取 56 留约 10 个字符余量。路径正常（如主工作区）时完全不映射。
$script:HarnessMaxRootLength = 56

function Test-HarnessRootTooLong {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Root,
        [int]$MaxLength = $script:HarnessMaxRootLength
    )

    return ($Root.TrimEnd('\', '/').Length -gt $MaxLength)
}

function Get-HarnessShortRootCandidateLetter {
    <#
    .SYNOPSIS
        返回 E-Z 的候选盘符顺序：起点由根目录的哈希决定，同一根目录每次顺序相同。
    .DESCRIPTION
        CMake 缓存记录源目录的绝对路径；同一工作树每次映射到同一盘符，缓存才不会因盘符变化而失效。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Root
    )

    $letters = [char[]]([int][char]'E'..[int][char]'Z')
    $normalized = ($Root.Replace('/', '\').TrimEnd('\')).ToLowerInvariant()
    # FNV-1a 32 位；用 uint64 取模避免 PowerShell 的有符号溢出。
    [uint64]$hash = 2166136261
    foreach ($character in $normalized.ToCharArray()) {
        $hash = (($hash -bxor [uint64][int]$character) * 16777619) % 4294967296
    }
    $start = [int]($hash % $letters.Count)
    $ordered = New-Object System.Collections.Generic.List[string]
    for ($offset = 0; $offset -lt $letters.Count; $offset++) {
        $ordered.Add([string]$letters[($start + $offset) % $letters.Count])
    }
    return $ordered.ToArray()
}

function Invoke-HarnessSubst {
    <#
    .SYNOPSIS
        运行 subst.exe，成功返回 $true，失败（非零退出码）返回 $false，不抛错。
    .DESCRIPTION
        经 Invoke-ExternalCommand：Windows PowerShell 5.1 在 'Stop' 下对合并 stderr 的原生命令会把 stderr 行
        变成终止错误，而 subst 失败时正是写 stderr；成败只看退出码。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string[]]$Arguments
    )

    try {
        Invoke-ExternalCommand -CommandPath 'subst.exe' -Arguments $Arguments -CaptureOutput | Out-Null
        return $true
    }
    catch {
        return $false
    }
}

function Get-HarnessUsedDriveLetter {
    [CmdletBinding()]
    param()

    $used = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($drive in [System.IO.DriveInfo]::GetDrives()) {
        [void]$used.Add($drive.Name.Substring(0, 1).ToUpperInvariant())
    }
    foreach ($line in @(Invoke-ExternalCommand -CommandPath 'subst.exe' -CaptureOutput)) {
        if ([string]$line -match '^([A-Za-z]):\\: =>') {
            [void]$used.Add($Matches[1].ToUpperInvariant())
        }
    }
    return @($used)
}

function Get-HarnessShortRootLetter {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Root,
        [string[]]$UsedLetter = (Get-HarnessUsedDriveLetter)
    )

    $used = @($UsedLetter | ForEach-Object { $_.ToUpperInvariant() })
    foreach ($letter in (Get-HarnessShortRootCandidateLetter -Root $Root)) {
        if ($used -notcontains $letter) {
            return $letter
        }
    }

    throw '没有空闲盘符（E-Z 均已占用），无法映射短路径；请用 `subst X: /D` 释放一个，或把工作树移到更短的路径。'
}

function Invoke-HarnessFullInShortRoot {
    <#
    .SYNOPSIS
        verify.ps1 的接线：路径过长时经短盘符根运行 scripts/verify_full.ps1，否则在原根运行。
    .DESCRIPTION
        Runner 是测试接缝：param($ScriptPath, $Parameters)，默认用 Invoke-PowerShellScript 真正运行。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [hashtable]$Parameters = @{},
        [int]$MaxLength = $script:HarnessMaxRootLength,
        [scriptblock]$Runner = { param($ScriptPath, $Parameters) Invoke-PowerShellScript -ScriptPath $ScriptPath -Parameters $Parameters }
    )

    $fullParameters = $Parameters
    $fullRunner = $Runner
    Invoke-HarnessInShortRoot -RepositoryRoot $RepositoryRoot -MaxLength $MaxLength -Action {
        param($effectiveRoot)
        & $fullRunner (Join-Path -Path $effectiveRoot -ChildPath 'scripts/verify_full.ps1') $fullParameters
    }
}

function Invoke-HarnessInShortRoot {
    <#
    .SYNOPSIS
        路径过长时把 RepositoryRoot subst 到空闲盘符并以盘符根调用 Action，结束（含失败）卸载映射。
    .DESCRIPTION
        路径不长时不做任何事，Action 收到原根目录。Action 的输出原样返回，异常原样向上传播。
        已存在的映射（别人占用的盘符）不会被动：选盘符前读取已占用列表，占用冲突时顺延到下一个候选。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [int]$MaxLength = $script:HarnessMaxRootLength,
        [Parameter(Mandatory)][scriptblock]$Action
    )

    if (-not (Test-HarnessRootTooLong -Root $RepositoryRoot -MaxLength $MaxLength)) {
        return (& $Action $RepositoryRoot)
    }

    $target = $RepositoryRoot.TrimEnd('\', '/')
    $used = @(Get-HarnessUsedDriveLetter)
    $mapped = $null
    foreach ($letter in (Get-HarnessShortRootCandidateLetter -Root $RepositoryRoot)) {
        if ($used -contains $letter) {
            continue
        }

        # 选定到映射之间别的进程可能抢走同一盘符；subst 失败就试下一个。
        if (Invoke-HarnessSubst -Arguments @("${letter}:", $target)) {
            $mapped = $letter
            break
        }
    }
    if ($null -eq $mapped) {
        throw '没有空闲盘符（E-Z 均已占用或映射失败），无法映射短路径；请用 `subst X: /D` 释放一个，或把工作树移到更短的路径。'
    }

    $shortRoot = "${mapped}:\"
    Write-NativeUtf8Line -Stream Error -Text "路径过长（$($target.Length) > $MaxLength 个字符）：已 subst ${mapped}: -> $target，在 $shortRoot 下运行，结束后卸载。"
    try {
        return (& $Action $shortRoot)
    }
    finally {
        # 当前目录在映射盘上会让卸载失败；先回到真实仓库根。
        if ((Get-Location).Path -like "${mapped}:*") {
            Set-Location -LiteralPath $target
        }
        # 卸载失败只警告：finally 里抛错会盖掉动作的原异常。
        if (-not (Invoke-HarnessSubst -Arguments @("${mapped}:", '/D'))) {
            Write-NativeUtf8Line -Stream Error -Text "警告：卸载 ${mapped}: 失败，请手动执行 subst ${mapped}: /D。"
        }
    }
}
