Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$skipRepositoryCheck = $env:MP3_HARNESS_INDEX_SNAPSHOT -eq '1'

$checkerScript = Join-Path -Path $PSScriptRoot -ChildPath 'check-generated-write.ps1'
if (-not (Test-Path -LiteralPath $checkerScript -PathType Leaf)) {
    throw "缺少生成目录写保护脚本：$checkerScript"
}

. $checkerScript

$inheritedGitEnvironment = Suspend-InheritedGitEnvironment

function Assert-True {
    param(
        [Parameter(Mandatory)]
        [bool]$Actual,

        [Parameter(Mandatory)]
        [string]$Message
    )

    if (-not $Actual) {
        throw $Message
    }
}

function Assert-False {
    param(
        [Parameter(Mandatory)]
        [bool]$Actual,

        [Parameter(Mandatory)]
        [string]$Message
    )

    if ($Actual) {
        throw $Message
    }
}

Assert-True -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c') `
    -Message 'Drivers 应受保护'
Assert-True -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c') `
    -Message 'ST USB 库应受保护'
Assert-True -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'Middlewares/Third_Party/LVGL/src/core/lv_obj.c') `
    -Message 'LVGL 源码应受保护'
Assert-True -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'Middlewares/Third_Party/FreeRTOS/Source/tasks.c') `
    -Message 'FreeRTOS 内核源码应受保护'
Assert-True -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'Middlewares/Third_Party/FatFs/src/ff.c') `
    -Message 'FatFs 上游源码应受保护'
Assert-True -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'cmake/stm32cubemx/CMakeLists.txt') `
    -Message 'CubeMX CMake 应受保护'

Assert-False -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'Middlewares/Third_Party/LVGL/lv_conf.h') `
    -Message 'lv_conf.h 应允许修改'
Assert-False -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'Middlewares/Third_Party/FreeRTOS/Config/FreeRTOSConfig.h') `
    -Message 'FreeRTOSConfig.h 应允许修改'
Assert-False -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'Core/Src/main.c') `
    -Message 'Core USER CODE 接缝应允许修改'
Assert-False -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'FATFS/App/fatfs.c') `
    -Message 'FATFS 接缝应允许修改'
Assert-False -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'USB_DEVICE/App/usb_device.c') `
    -Message 'USB_DEVICE 接缝应允许修改'
Assert-False -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'cmake/gcc-arm-none-eabi.cmake') `
    -Message '自维护工具链 CMake 应允许修改'
Assert-False -Actual (Test-GeneratedWriteProtectedPath -RelativePath 'Service/gui/gui_service.c') `
    -Message 'GUI Service 应允许修改'

$gitPath = Get-ExternalCommand -Name 'git'
if ($gitPath -is [System.Array]) {
    throw 'Get-ExternalCommand 必须返回单个路径字符串。'
}
if ($gitPath -isnot [string] -or [string]::IsNullOrWhiteSpace($gitPath)) {
    throw 'Get-ExternalCommand 必须返回非空路径字符串。'
}

if (-not $skipRepositoryCheck) {
    $mingwGitBin = 'C:\Program Files\Git\mingw64\bin'
    $cmdGitBin = 'C:\Program Files\Git\cmd'
    if ((Test-Path -LiteralPath (Join-Path -Path $mingwGitBin -ChildPath 'git.exe')) -and
        (Test-Path -LiteralPath (Join-Path -Path $cmdGitBin -ChildPath 'git.exe'))) {
        $previousPath = $env:PATH
        try {
            $env:PATH = "$mingwGitBin;$cmdGitBin;$previousPath"
            $hookGitPath = Get-ExternalCommand -Name 'git'
            if ($hookGitPath -is [System.Array]) {
                throw 'Git hook PATH 下 Get-ExternalCommand 仍返回了多个路径。'
            }

            $repositoryRootForGit = Get-RepositoryRoot -EntryScriptPath $checkerScript
            $insideWorkTree = Get-GitOutputLines -GitPath $hookGitPath -RepositoryRoot $repositoryRootForGit -Arguments @(
                'rev-parse', '--is-inside-work-tree'
            )
            if (($insideWorkTree -join '').Trim() -ne 'true') {
                throw '双 git.exe PATH 下应能调用 git rev-parse。'
            }
        }
        finally {
            $env:PATH = $previousPath
        }
    }
}

function Invoke-InTempGitRepository {
    param(
        [Parameter(Mandatory)]
        [scriptblock]$Action
    )

    $repositoryRoot = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ('generated-write-' + [Guid]::NewGuid().ToString('N'))
    $emptyHooks = Join-Path -Path $repositoryRoot -ChildPath '.empty-hooks'
    New-Item -ItemType Directory -Path $emptyHooks -Force | Out-Null
    New-Item -ItemType Directory -Path $repositoryRoot -Force | Out-Null

    $git = Get-ExternalCommand -Name 'git'
    $previousAllow = $env:ALLOW_GENERATED_UPDATE
    try {
        Remove-Item -Path Env:ALLOW_GENERATED_UPDATE -ErrorAction SilentlyContinue

        Invoke-InDirectory -Path $repositoryRoot -Action {
            Invoke-ExternalCommand -CommandPath $git -Arguments @('init')
            Set-Content -LiteralPath (Join-Path -Path $repositoryRoot -ChildPath 'tracked.txt') -Value 'seed' -Encoding ascii
            Invoke-ExternalCommand -CommandPath $git -Arguments @('add', '--', 'tracked.txt')
            Invoke-ExternalCommand -CommandPath $git -Arguments @(
                '-c', ('core.hooksPath=' + $emptyHooks)
                '-c', 'commit.gpgsign=false'
                '-c', 'user.email=generated-write-test@example.invalid'
                '-c', 'user.name=generated-write-test'
                'commit', '-m', 'seed'
            )
        }

        & $Action $repositoryRoot
    }
    finally {
        if ($null -eq $previousAllow) {
            Remove-Item -Path Env:ALLOW_GENERATED_UPDATE -ErrorAction SilentlyContinue
        }
        else {
            $env:ALLOW_GENERATED_UPDATE = $previousAllow
        }

        if (Test-Path -LiteralPath $repositoryRoot) {
            Remove-Item -LiteralPath $repositoryRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

Invoke-InTempGitRepository -Action {
    param($RepositoryRoot)

    New-Item -ItemType Directory -Path (Join-Path -Path $RepositoryRoot -ChildPath 'Drivers') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path -Path $RepositoryRoot -ChildPath 'Drivers/vendor.c') -Value 'generated' -Encoding ascii

    try {
        Invoke-GeneratedWriteCheck -RepositoryRoot $RepositoryRoot
        throw '未跟踪的 Vendor 文件应让写保护失败。'
    }
    catch {
        if ($_.Exception.Message -notmatch 'Drivers/vendor\.c') {
            throw "未跟踪 Vendor 文件的失败信息应包含路径，实际：$($_.Exception.Message)"
        }
    }
}

$previousAllowForLeakTest = $env:ALLOW_GENERATED_UPDATE
$env:ALLOW_GENERATED_UPDATE = '1'
try {
    Invoke-InTempGitRepository -Action {
        param($RepositoryRoot)

        New-Item -ItemType Directory -Path (Join-Path -Path $RepositoryRoot -ChildPath 'Drivers') -Force | Out-Null
        Set-Content -LiteralPath (Join-Path -Path $RepositoryRoot -ChildPath 'Drivers/vendor.c') -Value 'generated' -Encoding ascii

        try {
            Invoke-GeneratedWriteCheck -RepositoryRoot $RepositoryRoot
            throw '外层 ALLOW_GENERATED_UPDATE 不得让夹具里的未跟踪 Vendor 检查被跳过。'
        }
        catch {
            if ($_.Exception.Message -notmatch 'Drivers/vendor\.c') {
                throw "外层允许更新时夹具仍应报告路径，实际：$($_.Exception.Message)"
            }
        }
    }
}
finally {
    if ($null -eq $previousAllowForLeakTest) {
        Remove-Item -Path Env:ALLOW_GENERATED_UPDATE -ErrorAction SilentlyContinue
    }
    else {
        $env:ALLOW_GENERATED_UPDATE = $previousAllowForLeakTest
    }
}

Invoke-InTempGitRepository -Action {
    param($RepositoryRoot)

    New-Item -ItemType Directory -Path (Join-Path -Path $RepositoryRoot -ChildPath 'Core/Src') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path -Path $RepositoryRoot -ChildPath 'Core/Src/main.c') -Value 'user code' -Encoding ascii
    Invoke-GeneratedWriteCheck -RepositoryRoot $RepositoryRoot
}

Invoke-InTempGitRepository -Action {
    param($RepositoryRoot)

    New-Item -ItemType Directory -Path (Join-Path -Path $RepositoryRoot -ChildPath 'Drivers') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path -Path $RepositoryRoot -ChildPath 'Drivers/vendor.c') -Value 'generated' -Encoding ascii
    $env:ALLOW_GENERATED_UPDATE = '1'
    Invoke-GeneratedWriteCheck -RepositoryRoot $RepositoryRoot
}

Invoke-InTempGitRepository -Action {
    param($RepositoryRoot)

    $vendorDirectory = Join-Path -Path $RepositoryRoot -ChildPath 'Drivers'
    $generatedFile = Join-Path -Path $vendorDirectory -ChildPath 'vendor.c'
    New-Item -ItemType Directory -Path $vendorDirectory -Force | Out-Null
    Set-Content -LiteralPath $generatedFile -Value 'staged generated content' -Encoding ascii
    $git = Get-ExternalCommand -Name 'git'
    Invoke-ExternalCommand -CommandPath $git -Arguments @('-C', $RepositoryRoot, 'add', '--', 'Drivers/vendor.c')
    Remove-Item -LiteralPath $generatedFile -Force

    try {
        Invoke-GeneratedWriteCheck -RepositoryRoot $RepositoryRoot -Snapshot Index
        throw '索引中已暂存、工作树中已删除的 Vendor 文件仍应让写保护失败。'
    }
    catch {
        if ($_.Exception.Message -notmatch 'Drivers/vendor\.c') {
            throw "索引快照失败信息应包含暂存路径，实际：$($_.Exception.Message)"
        }
    }
}

Restore-InheritedGitEnvironment -Saved $inheritedGitEnvironment

if (-not $skipRepositoryCheck) {
    $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $checkerScript
    Invoke-GeneratedWriteCheck -RepositoryRoot $repositoryRoot
}

Write-Output 'check-generated-write 函数测试通过。'
