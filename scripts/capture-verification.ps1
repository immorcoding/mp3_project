[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('FULL', 'GUI')][string]$Mode,
    [string]$OutputDirectory,
    [string]$HostCompiler,
    [string]$SdlSourceDir
)

# 只包装现有验证入口；每次运行有独立证据目录，底层退出码原样返回。
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$utf8 = New-Object Text.UTF8Encoding($true)
function Get-EvidenceGit {
    param([string[]]$GitArguments)
    $result = @(& git -C $repo @GitArguments)
    if ($LASTEXITCODE -ne 0) { throw "无法读取 Git 证据：$GitArguments" }
    return $result
}
function Quote-EvidenceLiteral {
    param([string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

if ($Mode -eq 'GUI' -and $HostCompiler) { throw 'HostCompiler 仅适用于 FULL。' }
if ($Mode -eq 'FULL' -and $SdlSourceDir) { throw 'SdlSourceDir 仅适用于 GUI。' }
$commit = (Get-EvidenceGit -GitArguments @('rev-parse', 'HEAD')) -join ''
$before = @(Get-EvidenceGit -GitArguments @('status', '--porcelain=v1', '--untracked-files=all'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo 'build/evidence' }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($outputRoot)
$runName = '{0}-{1}-{2}' -f ([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')), $Mode.ToLowerInvariant(), [Guid]::NewGuid().ToString('N')
$run = Join-Path $outputRoot $runName
# 不使用 -Force；现有运行目录和日志永不覆盖。
[void](New-Item -ItemType Directory -Path $run)
$scriptPath = if ($Mode -eq 'FULL') { Join-Path $repo 'scripts/verify.ps1' } else { Join-Path $repo 'Tools/gui_simulator/run-scenarios.ps1' }
$parameters = [ordered]@{}
if ($HostCompiler) { $parameters.HostCompiler = $HostCompiler }
if ($SdlSourceDir) { $parameters.SdlSourceDir = $SdlSourceDir }
$command = '& ' + (Quote-EvidenceLiteral $scriptPath)
foreach ($key in $parameters.Keys) { $command += ' -' + $key + ' ' + (Quote-EvidenceLiteral $parameters[$key]) }
$metadata = [ordered]@{
    schemaVersion = 1
    mode = $Mode
    repository = $repo
    commit = $commit
    command = $command
    parameters = $parameters
    startedAtUtc = [DateTime]::UtcNow.ToString('o')
    finishedAtUtc = $null
    dirtyBefore = ($before.Count -gt 0)
    statusBefore = $before
    dirtyAfter = $null
    statusAfter = @()
    exitCode = $null
    captureError = $null
    stdout = 'stdout.log'
    stderr = 'stderr.log'
}
$metadataPath = Join-Path $run 'metadata.json'
[IO.File]::WriteAllText($metadataPath, ($metadata | ConvertTo-Json -Depth 5), $utf8)
$code = 1
try {
    # 子进程句柄重定向会捕获原生 Console/外部程序，不依赖 PowerShell 管道或 Transcript。
    $body = "[Console]::OutputEncoding = New-Object Text.UTF8Encoding(`$false); [Console]::InputEncoding = [Console]::OutputEncoding; `$OutputEncoding = [Console]::OutputEncoding; `$ErrorActionPreference = 'Stop'; try { $command; if (`$null -ne `$LASTEXITCODE) { exit `$LASTEXITCODE }; exit 0 } catch { [Console]::Error.WriteLine(`$_); exit 1 }"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($body))
    $process = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe') `
        -ArgumentList @('-NoProfile', '-NonInteractive', '-OutputFormat', 'Text', '-EncodedCommand', $encoded) `
        -WorkingDirectory $repo -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput (Join-Path $run 'stdout.log') -RedirectStandardError (Join-Path $run 'stderr.log')
    $code = $process.ExitCode
}
catch {
    $metadata.captureError = $_.ToString()
    [Console]::Error.WriteLine($_)
}
finally {
    $metadata.exitCode = $code
    $metadata.finishedAtUtc = [DateTime]::UtcNow.ToString('o')
    try {
        $after = @(Get-EvidenceGit -GitArguments @('status', '--porcelain=v1', '--untracked-files=all'))
        $metadata.statusAfter = $after
        $metadata.dirtyAfter = ($after.Count -gt 0)
    }
    catch { $metadata.captureError = $_.ToString(); if ($code -eq 0) { $code = 1 } }
    [IO.File]::WriteAllText($metadataPath, ($metadata | ConvertTo-Json -Depth 5), $utf8)
    Write-Output "EVIDENCE_DIRECTORY=$run"
}
exit $code
