[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$SdlSourceDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$simRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$repoRoot = (Resolve-Path (Join-Path $simRoot '..\..')).Path
if (-not $BuildDir) {
    $BuildDir = Join-Path $repoRoot 'build\gui_simulator_track_title_prototype'
}

$configureArguments = @('-S', $simRoot, '-B', $BuildDir, '-G', 'Ninja', '-DCMAKE_C_COMPILER=gcc')
if ($SdlSourceDir) {
    $configureArguments += "-DFETCHCONTENT_SOURCE_DIR_SDL2_MINGW=$SdlSourceDir"
}
& cmake @configureArguments
if ($LASTEXITCODE -ne 0) { throw "原型模拟器 CMake 配置失败" }
& cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { throw "原型模拟器构建失败" }

& (Join-Path $BuildDir 'gui_simulator.exe') --prototype-track-title
