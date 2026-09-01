[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

Complete-Utf8EntryScript -Action {
    $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
    $hooksPath = Join-Path -Path $repositoryRoot -ChildPath '.githooks'
    $preCommitPath = Join-Path -Path $hooksPath -ChildPath 'pre-commit'
    $git = Get-ExternalCommand -Name 'git'

    if (-not (Test-Path -LiteralPath $preCommitPath -PathType Leaf)) {
        throw "缺少 Git hook：$preCommitPath"
    }

    Invoke-ExternalCommand -CommandPath $git -Arguments @(
        '-C', $repositoryRoot,
        'config',
        '--local',
        'core.hooksPath',
        '.githooks'
    )

    Write-NativeUtf8Line -Text '已为本仓库启用 .githooks（仅 local core.hooksPath）。提交时将运行分层 include 检查和生成目录写保护。'
}
