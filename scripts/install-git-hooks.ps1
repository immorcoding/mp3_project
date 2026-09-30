[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

Complete-Utf8EntryScript -Action {
    $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
    $hooksPath = Join-Path -Path $repositoryRoot -ChildPath '.githooks'
    $git = Get-ExternalCommand -Name 'git'

    foreach ($hookName in @('pre-commit', 'commit-msg', 'pre-push')) {
        $hookPath = Join-Path -Path $hooksPath -ChildPath $hookName
        if (-not (Test-Path -LiteralPath $hookPath -PathType Leaf)) {
            throw "缺少 Git hook：$hookPath"
        }
    }

    Invoke-ExternalCommand -CommandPath $git -Arguments @(
        '-C', $repositoryRoot,
        'config',
        '--local',
        'core.hooksPath',
        '.githooks'
    )

    Write-NativeUtf8Line -Text '已启用 .githooks：pre-commit 验证索引快照，commit-msg 校验 Conventional Commits 标题，pre-push 按实际推送提交运行受影响主机测试。'
}
