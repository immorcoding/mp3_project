Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# check-ps1-encoding.ps1: ASCII without BOM and non-ASCII with BOM pass; non-ASCII without BOM fails;
# build/ is skipped.
$check = Join-Path -Path $PSScriptRoot -ChildPath 'check-ps1-encoding.ps1'
$bom = [byte[]](0xEF, 0xBB, 0xBF)
$ascii = [System.Text.Encoding]::ASCII.GetBytes("Write-Output 'ok'`r`n")
$chinese = [byte[]](0x23, 0x20, 0xE4, 0xB8, 0xAD, 0x0D, 0x0A)   # "# " + U+4E2D + CRLF

function New-EncodingFixture {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][hashtable]$Files)
    foreach ($name in $Files.Keys) {
        $full = Join-Path -Path $Root -ChildPath $name
        New-Item -ItemType Directory -Path (Split-Path -Parent $full) -Force | Out-Null
        [System.IO.File]::WriteAllBytes($full, [byte[]]$Files[$name])
    }
}

function Invoke-Check {
    param([Parameter(Mandatory)][string]$Root)
    try {
        & $check -Root $Root | Out-Null
        return $null
    }
    catch {
        return $_.Exception.Message
    }
}

$temp = Join-Path -Path ([IO.Path]::GetTempPath()) -ChildPath ('ps1-encoding-' + [Guid]::NewGuid().ToString('N'))
try {
    $good = Join-Path $temp 'good'
    New-EncodingFixture -Root $good -Files @{
        'a/plain.ps1' = $ascii
        'a/bom.ps1' = [byte[]]($bom + $chinese)
        'build/ignored.ps1' = $chinese
    }
    $failure = Invoke-Check -Root $good
    if ($null -ne $failure) { throw "compliant tree should pass: $failure" }

    $bad = Join-Path $temp 'bad'
    New-EncodingFixture -Root $bad -Files @{
        'a/plain.ps1' = $ascii
        'scripts/nobom.ps1' = $chinese
    }
    $failure = Invoke-Check -Root $bad
    if (($null -eq $failure) -or ($failure -notmatch 'scripts/nobom\.ps1')) {
        throw "non-ASCII without BOM should fail and name the file: $failure"
    }

    Write-Output 'PowerShell encoding check tests passed.'
}
finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
$global:LASTEXITCODE = 0
