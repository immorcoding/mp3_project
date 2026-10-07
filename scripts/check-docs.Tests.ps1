Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'check-docs.ps1')
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('mp3-doc-tests-' + [guid]::NewGuid().ToString('N'))
function Write-Fixture { param([string]$Path, [string]$Text)
    $target = Join-Path $fixture $Path
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
    [IO.File]::WriteAllText($target, $Text, (New-Object Text.UTF8Encoding $true))
}
function Assert-DocsFailure { param([string]$Pattern)
    $failure = $null
    try { Invoke-DocumentationCheck -RepositoryRoot $fixture } catch { $failure = $_.Exception.Message }
    if (-not $failure -or $failure -notmatch $Pattern) { throw "预期文档失败 $Pattern，实际：$failure" }
}
try {
    Write-Fixture 'README.md' '[失效](missing.md)'
    Assert-DocsFailure 'missing.md'
    Write-Fixture 'README.md' "# 入口`n[有效](docs/target.md#中文标题)"
    Write-Fixture 'docs/target.md' "# 中文标题`n[返回根](..)"
    Write-Fixture 'README.md' "# 入口`n[根目录](.)`n[有效](docs/target.md#中文标题)"
    Invoke-DocumentationCheck -RepositoryRoot $fixture
    Write-Fixture 'docs/target.md' "# 中文标题`n`n## Repeat *text*`n`n## Repeat *text*"
    Write-Fixture 'README.md' "[重复标题](docs/target.md#repeat-text-1)`n[引用][target]`n`n[target]: docs/target.md#中文标题`n``````md`n[示例非链接](missing.md)`n``````"
    Invoke-DocumentationCheck -RepositoryRoot $fixture
    Write-Fixture 'README.md' '[坏锚点](docs/target.md#missing)'
    Assert-DocsFailure 'missing'
    Write-Fixture 'README.md' '# 入口'
    $valid = "# Example`n`n范围。`n`nNext id: EX-2`n`n## naming`n`n命名范围。`n`n### Rules`n`n- **EX-1** · settled · 规则。 _Why:_ 理由。 _Check:_ 检查。"
    Write-Fixture 'docs/shape/example.md' $valid
    Invoke-DocumentationCheck -RepositoryRoot $fixture
    Write-Fixture 'docs/shape/example.md' ($valid.Replace("# Example`n`n范围。", '# Example'))
    Assert-DocsFailure 'area scope'
    Write-Fixture 'docs/shape/example.md' $valid
    Write-Fixture 'docs/shape/bad_name.md' $valid
    Assert-DocsFailure '非法 area/title'
    Remove-Item -LiteralPath (Join-Path $fixture 'docs/shape/bad_name.md')
    Write-Fixture 'docs/shape/example.md' ($valid + "`n### Rejected`n`n- 旧规则 EX-9 已退役。")
    Assert-DocsFailure 'Next id'
    $large = $valid.Replace('Next id: EX-2','Next id: EX-17')
    foreach ($n in 2..16) { $large += "`n- **EX-$n** · provisional · 规则。 _Why:_ 理由。" }
    Write-Fixture 'docs/shape/example.md' $large
    Assert-DocsFailure '整体 split'
    Write-Fixture 'docs/shape/example.md' ($valid + "`n- **EX-1** · exploring · 重复。 _Why:_ 理由。")
    Assert-DocsFailure '重复.*EX-1'
    Write-Fixture 'docs/shape/example.md' ($valid + "`n- **EX-x** · exploring · 坏编号。 _Why:_ 理由。")
    Assert-DocsFailure 'Rules.*单行'
    Write-Fixture 'docs/shape/example.md' ($valid + "`n- 无编号规则")
    Assert-DocsFailure 'Rules.*单行'
    Write-Fixture 'docs/shape/example.md' ($valid + "`n  不允许把规则续写到第二行。")
    Assert-DocsFailure 'Rules.*单行'
    Write-Fixture 'docs/shape/example.md' ($valid.Replace('命名范围。',''))
    Assert-DocsFailure 'scope'
    Write-Fixture 'docs/shape/example.md' ($valid.Replace(' _Why:_ 理由。',''))
    Assert-DocsFailure 'Why'
    Write-Fixture 'docs/shape/example.md' ($valid.Replace(' _Check:_ 检查。',''))
    Assert-DocsFailure 'Check'
    Write-Fixture 'docs/shape/example.md' ($valid.Replace('Next id: EX-2','Next id: EX-1'))
    Assert-DocsFailure 'Next id'
    Write-Fixture 'docs/shape/example.md' ($valid.Replace('## naming','## Naming'))
    Assert-DocsFailure 'title'
    Write-Fixture 'docs/shape/example.md' ($valid.Replace('### Rules','### Signals'))
    Assert-DocsFailure 'Rules'
    # 分拆过的 area 即使仅余一条规则也合法。
    Write-Fixture 'docs/shape/example.md' "# Example`n`n范围。`n`nNext id: EX-2`n`n## Titles`n`n- [naming](example.naming.md): 命名范围。"
    Write-Fixture 'docs/shape/example.naming.md' "# Example · naming`n`n命名范围。`n`n[返回](example.md)`n`n## Rules`n`n- **EX-1** · settled · 规则。 _Why:_ 理由。 _Check:_ 检查。"
    Invoke-DocumentationCheck -RepositoryRoot $fixture
    $splitIndex = [IO.File]::ReadAllText((Join-Path $fixture 'docs/shape/example.md'))
    Write-Fixture 'docs/shape/example.md' ($splitIndex.Replace('## Titles','## Pillars') + "`n## Titles")
    Assert-DocsFailure '索引'
    Write-Fixture 'docs/shape/example.md' $splitIndex
    $splitTitle = [IO.File]::ReadAllText((Join-Path $fixture 'docs/shape/example.naming.md'))
    Write-Fixture 'docs/shape/example.naming.md' ($splitTitle.Replace('命名范围。',''))
    Assert-DocsFailure 'scope'
    Write-Fixture 'docs/shape/example.naming.md' $splitTitle
    Write-Fixture 'docs/shape/example.orphan.md' "# Example · orphan`n`n另一范围。`n`n[返回](example.md)`n`n## Rules`n`n- **EX-2** · exploring · 规则。 _Why:_ 理由。"
    Assert-DocsFailure '孤立|索引'
    Remove-Item -LiteralPath (Join-Path $fixture 'docs/shape/example.orphan.md')
    Write-Fixture 'docs/shape/example.naming.md' "# Example · naming`n`n命名范围。`n`n## Rules`n`n- **EX-1** · settled · 规则。 _Why:_ 理由。 _Check:_ 检查。"
    Assert-DocsFailure 'backlink'
    Write-Fixture 'docs/shape/example.naming.md' "# Example · naming`n`n命名范围。`n`n[返回](example.md)`n`n### Rules`n`n- **EX-1** · settled · 规则。 _Why:_ 理由。 _Check:_ 检查。"
    # 审阅导航在根目录，链接仍需检查；旧大写 REVIEW 不再是 shape area 例外。
    Write-Fixture 'CODING_STANDARDS.md' '[规范](docs/shape/example.md)'
    Invoke-DocumentationCheck -RepositoryRoot $fixture
    Write-Fixture 'CODING_STANDARDS.md' '[失效规范](docs/shape/missing.md)'
    Assert-DocsFailure 'missing.md'
    Write-Fixture 'CODING_STANDARDS.md' '[规范](docs/shape/example.md)'
    Write-Fixture 'docs/shape/REVIEW.md' '# 旧入口'
    Assert-DocsFailure '非法 area/title 文件名'
    Remove-Item -LiteralPath (Join-Path $fixture 'docs/shape/REVIEW.md')
    Write-Fixture '.gitignore' '.claude/worktrees/'
    Write-Fixture '.claude/worktrees/other/docs/bad.md' '[他仓链接](missing.md)'
    Write-Fixture 'Tools/vendor/bad.md' '[不扫描](missing.md)'
    Write-Fixture 'Tools/build/bad.md' '[不扫描](missing.md)'
    Write-Fixture 'Drivers/bad.md' '[不扫描](missing.md)'
    $savedGit = Suspend-InheritedGitEnvironment
    try {
        $git = Get-ExternalCommand -Name 'git'
        Invoke-ExternalCommand -CommandPath $git -Arguments @('-C',$fixture,'init','--quiet')
        Invoke-ExternalCommand -CommandPath $git -Arguments @('-C',$fixture,'config','core.autocrlf','false')
        Write-Fixture 'README.md' '[索引坏链接](staged-missing.md)'
        Invoke-ExternalCommand -CommandPath $git -Arguments @('-C',$fixture,'add','.')
        Write-Fixture 'README.md' '# 工作树已经修正'
        Invoke-DocumentationCheck -RepositoryRoot $fixture
        $failure = $null
        try { Invoke-DocumentationIndexCheck -RepositoryRoot $fixture } catch { $failure = $_.Exception.Message }
        if (-not $failure -or $failure -notmatch 'staged-missing.md') { throw "索引错误被工作树掩盖：$failure" }
        Invoke-ExternalCommand -CommandPath $git -Arguments @('-C',$fixture,'add','README.md')
        Invoke-DocumentationIndexCheck -RepositoryRoot $fixture
    } finally { Restore-InheritedGitEnvironment -Saved $savedGit }
}
finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if (-not $resolved.StartsWith($temp + '\', [StringComparison]::OrdinalIgnoreCase)) { throw '拒绝清理临时目录外路径' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
Write-Output 'check-docs 文档行为测试通过。'
