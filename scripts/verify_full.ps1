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

        # 模拟器编译 Service/gui、APP/tasks/gui 与部分 Component；只在 CHANGED 跑时，改这些源的票可能 FULL 通过而模拟器链接失败。
        $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
        Invoke-PowerShellScript -ScriptPath (Join-Path -Path $repositoryRoot -ChildPath 'Tools/gui_simulator/run-scenarios.ps1') `
            -Parameters @{}

        Write-HarnessStatus -Status PASS_HOST_ONLY `
            -Summary 'FULL 通过：架构规则、Harness 自测、固件 Debug/Release、全部主机回归与 GUI 模拟器场景均通过；本命令不替代上板。'
    }
    catch {
        Write-HarnessStatus -Status FAIL -Summary "FULL 失败：$($_.Exception.Message)"
        throw
    }
}
