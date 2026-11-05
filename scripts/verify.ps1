[CmdletBinding()]
param(
    [string]$HostCompiler,

    [ValidateSet('external_loader', 'flash_ftl', 'filesystem_locator', 'gui_task', 'music_gui', 'gui_theme', 'gui_canvas', 'gui_font', 'w25qxx', 'resource_pack', 'storage_audio_stream', 'storage_catalog', 'ui_cell', 'playback_service', 'playback_task', 'monitor_warn', 'board_test_card')]
    [string[]]$Module,

    [switch]$AllowGeneratedUpdate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')
. (Join-Path -Path $PSScriptRoot -ChildPath 'short_root.ps1')

if ($PSBoundParameters.ContainsKey('Module')) {
    Write-NativeUtf8Line -Text '提示：verify.ps1 现在固定等价于 FULL；为避免削弱验收，-Module 参数已忽略。按影响测试请使用 verify_changed.ps1。'
}

$parameters = @{ AllowGeneratedUpdate = $AllowGeneratedUpdate }
if (-not [string]::IsNullOrWhiteSpace($HostCompiler)) {
    $parameters.HostCompiler = $HostCompiler
}
# 工作树路径过长时（构建产物超过 MAX_PATH）自动 subst 到空闲盘符，在盘符路径下跑 FULL，结束（含失败）卸载；
# 路径正常时原样运行。证据目录与 metadata 由 capture-verification.ps1 按真实路径写。
$repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
Invoke-HarnessFullInShortRoot -RepositoryRoot $repositoryRoot -Parameters $parameters
exit $LASTEXITCODE
