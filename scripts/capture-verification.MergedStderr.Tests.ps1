Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 回归：Windows PowerShell 5.1 在 stderr 合并进管道（`2>&1`）时会把原生命令的 stderr 行包成
# ErrorRecord，$ErrorActionPreference='Stop' 下抛错。证据捕获自测在该调用形式下必须与重定向到文件时一致。
$target = Join-Path $PSScriptRoot 'capture-verification.Tests.ps1'
$outFile = [IO.Path]::GetTempFileName()
try {
    $inner = "& { & '" + $target.Replace("'", "''") + "' } 2>&1 | Out-File -LiteralPath '" + $outFile.Replace("'", "''") + "' -Encoding utf8; exit `$LASTEXITCODE"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -Command $inner
    $exit = $LASTEXITCODE
    $text = [IO.File]::ReadAllText($outFile)
    if ($exit -ne 0 -or -not $text.Contains('验证证据捕获测试通过。')) {
        throw "合并 stderr 调用下 capture-verification 自测失败（exit=$exit）：`n$text"
    }
    Write-Output '合并 stderr 回归测试通过。'
}
finally { Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue }
$global:LASTEXITCODE = 0
