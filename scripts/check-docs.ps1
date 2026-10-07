[CmdletBinding()]
param([ValidateSet('WorkingTree', 'Index')][string]$Snapshot = 'WorkingTree')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build_helpers.ps1')

# 只遍历自维护正文；不进入 Vendor、生成器、缓存和构建输出。
function Get-DocumentationFiles {
    param([string]$RepositoryRoot)
    Get-ChildItem -LiteralPath $RepositoryRoot -File -Filter '*.md'
    foreach ($name in @('docs','APP','Adapters','Components','Platform','Service','Tests','Tools','scripts','.agents','.claude','.codex')) {
        $path = Join-Path $RepositoryRoot $name
        if (Test-Path -LiteralPath $path -PathType Container) { Get-DocumentationDirectoryFiles -Directory $path }
    }
}
function Get-DocumentationDirectoryFiles {
    param([string]$Directory)
    foreach ($item in Get-ChildItem -LiteralPath $Directory -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
        if ($item.PSIsContainer) {
            if ($item.Name -notmatch '^(?i:build|vendor|generated|third_party|node_modules|\.git|_deps|\.venv|__pycache__)$') {
                Get-DocumentationDirectoryFiles -Directory $item.FullName
            }
        } elseif ($item.Extension -eq '.md') { $item }
    }
}
function Get-MarkdownBody {
    param([string]$Path)
    # 代码例子不产生有效标题或链接。保留行数用于报错。
    $fence = ''; $length = 0
    foreach ($line in [IO.File]::ReadAllLines($Path, [Text.Encoding]::UTF8)) {
        if ($line -match '^\s{0,3}(`{3,}|~{3,})') {
            $marker = $Matches[1]
            if (-not $fence) { $fence = $marker.Substring(0,1); $length = $marker.Length }
            elseif ($marker.StartsWith($fence) -and $marker.Length -ge $length) { $fence = '' }
            ''
        } elseif ($fence) { '' } else { $line }
    }
}
function Get-MarkdownAnchors {
    param([string]$Path)
    $seen = @{}
    foreach ($line in @(Get-MarkdownBody -Path $Path)) {
        if ($line -match '^#{1,6}\s+(.+?)\s*#*\s*$') {
            $title = $Matches[1] -replace '\[([^\]]+)\]\([^)]*\)', '$1'
            $slug = ($title.ToLowerInvariant() -replace '[^\p{L}\p{M}\p{N}_\s-]', '') -replace '\s', '-'
            if ($seen.ContainsKey($slug)) { $seen[$slug]++; $slug + '-' + $seen[$slug] }
            else { $seen[$slug] = 0; $slug }
        }
        foreach ($match in [regex]::Matches($line, '<a\s+(?:name|id)=["'']([^"'']+)["'']')) { $match.Groups[1].Value }
    }
}
function Get-ShapeViolations {
    param([string]$RepositoryRoot)
    $shape = Join-Path $RepositoryRoot 'docs/shape'
    if (-not (Test-Path -LiteralPath $shape)) { return }
    $all = @(Get-ChildItem -LiteralPath $shape -File -Filter '*.md' | Where-Object { $_.Name -notin @('README.md','REVIEW.md','ROUTES.md') })
    foreach ($file in $all) {
        if ($file.Name -cnotmatch '^[a-z0-9-]+(?:\.[a-z0-9-]+)?\.md$') { "$($file.Name): 非法 area/title 文件名" }
    }
    $ids = @{}; $prefixes = @{}
    foreach ($area in @($all | Where-Object { $_.BaseName -notmatch '\.' })) {
        $text = [IO.File]::ReadAllText($area.FullName, [Text.Encoding]::UTF8)
        $nextMatches = [regex]::Matches($text, '(?m)^Next id: ([A-Z][A-Z0-9]*)-(\d+)\s*$')
        if ($nextMatches.Count -ne 1) { "$($area.Name): 必须恰有一个 Next id"; continue }
        $prefix = $nextMatches[0].Groups[1].Value; $next = [int]$nextMatches[0].Groups[2].Value
        if ($prefixes.ContainsKey($prefix)) { "$($area.Name): area prefix 重复 $prefix" } else { $prefixes[$prefix] = $area.Name }
        $titles = @($all | Where-Object { $_.BaseName.StartsWith($area.BaseName + '.') })
        $split = $titles.Count -gt 0 -or $text -match '(?m)^## Titles\s*$'
        if ($split) {
            $indexText = ($text -split '(?m)^## Titles\s*$',2)[-1] -split '(?m)^## ',2 | Select-Object -First 1
            if ($text -notmatch '(?m)^## Titles\s*$') { "$($area.Name): split 缺少 Titles 索引" }
            foreach ($title in $titles) {
                $entryPattern = '(?m)^- \[[^\]]+\]\(' + [regex]::Escape($title.Name) + '\)\s*[:：]\s*\S'
                if ($indexText -notmatch $entryPattern) { "$($title.Name): 孤立 title，缺少索引条目和 scope" }
            }
            # 索引不得留规则；链接完整性由通用检查另外验证。
            if ($text -match '(?m)^- \*\*[A-Z][A-Z0-9]*-\d+\*\*') { "$($area.Name): split 索引仍含 Rules" }
            foreach ($entry in [regex]::Matches($indexText, '(?m)^- \[[^\]]+\]\(([^)]+\.md)\)')) {
                $target = $entry.Groups[1].Value
                if ($target -notmatch ('^' + [regex]::Escape($area.BaseName) + '\.[a-z0-9-]+\.md$') -or $titles.Name -notcontains $target) {
                    "$($area.Name): Titles 索引目标不是本 area title：$target"
                }
            }
        }
        $count = 0; $maximum = 0
        foreach ($document in @($area) + $titles) {
            $lines = @(Get-MarkdownBody -Path $document.FullName)
            $isTitle = $document.BaseName -match '\.'
            $titleName = ''; $section = ''; $scope = $false; $titleRules = 0
            $hasHeader = $false; $areaScope = $false; $beforeSections = $true
            if ($isTitle) {
                $titleName = $document.BaseName.Substring($area.BaseName.Length + 1)
                $titleText = $lines -join "`n"
                if ($titleText -notmatch ('(?m)^# .+ · ' + [regex]::Escape($titleName) + '\s*$')) { "$($document.Name): title 标题须为 # Area · $titleName" }
                if ($titleText -notmatch ('\]\(' + [regex]::Escape($area.Name) + '\)')) { "$($document.Name): 缺少 backlink 到 $($area.Name)" }
                if ($titleText -match '(?m)^Next id:') { "$($document.Name): Next id 只能放在 area 索引" }
            }
            for ($i=0; $i -lt $lines.Count; $i++) {
                $line = $lines[$i]; $at = "$($document.Name):$($i+1)"
                if ($line -match '^# [^#]') { $hasHeader = $true; continue }
                if ($line -match '^##|^Next id:') { $beforeSections = $false }
                if (-not $areaScope -and $beforeSections -and $hasHeader -and $line.Trim() -and $line -notmatch '^#|^[-*] |^\[') {
                    $areaScope = $true
                    if ($isTitle) { $scope = $true }
                }
                if (-not $isTitle -and $line -match '^## (.+?)\s*$') {
                    if ($titleName -and (-not $scope -or $titleRules -eq 0)) { "$at`: title $titleName 缺少 scope 或 Rules" }
                    $name = $Matches[1]; $titleName = ''; $section = ''; $scope = $false; $titleRules = 0
                    if ($name -notin @('Pillars','Open questions','Proposed','Titles')) {
                        if ($name -cnotmatch '^[a-z0-9-]+$') { "$at`: 非法 title 名 $name" }
                        $titleName = $name
                        if ($split) { "$at`: split 索引不应包含 title 正文" }
                    }
                    continue
                }
                if ($line -match '^#{2,3} (Rules|References|Proposed|Signals|Rejected)\s*$') { $section = $Matches[1]; continue }
                if ($titleName -and -not $section -and $line.Trim() -and $line -notmatch '^#|^[-*] |^\[') { $scope = $true }
                if ($line -match '^- \*\*([A-Z][A-Z0-9]*-\d+)\*\*') {
                    $id = $Matches[1]; $count++; $titleRules++
                    if (-not $titleName -or -not $scope -or $section -ne 'Rules') { "$at`: $id 必须在有 scope 的 title Rules 下" }
                    if ($ids.ContainsKey($id)) { "$at`: 重复规则 ID $id（$($ids[$id])）" } else { $ids[$id]=$at }
                    if ($id -notmatch ('^' + [regex]::Escape($prefix) + '-\d+$')) { "$at`: $id 不属于 area prefix $prefix" }
                    if ($line -notmatch '^\- \*\*[A-Z][A-Z0-9]*-\d+\*\* · (exploring|provisional|settled) · \S') { "$at`: $id 规则级别或格式错误" }
                    if ($line -notmatch '_Why:_\s*[^_\s]') { "$at`: $id 缺少 Why" }
                    if ($line -match '· settled ·' -and $line -notmatch '_Check:_\s*[^_\s]') { "$at`: $id settled 缺少 Check" }
                }
            }
            if (-not $hasHeader -or -not $areaScope) { "$($document.Name): 缺少标题或 area scope" }
            if ($titleName -and (-not $scope -or $titleRules -eq 0)) { "$($document.Name): title $titleName 缺少 scope 或 Rules" }
            # 当前文件（含 Rejected/Signals）的编号上限；无 Git 的快照不声称追溯历史退休号。
            $withoutNext = ($lines -join "`n") -replace '(?m)^Next id:.*$', ''
            foreach ($mention in [regex]::Matches($withoutNext, '\b' + [regex]::Escape($prefix) + '-(\d+)\b')) { $maximum = [Math]::Max($maximum, [int]$mention.Groups[1].Value) }
        }
        if ($next -le $maximum) { "$($area.Name): Next id 必须大于当前已用/退役编号 $prefix-$maximum" }
        if (-not $split -and $count -gt 15) { "$($area.Name): $count 条规则超过 15，须整体 split" }
    }
    foreach ($title in @($all | Where-Object { $_.BaseName -match '\.' })) {
        $parent = ($title.BaseName -split '\.')[0] + '.md'
        if ($all.Name -notcontains $parent) { "$($title.Name): 孤立 title，缺少 area 索引 $parent" }
    }
}

function Invoke-DocumentationCheck {
    param([Parameter(Mandatory)][string]$RepositoryRoot)
    $root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\','/')
    $errors = New-Object 'System.Collections.Generic.List[string]'
    $files = @(Get-DocumentationFiles -RepositoryRoot $root)
    $anchors = @{}
    foreach ($file in $files) {
        $number = 0
        foreach ($line in @(Get-MarkdownBody -Path $file.FullName)) {
            $number++
            # 内联链接/图片和引用定义。仅检查本地相对目标，不访问网络。
            $links = @([regex]::Matches($line, '\]\((<[^>]+>|[^\s)]+)(?:\s+"[^"]*")?\)') | ForEach-Object { $_.Groups[1].Value })
            if ($line -match '^\s{0,3}\[[^\]]+\]:\s*(<[^>]+>|\S+)') { $links += $Matches[1] }
            foreach ($raw in $links) {
                $href = [Uri]::UnescapeDataString($raw.Trim('<','>'))
                if ($href -match '^[A-Za-z][A-Za-z0-9+.-]*:' -or $href.StartsWith('/')) { continue }
                $parts = $href -split '#',2
                $target = ($parts[0] -split '\?',2)[0]
                $destination = if ($target) { [IO.Path]::GetFullPath((Join-Path $file.DirectoryName $target)) } else { $file.FullName }
                $location = $file.FullName.Substring($root.Length + 1).Replace('\','/') + ':' + $number
                if (-not $destination.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
                    [void]$errors.Add("${location}: 链接超出仓库：$href"); continue
                }
                if (-not (Test-Path -LiteralPath $destination)) { [void]$errors.Add("${location}: 缺少链接目标：$href"); continue }
                if ($parts.Count -eq 2 -and $parts[1] -and [IO.Path]::GetExtension($destination) -eq '.md') {
                    if (-not $anchors.ContainsKey($destination)) { $anchors[$destination] = @(Get-MarkdownAnchors -Path $destination) }
                    if ($anchors[$destination] -cnotcontains $parts[1]) { [void]$errors.Add("${location}: 缺少锚点：$href") }
                }
            }
        }
    }
    foreach ($violation in @(Get-ShapeViolations -RepositoryRoot $root)) { [void]$errors.Add($violation) }
    if ($errors.Count) { throw ("文档检查失败：`n" + ($errors -join "`n")) }
    Write-NativeUtf8Line -Text "文档检查通过：$($files.Count) 份自维护 Markdown。"
}
function Invoke-DocumentationIndexCheck {
    param([Parameter(Mandatory)][string]$RepositoryRoot)
    $snapshotRoot = Join-Path ([IO.Path]::GetTempPath()) ('mp3-doc-index-' + [guid]::NewGuid().ToString('N'))
    try {
        [void][IO.Directory]::CreateDirectory($snapshotRoot)
        $prefix = $snapshotRoot.Replace('\','/').TrimEnd('/') + '/'
        Invoke-ExternalCommand -CommandPath (Get-ExternalCommand -Name 'git') -Arguments @('-C',$RepositoryRoot,'checkout-index','--all','--force',"--prefix=$prefix")
        # 检查本身只读目录，不要求快照内存在 .git。
        Invoke-DocumentationCheck -RepositoryRoot $snapshotRoot
    } finally {
        $resolved = [IO.Path]::GetFullPath($snapshotRoot)
        $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
        if (-not $resolved.StartsWith($temp + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "拒绝清理临时快照外路径：$resolved" }
        if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    Complete-Utf8EntryScript -Action {
        $root = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
        if ($Snapshot -eq 'Index') { Invoke-DocumentationIndexCheck -RepositoryRoot $root }
        else { Invoke-DocumentationCheck -RepositoryRoot $root }
    }
}
