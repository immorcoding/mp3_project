[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

Complete-Utf8EntryScript -Action {
    $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
    $cmake = Get-ExternalCommand -Name 'cmake'
    [void](Get-ExternalCommand -Name 'ninja')
    [void](Get-ExternalCommand -Name 'arm-none-eabi-gcc')
    [void](Get-ExternalCommand -Name 'arm-none-eabi-objcopy')

    $preset = "firmware-$($Configuration.ToLowerInvariant())"

    Invoke-InDirectory -Path $repositoryRoot -Action {
        Invoke-ExternalCommand -CommandPath $cmake -Arguments @('--preset', $preset)
        Invoke-ExternalCommand -CommandPath $cmake -Arguments @('--build', '--preset', $preset)
    }
}
