Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptPath = Join-Path -Path $PSScriptRoot -ChildPath 'apply-vendor-patches.ps1'
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "缺少 Vendor 补丁重打脚本：$scriptPath"
}

. $scriptPath

$inheritedGitEnvironment = Suspend-InheritedGitEnvironment

function Assert-True {
    param(
        [Parameter(Mandatory)][bool]$Actual,
        [Parameter(Mandatory)][string]$Message
    )

    if (-not $Actual) {
        throw $Message
    }
}

function Assert-Throws {
    param(
        [Parameter(Mandatory)][scriptblock]$Action,
        [Parameter(Mandatory)][string]$MessagePattern,
        [Parameter(Mandatory)][string]$Message
    )

    $thrown = $null
    try {
        & $Action
    }
    catch {
        $thrown = $_.Exception.Message
    }
    if ($null -eq $thrown) {
        throw "$Message：期望抛错但没有。"
    }
    if ($thrown -notmatch $MessagePattern) {
        throw "$Message：错误信息 '$thrown' 不匹配 '$MessagePattern'。"
    }
}

# 夹具：系统临时目录里的非仓库目录，放一个“上游文件”和一个把它改成新内容的补丁。
function New-PatchFixture {
    $root = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ("vendor-patch-" + [guid]::NewGuid().ToString('N'))
    $sourceDirectory = Join-Path -Path $root -ChildPath 'Middlewares/Third_Party/FatFs/src'
    New-Item -ItemType Directory -Force -Path $sourceDirectory | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path -Path $root -ChildPath 'patches/fatfs') | Out-Null

    $original = "int a;`nint b;`nint c;`nint d;`nint e;`n"
    $patched = "int a;`nint b;`nint c;`nint added;`nint d;`nint e;`n"
    $before = Join-Path -Path $root -ChildPath 'before/Middlewares/Third_Party/FatFs/src'
    $after = Join-Path -Path $root -ChildPath 'after/Middlewares/Third_Party/FatFs/src'
    New-Item -ItemType Directory -Force -Path $before, $after | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $before 'ff.c'), $original)
    [System.IO.File]::WriteAllText((Join-Path $after 'ff.c'), $patched)
    [System.IO.File]::WriteAllText((Join-Path $sourceDirectory 'ff.c'), $original)

    Push-Location -LiteralPath $root
    try {
        $diff = @(& git -c core.autocrlf=false diff --no-index before after 2>$null)
    }
    finally {
        Pop-Location
    }
    $patchText = (($diff -join "`n") -replace 'a/before/', 'a/' -replace 'b/after/', 'b/') + "`n"
    [System.IO.File]::WriteAllText((Join-Path $root 'patches/fatfs/0001-add.patch'), $patchText)
    Remove-Item -LiteralPath (Join-Path $root 'before'), (Join-Path $root 'after') -Recurse -Force

    return [pscustomobject]@{ Root = $root; Source = (Join-Path $sourceDirectory 'ff.c') }
}

function Get-FixtureText {
    param([Parameter(Mandatory)][string]$Path)
    return ([System.IO.File]::ReadAllText($Path) -replace "`r`n", "`n")
}

$fixtures = New-Object System.Collections.Generic.List[string]
try {
    # 未应用：-Check 失败并报“FatFs 补丁未应用”与重打命令，且不改文件。
    $fixture = New-PatchFixture
    [void]$fixtures.Add($fixture.Root)
    Assert-Throws -Action { Invoke-VendorPatchApply -RepositoryRoot $fixture.Root -Check } `
        -MessagePattern 'FatFs 补丁未应用.*apply-vendor-patches\.ps1' -Message '未应用时 -Check 应失败并给出重打命令'
    Assert-True -Actual ((Get-FixtureText $fixture.Source) -notmatch 'added') -Message '-Check 不得修改文件'

    # 重打：应用成功；重复执行跳过；之后 -Check 通过。
    $first = @(Invoke-VendorPatchApply -RepositoryRoot $fixture.Root)
    Assert-True -Actual ((Get-FixtureText $fixture.Source) -match 'int added;') -Message '重打后文件应含补丁内容'
    Assert-True -Actual (($first -join "`n") -match '已应用：FatFs/0001-add\.patch') -Message '首次应报告已应用'
    $second = @(Invoke-VendorPatchApply -RepositoryRoot $fixture.Root)
    Assert-True -Actual (($second -join "`n") -match '已应用，跳过') -Message '重复执行应跳过已应用的补丁'
    Assert-True -Actual ((Get-FixtureText $fixture.Source) -match '(?s)added.*') -Message '重复执行不得改动文件'
    $count = ([regex]::Matches((Get-FixtureText $fixture.Source), 'int added;')).Count
    Assert-True -Actual ($count -eq 1) -Message '重复执行不得重复应用'
    Invoke-VendorPatchApply -RepositoryRoot $fixture.Root -Check | Out-Null

    # 被覆盖（模拟 CubeMX 重新导出）后，-Check 再次失败，重打恢复。
    [System.IO.File]::WriteAllText($fixture.Source, "int a;`nint b;`nint c;`nint d;`nint e;`n")
    Assert-Throws -Action { Invoke-VendorPatchApply -RepositoryRoot $fixture.Root -Check } `
        -MessagePattern 'FatFs 补丁未应用' -Message '文件被覆盖后 -Check 应失败'
    Invoke-VendorPatchApply -RepositoryRoot $fixture.Root | Out-Null
    Invoke-VendorPatchApply -RepositoryRoot $fixture.Root -Check | Out-Null

    # 冲突：上游改了补丁上下文，既不能正向也不能反向，停下并报出 git 的位置。
    $conflict = New-PatchFixture
    [void]$fixtures.Add($conflict.Root)
    [System.IO.File]::WriteAllText($conflict.Source, "int a;`nint b;`nint X;`nint d;`nint e;`n")
    Assert-Throws -Action { Invoke-VendorPatchApply -RepositoryRoot $conflict.Root } `
        -MessagePattern '(?s)冲突.*ff\.c' -Message '冲突时应停下并报出文件'
    Assert-True -Actual ((Get-FixtureText $conflict.Source) -notmatch 'added') -Message '冲突时不得改动文件'
    Assert-Throws -Action { Invoke-VendorPatchApply -RepositoryRoot $conflict.Root -Check } `
        -MessagePattern '(?s)FatFs 补丁未应用.*冲突' -Message '-Check 遇冲突也应报未应用'

    # 没有 patches 目录：什么都不做。
    $empty = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ("vendor-patch-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $empty | Out-Null
    [void]$fixtures.Add($empty)
    Invoke-VendorPatchApply -RepositoryRoot $empty -Check | Out-Null

    # 仓库里真实的 FatFs 补丁文件存在且是 LF 换行。
    $repositoryRoot = Split-Path -Parent $PSScriptRoot
    $realPatches = @(Get-VendorPatch -RepositoryRoot $repositoryRoot)
    Assert-True -Actual ($realPatches.Count -ge 1) -Message '仓库应带有 FatFs 补丁文件'
    foreach ($realPatch in $realPatches) {
        $bytes = [System.IO.File]::ReadAllBytes($realPatch.FullPath)
        Assert-True -Actual ($bytes -notcontains 13) -Message "补丁必须是 LF 换行：$($realPatch.Name)"
    }
}
finally {
    foreach ($path in $fixtures) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
    Restore-InheritedGitEnvironment -Saved $inheritedGitEnvironment
}

Write-Output 'apply-vendor-patches 自测通过。'

$global:LASTEXITCODE = 0
