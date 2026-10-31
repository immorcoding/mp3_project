# 用法：./docs/research/fatfs-open-cpu-cost/run_arm.ps1 [-Opt Os|O2] [-ArmBin <arm-none-eabi 工具链 bin 目录>] [-Fast]
# 需要先有 run.ps1 编译出的 bench.exe（用来导出镜像页与路径），以及 `pip install unicorn`。
# 交叉编译与固件同一份 ff.c / cc936.c / ffconf.h（-mcpu=cortex-m7 -mthumb），再用 Unicorn 数 f_open 的真实指令数。
param([string]$Opt = 'Os', [string]$ArmBin = '', [switch]$Fast)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here '../../..')).Path
$tag = if ($Fast) { "$Opt-fast" } else { $Opt }
$out = Join-Path $env:TEMP "fatfs-open-cpu-cost-$tag"
if (-not $ArmBin) { $ArmBin = Split-Path (Get-Command arm-none-eabi-gcc | Select-Object -First 1).Source }
$ff = Join-Path $root 'Middlewares/Third_Party/FatFs/src'
$ge = Join-Path $out 'arm'
New-Item -ItemType Directory -Force $ge | Out-Null

# 1) 主机程序导出 12000 首镜像
& "$out/bench.exe" 12000 3 "$ge/img" | Out-Null

# 2) 交叉编译（-fshort-wchar：TCHAR 为 16 位，与固件一致）
$flags = @('-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard', "-$Opt", '-w', '-ffreestanding',
    '-fshort-wchar', "-I$out/host_config", "-I$ff", '-ffunction-sections', '-fdata-sections')
$srcs = @((Join-Path $here 'arm_emu/harness.c'), "$ff/ff.c", "$ff/option/cc936.c")
if ($Fast) { $srcs += (Join-Path $here 'fastupper.c') }
$objs = @()
foreach ($s in $srcs) {
    $o = Join-Path $ge ((Split-Path $s -Leaf) + '.o'); $objs += $o
    $extra = @(); if ($Fast -and $s -like '*cc936.c') { $extra = @('-Dff_wtoupper=ff_wtoupper_orig') }
    & "$ArmBin/arm-none-eabi-gcc.exe" @flags @extra -c $s -o $o
    if ($LASTEXITCODE) { throw "gcc 失败: $s" }
}
& "$ArmBin/arm-none-eabi-gcc.exe" -mcpu=cortex-m7 -mthumb -nostdlib -nostartfiles '-Wl,--no-warn-rwx-segments' "-T$here/arm_emu/link.ld" @objs -lgcc -o "$ge/harness.elf"
if ($LASTEXITCODE) { throw '链接失败' }
& "$ArmBin/arm-none-eabi-objcopy.exe" -O binary "$ge/harness.elf" "$ge/harness.bin"
& "$ArmBin/arm-none-eabi-nm.exe" "$ge/harness.elf" | Set-Content "$ge/harness.nm"
& "$ArmBin/arm-none-eabi-size.exe" "$ge/harness.elf"

# 3) 仿真
python -I (Join-Path $here 'arm_emu/emu.py') $ge "$ge/img"
