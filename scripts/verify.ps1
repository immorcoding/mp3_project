[CmdletBinding()]
param(
    [string]$HostCompiler,

    [ValidateSet('flash_ftl', 'w25qxx', 'resource_pack')]
    [string[]]$Module,

    [switch]$AllowGeneratedUpdate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

Complete-Utf8EntryScript -Action {
    [void](Get-RepositoryRoot -EntryScriptPath $PSCommandPath)

    $checkLayerIncludesScript = Join-Path -Path $PSScriptRoot -ChildPath 'check-layer-includes.ps1'
    $checkGeneratedWriteScript = Join-Path -Path $PSScriptRoot -ChildPath 'check-generated-write.ps1'
    $buildFirmwareScript = Join-Path -Path $PSScriptRoot -ChildPath 'build-firmware.ps1'
    $testHostScript = Join-Path -Path $PSScriptRoot -ChildPath 'test-host.ps1'

    Invoke-PowerShellScript -ScriptPath $checkLayerIncludesScript

    $generatedWriteParameters = @{}
    if ($AllowGeneratedUpdate) {
        $generatedWriteParameters.AllowGeneratedUpdate = $true
    }

    Invoke-PowerShellScript -ScriptPath $checkGeneratedWriteScript -Parameters $generatedWriteParameters
    Invoke-PowerShellScript -ScriptPath $buildFirmwareScript -Parameters @{ Configuration = 'Debug' }
    Invoke-PowerShellScript -ScriptPath $buildFirmwareScript -Parameters @{ Configuration = 'Release' }

    $testHostParameters = @{}
    if ($PSBoundParameters.ContainsKey('HostCompiler')) {
        $testHostParameters.HostCompiler = $HostCompiler
    }

    if ($PSBoundParameters.ContainsKey('Module')) {
        $testHostParameters.Module = $Module
    }

    Invoke-PowerShellScript -ScriptPath $testHostScript -Parameters $testHostParameters
}
