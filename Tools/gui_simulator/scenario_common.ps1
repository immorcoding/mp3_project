# run-scenarios.ps1 与 compare-baseline.ps1 共用的场景函数：.args 解析、模拟器输出解析、模拟器构建。
# 只定义函数，由两个入口脚本 dot-source。

function Get-ScenarioArguments {
    <# 把 scenarios/<名>.args 展开成模拟器命令行：固定 --hidden 与 --out-dir，再逐行追加（忽略空行与 # 注释）。#>
    param([string]$ArgsFile, [string]$ShotDir)
    $arguments = @('--hidden', '--out-dir', $ShotDir)
    foreach ($line in Get-Content -Path $ArgsFile -Encoding UTF8) {
        $trimmed = $line.Trim()
        if ($trimmed -eq '' -or $trimmed.StartsWith('#')) { continue }
        $arguments += ($trimmed -split '\s+')
    }
    return $arguments
}

function ConvertFrom-SimulatorOutput {
    <# 解析模拟器 stdout：[shot] <帧> <哈希> 与 [anims] <名> <数量>；返回 键 -> 值（动画项键为 anims.<名>）。#>
    param([object[]]$Output)
    $keys = [ordered]@{}
    foreach ($row in $Output) {
        if ($row -match '^\[shot\] (\S+) ([0-9a-f]{8})') { $keys[$Matches[1]] = $Matches[2] }
        elseif ($row -match '^\[anims\] (\S+) (\d+)') { $keys["anims.$($Matches[1])"] = $Matches[2] }
    }
    return $keys
}

function Build-GuiSimulator {
    <#
        用 CMake/Ninja 构建 SimulatorDir（含 CMakeLists.txt 的 Tools\gui_simulator）到 BuildDir，返回 gui_simulator.exe 路径。
        给出 SdlSourceDir 时不再联网下载；否则清掉缓存里早先的覆盖值，按 CMakeLists 的 URL 与 SHA256 下载/校验。
    #>
    param([string]$SimulatorDir, [string]$BuildDir, [string]$SdlSourceDir, [string]$Label = $SimulatorDir)
    $configure = @('-S', $SimulatorDir, '-B', $BuildDir, '-G', 'Ninja', '-DCMAKE_C_COMPILER=gcc')
    if ($SdlSourceDir) { $configure += "-DFETCHCONTENT_SOURCE_DIR_SDL2_MINGW=$SdlSourceDir" }
    else { $configure += '-UFETCHCONTENT_SOURCE_DIR_SDL2_MINGW' }
    & cmake @configure | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "模拟器 CMake 配置失败：$Label" }
    & cmake --build $BuildDir | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "模拟器构建失败：$Label" }
    return (Join-Path $BuildDir 'gui_simulator.exe')
}
