[CmdletBinding()]
param(
    [string]$HostCompiler,

    [ValidateSet('external_loader', 'flash_ftl', 'w25qxx', 'resource_pack')]
    [string[]]$Module,

    [switch]$AllowGeneratedUpdate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

if ($PSBoundParameters.ContainsKey('Module')) {
    Write-NativeUtf8Line -Text '提示：verify.ps1 现在固定等价于 FULL；为避免削弱验收，-Module 参数已忽略。按影响测试请使用 verify_changed.ps1。'
}

$parameters = @{ AllowGeneratedUpdate = $AllowGeneratedUpdate }
if (-not [string]::IsNullOrWhiteSpace($HostCompiler)) {
    $parameters.HostCompiler = $HostCompiler
}
Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'verify_full.ps1') `
    -Parameters $parameters
exit $LASTEXITCODE
