[CmdletBinding()]
param(
    # 默认是本脚本所在仓库的根；自测传入夹具仓库或目录。
    [string]$RepositoryRoot,

    # 只检查补丁是否都已生效，不修改文件；有未生效的补丁就失败并给出重打命令（FAST 用）。
    [switch]$Check
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Vendor 补丁的重打与核对（HAR-15、ADR-0022）。
#
# `patches/<上游>/*.patch` 是对 Vendor 源码的本仓库补丁，文件名按字典序依次应用。CubeMX 重新导出或上游更新会静默覆盖
# 补丁后的 Vendor 文件，所以：
#   - 默认模式（重打）：可重复执行。逐个补丁判断状态——反向检查通过说明已应用，跳过；正向检查通过则应用；
#     两者都不通过是冲突，停下并报出 git 给出的文件与行位置，交人工改写 patch。
#   - -Check：只读。任何补丁未生效就失败，报“<上游> 补丁未应用”并给出重打命令。
# 补丁落到 `Middlewares/Third_Party/FatFs/` 等受保护目录：本脚本只改工作树，提交须由用户设 ALLOW_GENERATED_UPDATE
# 后进行（HAR-4）；助手不设置、不写入该变量。
#
# 补丁以 LF 换行存放（.gitattributes 固定）；`git apply` 会按 core.autocrlf 把 CRLF 工作树文件折算后再比对。

. (Join-Path -Path $PSScriptRoot -ChildPath 'harness_helpers.ps1')

# 目录名到显示名；未列出的上游直接用目录名。
$script:VendorDisplayName = @{ fatfs = 'FatFs' }

function Get-VendorPatch {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot
    )

    $patchRoot = Join-Path -Path $RepositoryRoot -ChildPath 'patches'
    if (-not (Test-Path -LiteralPath $patchRoot -PathType Container)) {
        return @()
    }

    $patches = New-Object System.Collections.Generic.List[object]
    foreach ($upstream in (Get-ChildItem -LiteralPath $patchRoot -Directory | Sort-Object -Property Name)) {
        foreach ($file in (Get-ChildItem -LiteralPath $upstream.FullName -File -Filter '*.patch' | Sort-Object -Property Name)) {
            $display = if ($script:VendorDisplayName.ContainsKey($upstream.Name)) { $script:VendorDisplayName[$upstream.Name] } else { $upstream.Name }
            [void]$patches.Add([pscustomobject]@{
                    Upstream = $display
                    Name     = $file.Name
                    FullPath = $file.FullName
                })
        }
    }
    return $patches.ToArray()
}

# 在仓库根（或夹具目录）运行 git apply，返回退出码与输出。
function Invoke-VendorGitApply {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string[]]$Arguments
    )

    $previousPreference = $ErrorActionPreference
    Push-Location -LiteralPath $RepositoryRoot
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& git apply @Arguments 2>&1 | ForEach-Object { [string]$_ })
        $exitCode = $LASTEXITCODE
        # 检查性调用的非零退出码是正常结果，不能漏成整个脚本的退出码。
        $global:LASTEXITCODE = 0
    }
    finally {
        $ErrorActionPreference = $previousPreference
        Pop-Location
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

# 返回 Applied（反向检查通过）、Applicable（能干净应用）或 Conflict（两者皆否，Detail 为 git 的报错）。
function Get-VendorPatchState {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$PatchPath
    )

    $reverse = Invoke-VendorGitApply -RepositoryRoot $RepositoryRoot -Arguments @('--reverse', '--check', $PatchPath)
    if ($reverse.ExitCode -eq 0) {
        return [pscustomobject]@{ State = 'Applied'; Detail = '' }
    }

    $forward = Invoke-VendorGitApply -RepositoryRoot $RepositoryRoot -Arguments @('--check', $PatchPath)
    if ($forward.ExitCode -eq 0) {
        return [pscustomobject]@{ State = 'Applicable'; Detail = '' }
    }
    return [pscustomobject]@{ State = 'Conflict'; Detail = ($forward.Output -join [Environment]::NewLine) }
}

function Invoke-VendorPatchApply {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [switch]$Check
    )

    $repairCommand = './scripts/apply-vendor-patches.ps1'
    foreach ($patch in (Get-VendorPatch -RepositoryRoot $RepositoryRoot)) {
        $label = "$($patch.Upstream)/$($patch.Name)"
        $state = Get-VendorPatchState -RepositoryRoot $RepositoryRoot -PatchPath $patch.FullPath

        switch ($state.State) {
            'Applied' {
                Write-Output "已应用，跳过：$label"
            }
            'Applicable' {
                if ($Check) {
                    throw "$($patch.Upstream) 补丁未应用：$label。CubeMX 重新导出或上游更新后需要重打，运行：$repairCommand"
                }
                $applied = Invoke-VendorGitApply -RepositoryRoot $RepositoryRoot -Arguments @($patch.FullPath)
                if ($applied.ExitCode -ne 0) {
                    throw "应用补丁失败：$label$([Environment]::NewLine)$($applied.Output -join [Environment]::NewLine)"
                }
                Write-Output "已应用：$label"
            }
            default {
                $guidance = if ($Check) {
                    "$($patch.Upstream) 补丁未应用，且补丁与当前文件冲突：$label。"
                }
                else {
                    "补丁与当前文件冲突，已停止：$label。"
                }
                throw ($guidance + [Environment]::NewLine + $state.Detail + [Environment]::NewLine +
                    '上游文件相对补丁有变化，请人工改写该 patch 的上下文（并确认新增函数仍成立）后重新运行：' + $repairCommand)
            }
        }
    }
}

$isDotSourced = $MyInvocation.InvocationName -eq '.'
if (-not $isDotSourced) {
    Complete-Utf8EntryScript -Action {
        $root = if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) { Get-RepositoryRoot -EntryScriptPath $PSCommandPath } else { $RepositoryRoot }
        Invoke-VendorPatchApply -RepositoryRoot $root -Check:$Check
        if (-not $Check) {
            Write-Output 'Vendor 补丁已就绪。补丁落在受保护目录，提交须由用户设 ALLOW_GENERATED_UPDATE 后进行（HAR-4）。'
        }
    }
}
