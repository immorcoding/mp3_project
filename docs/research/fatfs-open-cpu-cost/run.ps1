# 用法：./docs/research/fatfs-open-cpu-cost/run.ps1 [-Opt Os|O2] [-N 12000] [-Reps 101] [-Fast]
# 编译仓库未修改的 ff.c / option/cc936.c，配置取自未修改的 FATFS/Target/ffconf.h
# （只剥掉其中 3 个 MCU/BSP 头文件 include，与 Tests/flash_ftl/CMakeLists.txt 的做法一致）。
param([string]$Opt = 'Os', [int]$N = 12000, [int]$Reps = 101, [switch]$Fast)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here '../../..')).Path
$tag = if ($Fast) { "$Opt-fast" } else { $Opt }
$out = Join-Path $env:TEMP "fatfs-open-cpu-cost-$tag"
New-Item -ItemType Directory -Force "$out/host_config" | Out-Null
$conf = Get-Content (Join-Path $root 'FATFS/Target/ffconf.h') -Raw
foreach ($h in 'main.h', 'stm32h7xx_hal.h', 'bsp_driver_sd.h') { $conf = $conf.Replace("#include `"$h`"", '') }
Set-Content "$out/host_config/ffconf.h" $conf -Encoding ASCII
$ff = Join-Path $root 'Middlewares/Third_Party/FatFs/src'
$common = @("-$Opt", '-std=gnu11', '-w', '-D__USE_MINGW_ANSI_STDIO=1', '-DUNICODE', '-D_UNICODE', "-I$out/host_config", "-I$ff", "-I$here")
# -DUNICODE：integer.h 在 _WIN32 下引入 tchar.h，不定义 UNICODE 时 TCHAR 为 char，
# 与固件 _LFN_UNICODE=1（TCHAR=WCHAR=UTF-16）不符。
$srcs = @((Join-Path $here 'bench.c'), (Join-Path $here 'ramdisk.c'), "$ff/ff.c", "$ff/option/cc936.c")
if ($Fast) { $srcs += (Join-Path $here 'fastupper.c'); $common += @('-DFAST_UPPER') }
$objs = @()
foreach ($s in $srcs) {
    $o = Join-Path $out ((Split-Path $s -Leaf) + '.o'); $objs += $o
    $extra = @(); if ($Fast -and $s -like '*cc936.c') { $extra = @('-Dff_wtoupper=ff_wtoupper_orig') }
    & gcc @common @extra -c $s -o $o
    if ($LASTEXITCODE) { throw "gcc 失败: $s" }
}
& gcc @common -c (Join-Path $here 'hostclock.c') -o "$out/hostclock.o"
if ($LASTEXITCODE) { throw 'gcc 失败: hostclock.c' }
& gcc @objs "$out/hostclock.o" -o "$out/bench.exe"
if ($LASTEXITCODE) { throw '链接失败' }
& "$out/bench.exe" $N $Reps
