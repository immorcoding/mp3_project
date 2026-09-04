[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

Complete-Utf8EntryScript -Action {
    $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
    $hooksPath = Join-Path -Path $repositoryRoot -ChildPath '.githooks'
    $preCommitPath = Join-Path -Path $hooksPath -ChildPath 'pre-commit'
    $prePushPath = Join-Path -Path $hooksPath -ChildPath 'pre-push'
    $git = Get-ExternalCommand -Name 'git'

    if (-not (Test-Path -LiteralPath $preCommitPath -PathType Leaf)) {
        throw "缺少 Git hook：$preCommitPath"
    }
    if (-not (Test-Path -LiteralPath $prePushPath -PathType Leaf)) {
        throw "缺少 Git hook：$prePushPath"
    }

    Invoke-ExternalCommand -CommandPath $git -Arguments @(
        '-C', $repositoryRoot,
        'config',
        '--local',
        'core.hooksPath',
        '.githooks'
    )

    Write-NativeUtf8Line -Text '已启用 .githooks：pre-commit 验证索引快照，pre-push 按实际推送提交运行受影响主机测试。'
}
