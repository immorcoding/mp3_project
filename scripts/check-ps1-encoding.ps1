[CmdletBinding()]
param(
    [string]$Root
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Windows PowerShell 5.1 reads a .ps1 without a BOM in the system ANSI code page, so non-ASCII text
# (Chinese comments, smart quotes) garbles and the parser fails far from the real cause.
# Rule: every tracked-tree .ps1 that contains a non-ASCII byte must start with the UTF-8 BOM.
# Runs on the working tree; FAST's index mode runs it inside the staged snapshot.

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = Split-Path -Parent $PSScriptRoot
}
$rootFull = [System.IO.Path]::GetFullPath($Root)
$skipTop = @('.git', '.claude', 'build')

$files = Get-ChildItem -LiteralPath $rootFull -Recurse -File -Filter '*.ps1' -Force | Where-Object {
    $relative = $_.FullName.Substring($rootFull.Length).TrimStart('\', '/')
    $top = ($relative -split '[\\/]')[0]
    $skipTop -notcontains $top
}

$violations = New-Object System.Collections.Generic.List[string]
foreach ($file in $files) {
    $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
    $hasBom = ($bytes.Length -ge 3) -and ($bytes[0] -eq 0xEF) -and ($bytes[1] -eq 0xBB) -and ($bytes[2] -eq 0xBF)
    if ($hasBom) {
        continue
    }
    foreach ($byte in $bytes) {
        if ($byte -ge 0x80) {
            [void]$violations.Add(($file.FullName.Substring($rootFull.Length).TrimStart('\', '/') -replace '\\', '/'))
            break
        }
    }
}

if ($violations.Count -gt 0) {
    $list = ($violations | ForEach-Object { "  $_" }) -join [Environment]::NewLine
    throw ("PowerShell scripts with non-ASCII text need a UTF-8 BOM (Windows PowerShell 5.1 parses them as ANSI otherwise):" +
        [Environment]::NewLine + $list)
}

Write-Output "PowerShell encoding check passed: $(@($files).Count) scripts."
