[CmdletBinding()]
param(
    [string]$HostCompiler,

    [switch]$AllowGeneratedUpdate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'harness_helpers.ps1')

Complete-Utf8EntryScript -Action {
    try {
        [void](Get-RepositoryRoot -EntryScriptPath $PSCommandPath)
        Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'check_fast.ps1') `
            -Parameters @{
                Snapshot = 'WorkingTree'
                SuppressStatus = $true
                AllowGeneratedUpdate = $AllowGeneratedUpdate
            }
        Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'build-firmware.ps1') `
            -Parameters @{ Configuration = 'Debug' }
        Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'build-firmware.ps1') `
            -Parameters @{ Configuration = 'Release' }

        $hostParameters = @{}
        if (-not [string]::IsNullOrWhiteSpace($HostCompiler)) {
            $hostParameters.HostCompiler = $HostCompiler
        }
        Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'test-host.ps1') `
            -Parameters $hostParameters

        Write-HarnessStatus -Status PASS_HOST_ONLY `
            -Summary 'FULL 通过：架构规则、Harness 自测、固件 Debug/Release 与全部主机回归均通过；本命令不替代上板。'
    }
    catch {
        Write-HarnessStatus -Status FAIL -Summary "FULL 失败：$($_.Exception.Message)"
        throw
    }
}
