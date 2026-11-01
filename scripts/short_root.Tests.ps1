Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 自测：工作树路径过长时 FULL 自动映射短盘符（#183）。
# 接缝：short_root.ps1 的公开函数；用真实 subst 与临时目录，结束后核对没有残留映射。
. (Join-Path -Path $PSScriptRoot -ChildPath 'short_root.ps1')

function Assert-ShortRootEqual {
    param([Parameter(Mandatory)]$Expected, [Parameter(Mandatory)]$Actual, [Parameter(Mandatory)][string]$Message)
    if ([string]$Expected -ne [string]$Actual) {
        throw "$Message 期望 '$Expected'，实际 '$Actual'。"
    }
}

function Assert-ShortRootThrows {
    param([Parameter(Mandatory)][scriptblock]$Action, [Parameter(Mandatory)][string]$Pattern, [Parameter(Mandatory)][string]$Message)
    $thrown = $null
    try { & $Action } catch { $thrown = $_.Exception.Message }
    if ($null -eq $thrown) { throw "$Message 期望抛错，实际未抛。" }
    if ($thrown -notmatch $Pattern) { throw "$Message 错误信息 '$thrown' 不匹配 '$Pattern'。" }
}

# --- 1. 阈值判定 ---
Assert-ShortRootEqual -Expected $false -Actual (Test-HarnessRootTooLong -Root 'C:\a\b' -MaxLength 10) -Message '短路径不应触发映射'
Assert-ShortRootEqual -Expected $false -Actual (Test-HarnessRootTooLong -Root 'C:\a\b\cc' -MaxLength 9) -Message '长度恰等于阈值不触发'
Assert-ShortRootEqual -Expected $true -Actual (Test-HarnessRootTooLong -Root 'C:\a\b\ccc' -MaxLength 9) -Message '长度超过阈值触发'
Assert-ShortRootEqual -Expected $false -Actual (Test-HarnessRootTooLong -Root 'C:\a\b\' -MaxLength 6) -Message '末尾分隔符不计入长度'

# --- 2. 盘符选择：同一根目录稳定，跳过已占用，耗尽则报错 ---
$first = Get-HarnessShortRootCandidateLetter -Root 'E:\work\r1'
Assert-ShortRootEqual -Expected $first.Count -Actual @($first | Select-Object -Unique).Count -Message '候选盘符不得重复'
Assert-ShortRootEqual -Expected (($first) -join '') -Actual ((Get-HarnessShortRootCandidateLetter -Root 'e:\WORK\r1/') -join '') `
    -Message '同一根目录（忽略大小写与末尾分隔符）候选顺序必须稳定，CMake 缓存才不会因盘符变化失效'
$other = Get-HarnessShortRootCandidateLetter -Root 'E:\work\another-tree-with-a-different-name'
if ($first[0] -eq $other[0]) {
    # 哈希碰撞不是错误，但这两个固定输入不应碰撞；碰撞说明散列退化为常数。
    throw '不同根目录的首选盘符退化为同一个，哈希无效。'
}
foreach ($letter in $first) {
    if ($letter -notmatch '^[E-Z]$') { throw "候选盘符超出 E-Z：$letter" }
}
$picked = Get-HarnessShortRootLetter -Root 'E:\work\r1' -UsedLetter @($first[0], $first[1])
Assert-ShortRootEqual -Expected $first[2] -Actual $picked -Message '已占用的首选盘符必须顺延到下一个空闲候选'
Assert-ShortRootThrows -Pattern '没有空闲盘符' -Message '盘符耗尽必须报错' -Action {
    Get-HarnessShortRootLetter -Root 'E:\work\r1' -UsedLetter $first
}

# --- 3. 映射生命周期（真实 subst） ---
function Get-SubstLetters {
    return @(& subst.exe | ForEach-Object { if ($_ -match '^([A-Z]):\\: =>') { $Matches[1] } })
}

$sandbox = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ('mp3-shortroot-' + [Guid]::NewGuid().ToString('N'))
$blockerLetter = $null
try {
    New-Item -ItemType Directory -Path (Join-Path $sandbox 'tree/sub') -Force | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $sandbox 'tree/sub/marker.txt'), 'marker')
    $tree = Join-Path $sandbox 'tree'
    $lettersBefore = Get-SubstLetters

    # 3a. 路径不长：不映射，动作看到原路径，行为不变。
    $seen = Invoke-HarnessInShortRoot -RepositoryRoot $tree -MaxLength 1000 -Action { param($root) $root }
    Assert-ShortRootEqual -Expected $tree -Actual $seen -Message '路径正常时动作必须收到原路径'
    Assert-ShortRootEqual -Expected ($lettersBefore -join ',') -Actual ((Get-SubstLetters) -join ',') -Message '路径正常时不得创建映射'

    # 3b. 路径过长：动作看到短盘符根，能读到原目录内容，结束后映射被卸掉。
    $seenRoot = $null
    $seenText = $null
    $result = Invoke-HarnessInShortRoot -RepositoryRoot $tree -MaxLength 1 -Action {
        param($root)
        $script:seenRoot = $root
        $script:seenText = [System.IO.File]::ReadAllText((Join-Path $root 'sub/marker.txt'))
        'action-result'
    }
    Assert-ShortRootEqual -Expected 'action-result' -Actual $result -Message '动作的输出必须原样返回'
    if ($seenRoot -notmatch '^[E-Z]:\\$') { throw "过长路径下动作应收到盘符根，实际 '$seenRoot'" }
    Assert-ShortRootEqual -Expected 'marker' -Actual $seenText -Message '映射盘必须能读到原目录内容'
    Assert-ShortRootEqual -Expected ($lettersBefore -join ',') -Actual ((Get-SubstLetters) -join ',') -Message '动作结束后映射必须卸掉'

    # 3c. 动作失败：异常原样向上传播，映射同样卸掉。
    Assert-ShortRootThrows -Pattern 'boom-from-action' -Message '动作失败必须向上传播' -Action {
        Invoke-HarnessInShortRoot -RepositoryRoot $tree -MaxLength 1 -Action { throw 'boom-from-action' }
    }
    Assert-ShortRootEqual -Expected ($lettersBefore -join ',') -Actual ((Get-SubstLetters) -join ',') -Message '动作失败后映射也必须卸掉'

    # 3d. 首选盘符已被别人占用：换一个空闲盘符，且不动别人的映射。
    # 取函数此刻会选的盘符（首选可能已被本机真实盘占用），用 subst 抢占它，再确认被换掉。
    $preferred = Get-HarnessShortRootLetter -Root $tree
    & subst.exe "${preferred}:" $sandbox | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "测试夹具无法占用 ${preferred}:" }
    $blockerLetter = $preferred
    $seenRoot = $null
    Invoke-HarnessInShortRoot -RepositoryRoot $tree -MaxLength 1 -Action { param($root) $script:seenRoot = $root } | Out-Null
    if ($seenRoot -eq "${preferred}:\") { throw '首选盘符被占用时不应复用它' }
    if ($seenRoot -notmatch '^[E-Z]:\\$') { throw "顺延后动作应收到盘符根，实际 '$seenRoot'" }
    if ((Get-SubstLetters) -notcontains $preferred) { throw '不得卸掉别人已有的映射' }
}
finally {
    if ($null -ne $blockerLetter) { & subst.exe "${blockerLetter}:" /D | Out-Null }
    Remove-Item -LiteralPath $sandbox -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output '短路径映射自测通过。'
$global:LASTEXITCODE = 0
