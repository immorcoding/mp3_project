Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 回归：Windows PowerShell 5.1 在调用方把 stderr 合并或重定向（`2>&1`、`*>`）时，会把原生命令的
# stderr 行包成 ErrorRecord，$ErrorActionPreference='Stop' 下抛错；cmake 正常状态输出就写 stderr，
# 于是 build-firmware.ps1 在 configure 之后中止。Invoke-ExternalCommand 只以退出码判定成败。
$helpers = Join-Path $PSScriptRoot 'build_helpers.ps1'
$outFile = [IO.Path]::GetTempFileName()

function Invoke-ChildCase {
    param([Parameter(Mandatory)][string]$Body)

    $script = ". '" + $helpers.Replace("'", "''") + "'; `$ErrorActionPreference = 'Stop'; " + $Body
    $inner = "& { $script } 2>&1 | Out-File -LiteralPath '" + $outFile.Replace("'", "''") + "' -Encoding utf8; exit `$LASTEXITCODE"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -Command $inner
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Text = [IO.File]::ReadAllText($outFile)
    }
}

try {
    $cmd = (Get-Command cmd.exe).Source

    # 1. 不捕获输出：原生命令写 stderr 且退出码 0，不应抛错。
    $case = Invoke-ChildCase -Body ("Invoke-ExternalCommand -CommandPath '$cmd' -Arguments @('/c', 'echo stderr-line 1>&2'); Write-Output 'CASE-OK'")
    if (($case.ExitCode -ne 0) -or -not $case.Text.Contains('CASE-OK')) {
        throw "stderr 合并时不捕获输出的调用被当作失败（exit=$($case.ExitCode)）：`n$($case.Text)"
    }

    # 2. 捕获输出：stderr 行以字符串返回，不抛错。
    $case = Invoke-ChildCase -Body ("`$lines = Invoke-ExternalCommand -CommandPath '$cmd' -Arguments @('/c', 'echo stderr-line 1>&2') -CaptureOutput; if ((`$lines -join ' ') -match 'stderr-line') { Write-Output 'CAPTURE-OK' }")
    if (($case.ExitCode -ne 0) -or -not $case.Text.Contains('CAPTURE-OK')) {
        throw "stderr 合并时捕获输出未返回 stderr 文本（exit=$($case.ExitCode)）：`n$($case.Text)"
    }

    # 3. 非零退出码仍按失败抛出。
    $case = Invoke-ChildCase -Body ("try { Invoke-ExternalCommand -CommandPath '$cmd' -Arguments @('/c', 'exit 3') } catch { Write-Output 'THREW-OK' }")
    if (-not $case.Text.Contains('THREW-OK')) {
        throw "非零退出码未抛出：`n$($case.Text)"
    }

    Write-Output '外部命令 stderr 回归测试通过。'
}
finally { Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue }
$global:LASTEXITCODE = 0
