<#
.SYNOPSIS
STY-2 的机械部分：自维护 C 函数定义必须紧邻中文 Doxygen。

.DESCRIPTION
只判断“有没有”，不判断“写得好不好”（描述措辞仍由 code-review 审阅）：
  (a) 函数定义上方紧邻没有 /** ... */（或 /*! ... */）块；
  (b) 块里没有 @brief；
  (c) 缺少任一形参名的 @param；
  (d) 非 void 函数缺少 @return / @retval。
新函数（本次差异里新增的函数）另要求 @param 带方向 [in] / [out] / [in,out]；
旧函数被改动时继续容忍旧的无方向写法（STY-2“旧无方向注释维护时补齐”）。
声明（以 ; 结尾）不在检查范围，头文件里的 static inline 定义与 .c 一样要检查。

范围（避免存量欠账挡住 FAST）：
  - Index        ：暂存区相对 HEAD 的差异（pre-commit），文件内容取暂存版本；
  - WorkingTree  ：工作树相对 HEAD 的差异加未跟踪文件（本地 FAST、CHANGED、FULL 内的 FAST）；
  - -BaseRef     ：BaseRef..HeadRef 的差异（CHANGED 的 pre-push 与 -BaseRef 模式）；
  以上三种都只报“差异里新增或修改过的行所落入的函数”，未触及的存量函数永远不报。
  - -Report      ：全仓库存量统计，只打印计数，永远不失败（FULL 不跑它，需要时手动执行）。

排除（复用而非另立清单）：check-generated-write.ps1 的受保护前缀、agent_hook_helpers.ps1 的
CubeMX 生成/USER CODE 判定（Core/、FATFS/、USB_DEVICE/ 下含 ST 声明或 USER CODE 的文件），
再加 Middlewares/、third_party、vendor、build 目录与非 .c/.h 文件。

函数定义查找器的限制（遇到无法解析的写法一律静默跳过，不报错）：
  - 先把注释、字符串、字符常量和预处理行（含续行宏）抹成空白，再在大括号深度 0 处找 `名字(形参) {`；
  - 要求头部以 `)` 结尾、名字前有返回类型：K&R 旧式定义、返回函数指针的定义
    （void (*f(int))(int)）、没有返回类型的宏包装（TEST_CASE(x) {）都被跳过；
  - extern "C" { 视为透明；结构体/联合体/枚举/带 = 的初始化块不是函数；
  - 无名形参、`...` 不要求 @param；函数指针形参取括号里的名字；
  - 文档块与函数之间允许预处理行与属性，不允许空行或其它注释；含 @file 的块不算函数文档；
  - “新函数”判定：名字所在行在新增行里，且被删除的行里没有同名调用形式（名字加左括号）；
    整文件新增则全部视为新函数；仅删除而无新增行的 hunk 只会命中其内部的函数，
    删光紧邻函数上方的文档这种纯删除不会被发现。
#>
[CmdletBinding()]
param(
    [ValidateSet('WorkingTree', 'Index')]
    [string]$Snapshot = 'WorkingTree',

    [string]$BaseRef,

    [string]$HeadRef = 'HEAD',

    [string[]]$ChangedPath,

    [switch]$Report
)

# 复用的检查脚本带有同名参数（Snapshot、ChangedPath），被点源后会覆盖这里的值，先保存。
$entrySnapshot = $Snapshot
$entryChangedPath = $ChangedPath
$entryChangedPathSpecified = $PSBoundParameters.ContainsKey('ChangedPath')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'harness_helpers.ps1')
. (Join-Path -Path $PSScriptRoot -ChildPath 'hooks/agent_hook_helpers.ps1')

$script:DoxygenEmptyTree = '4b825dc642cb6eb9a060e54bf8d69288fbee4904'

# 一次扫描同时抹掉预处理行、块注释、行注释、字符串和字符常量；左到右最早匹配优先，
# 所以字符串里的 // 或 /* 不会被误当注释。
$script:DoxygenMaskRegex = New-Object System.Text.RegularExpressions.Regex -ArgumentList @(
    ('(?<pre>^[ \t]*#(?:\\\n|(?s:/\*.*?\*/)|"(?:\\.|[^"\\\n])*"|[^\n])*)' +
        '|(?<blk>(?s:/\*.*?\*/))' +
        '|(?<line>//[^\n]*)' +
        '|(?<str>"(?:\\.|[^"\\\n])*")' +
        "|(?<chr>'(?:\\.|[^'\\\n])*')"),
    [System.Text.RegularExpressions.RegexOptions]::Multiline
)
$script:DoxygenStructureRegex = New-Object System.Text.RegularExpressions.Regex -ArgumentList @('[{};]')
$script:DoxygenTypeKeywords = @('int', 'char', 'short', 'long', 'float', 'double', 'signed', 'unsigned', 'void', '_Bool')
$script:DoxygenNonFunctionNames = @('if', 'for', 'while', 'switch', 'return', 'sizeof', 'defined')

function Get-DoxygenMaskedSource {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Text
    )

    $normalized = ($Text -replace "`r`n", "`n").TrimStart([char]0xFEFF)
    $builder = New-Object System.Text.StringBuilder $normalized.Length
    $comments = New-Object System.Collections.Generic.List[object]
    $position = 0
    foreach ($match in $script:DoxygenMaskRegex.Matches($normalized)) {
        [void]$builder.Append($normalized, $position, $match.Index - $position)
        [void]$builder.Append([regex]::Replace($match.Value, '[^\n]', ' '))
        $position = $match.Index + $match.Length
        $isBlock = $match.Groups['blk'].Success
        if ($isBlock -or $match.Groups['line'].Success) {
            [void]$comments.Add([pscustomobject]@{
                Start = $match.Index
                End = $position
                Kind = if ($isBlock) { 'blk' } else { 'line' }
                Text = $match.Value
            })
        }
    }
    [void]$builder.Append($normalized, $position, $normalized.Length - $position)

    $newlines = New-Object System.Collections.Generic.List[int]
    for ($index = $normalized.IndexOf("`n"); $index -ge 0; $index = $normalized.IndexOf("`n", $index + 1)) {
        [void]$newlines.Add($index)
    }

    return [pscustomobject]@{
        Text = $normalized
        Masked = $builder.ToString()
        Comments = $comments.ToArray()
        NewlineOffsets = [int[]]$newlines.ToArray()
    }
}

function Get-DoxygenLineNumber {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][int[]]$NewlineOffsets,
        [Parameter(Mandatory)][int]$Offset
    )

    if ($NewlineOffsets.Count -eq 0) {
        return 1
    }

    $found = [System.Array]::BinarySearch($NewlineOffsets, $Offset)
    $before = if ($found -ge 0) { $found } else { -bnot $found }
    return $before + 1
}

# 把形参列表按顶层逗号拆开（括号、方括号里的逗号不拆）。
function Split-CParameterList {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Text
    )

    $parts = New-Object System.Collections.Generic.List[string]
    $depth = 0
    $start = 0
    for ($index = 0; $index -lt $Text.Length; $index++) {
        $character = $Text[$index]
        if ($character -in @('(', '[', '{')) { $depth++ }
        elseif ($character -in @(')', ']', '}')) { $depth-- }
        elseif (($character -eq ',') -and ($depth -eq 0)) {
            [void]$parts.Add($Text.Substring($start, $index - $start))
            $start = $index + 1
        }
    }
    [void]$parts.Add($Text.Substring($start))
    return $parts.ToArray()
}

# 取一个形参声明里的形参名；无名形参（只有类型）返回 $null。
function Get-CParameterName {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Declaration
    )

    $text = $Declaration.Trim()
    if (($text.Length -eq 0) -or ($text -eq '...')) {
        return $null
    }

    $pointer = [regex]::Match($text, '\(\s*\*+\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)')
    if ($pointer.Success) {
        return $pointer.Groups[1].Value
    }

    $text = [regex]::Replace($text, '(\s*\[[^\]]*\])+\s*$', '')
    $nameMatch = [regex]::Match($text, '(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*)$')
    if (-not $nameMatch.Success) {
        return $null
    }

    $name = $nameMatch.Groups[1].Value
    $rest = $text.Substring(0, $nameMatch.Index).Trim()
    if (($rest.Length -eq 0) -or ($rest -match '^(struct|union|enum)$') -or ($name -in $script:DoxygenTypeKeywords)) {
        return $null
    }

    return $name
}

# 解析大括号前的头部文本；不是函数定义（或无法解析）时返回 $null。
function ConvertFrom-CFunctionHeader {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Header
    )

    $text = [regex]::Replace($Header,
        '__attribute__\s*\(\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\)\)', ' ')
    $text = ($text -replace '\s+', ' ').Trim()
    if (($text.Length -eq 0) -or ($text[$text.Length - 1] -ne ')')) {
        return $null
    }

    $depth = 0
    $openIndex = -1
    for ($index = $text.Length - 1; $index -ge 0; $index--) {
        if ($text[$index] -eq ')') { $depth++ }
        elseif ($text[$index] -eq '(') {
            $depth--
            if ($depth -eq 0) {
                $openIndex = $index
                break
            }
        }
    }
    if ($openIndex -lt 1) {
        return $null
    }

    $before = $text.Substring(0, $openIndex).TrimEnd()
    $nameMatch = [regex]::Match($before, '(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*)$')
    if (-not $nameMatch.Success) {
        return $null
    }

    $name = $nameMatch.Groups[1].Value
    $prefix = $before.Substring(0, $nameMatch.Index).Trim()
    if (($prefix.Length -eq 0) -or $prefix.Contains('=') -or ($name -in $script:DoxygenNonFunctionNames)) {
        return $null
    }

    $inner = $text.Substring($openIndex + 1, $text.Length - $openIndex - 2)
    $names = New-Object System.Collections.Generic.List[string]
    $declarations = @(Split-CParameterList -Text $inner)
    if (-not (($declarations.Count -eq 1) -and ($declarations[0].Trim() -in @('', 'void')))) {
        foreach ($declaration in $declarations) {
            $parameterName = Get-CParameterName -Declaration $declaration
            if ($null -ne $parameterName) {
                [void]$names.Add($parameterName)
            }
        }
    }

    return [pscustomobject]@{
        Name = $name
        Parameters = $names.ToArray()
        IsVoid = [bool]($prefix -match '(?<![A-Za-z0-9_])void$')
    }
}

# 只有 /** 或 /*! 开头、且不是 @file 文件头的块才算函数文档。
function Test-DoxygenFunctionComment {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Comment
    )

    return ($Comment.Kind -eq 'blk') -and
        ($Comment.Text -match '^/\*[*!](?!/)') -and
        ($Comment.Text -notmatch '[@\\]file\b')
}

# 返回文本中的全部函数定义。Doc 为紧邻上方的文档块（没有则 $null）。
function Get-CFunctionDefinition {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Text
    )

    $source = Get-DoxygenMaskedSource -Text $Text
    $masked = $source.Masked
    $comments = $source.Comments
    $results = New-Object System.Collections.Generic.List[object]
    $depth = 0
    $boundary = -1
    $open = $null

    foreach ($match in $script:DoxygenStructureRegex.Matches($masked)) {
        $offset = $match.Index
        $character = $match.Value
        if ($character -eq ';') {
            if ($depth -eq 0) { $boundary = $offset }
        }
        elseif ($character -eq '{') {
            if ($depth -eq 0) {
                $header = $masked.Substring($boundary + 1, $offset - $boundary - 1)
                if ($header.Trim() -ceq 'extern') {
                    # extern "C" { 对其内部定义透明。
                    $boundary = $offset
                    continue
                }

                $open = [pscustomobject]@{
                    HeaderStart = $boundary + 1
                    Header = $header
                    Parsed = ConvertFrom-CFunctionHeader -Header $header
                }
            }
            $depth++
        }
        else {
            if ($depth -gt 0) {
                $depth--
                if (($depth -eq 0) -and ($null -ne $open)) {
                    if ($null -ne $open.Parsed) {
                        [void]$results.Add((New-CFunctionRecord -Source $source -Open $open -EndOffset $offset))
                        $boundary = $offset
                    }
                    else {
                        $trimmed = $open.Header.Trim()
                        # 类型定义和带初始化的对象要等到 `;` 才结束；其余无法识别的块到 `}` 结束。
                        if (-not (($trimmed -match '^(typedef|struct|union|enum)\b') -or $trimmed.Contains('='))) {
                            $boundary = $offset
                        }
                    }
                    $open = $null
                }
            }
            else {
                $boundary = $offset
            }
        }
    }

    return $results.ToArray()
}

function New-CFunctionRecord {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Source,
        [Parameter(Mandatory)]$Open,
        [Parameter(Mandatory)][int]$EndOffset
    )

    $header = $Open.Header
    $headerOffset = $Open.HeaderStart + ($header.Length - $header.TrimStart().Length)
    $parsed = $Open.Parsed

    $nameOffset = $headerOffset
    $nameMatches = [regex]::Matches($header, '(?<![A-Za-z0-9_])' + [regex]::Escape($parsed.Name) + '\s*\(')
    if ($nameMatches.Count -gt 0) {
        $nameOffset = $Open.HeaderStart + $nameMatches[$nameMatches.Count - 1].Index
    }

    # 头部起点之前最后一个注释；它必须落在上一个语句边界之后，且中间只有空白/预处理行。
    $doc = $null
    $candidate = $null
    foreach ($comment in $Source.Comments) {
        if ($comment.End -le $headerOffset) { $candidate = $comment } else { break }
    }
    if (($null -ne $candidate) -and ($candidate.Start -ge $Open.HeaderStart) -and (Test-DoxygenFunctionComment -Comment $candidate)) {
        $gapLines = @($Source.Text.Substring($candidate.End, $headerOffset - $candidate.End) -split "`n")
        $hasBlankLine = $false
        for ($index = 1; $index -lt ($gapLines.Count - 1); $index++) {
            if ($gapLines[$index].Trim().Length -eq 0) { $hasBlankLine = $true }
        }
        if (-not $hasBlankLine) {
            $doc = $candidate
        }
    }

    $headerLine = Get-DoxygenLineNumber -NewlineOffsets $Source.NewlineOffsets -Offset $headerOffset
    $startLine = if ($null -ne $doc) {
        Get-DoxygenLineNumber -NewlineOffsets $Source.NewlineOffsets -Offset $doc.Start
    }
    else {
        $headerLine
    }

    return [pscustomobject]@{
        Name = $parsed.Name
        Parameters = $parsed.Parameters
        IsVoid = $parsed.IsVoid
        Doc = $doc
        StartLine = $startLine
        NameLine = Get-DoxygenLineNumber -NewlineOffsets $Source.NewlineOffsets -Offset $nameOffset
        EndLine = Get-DoxygenLineNumber -NewlineOffsets $Source.NewlineOffsets -Offset $EndOffset
    }
}

# 返回该函数的缺项说明（每项一句）；合格返回空数组。
function Get-DoxygenFunctionViolation {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Function,
        [switch]$RequireDirection
    )

    if ($null -eq $Function.Doc) {
        return @('缺少紧邻的 /** ... */ Doxygen 块')
    }

    $reasons = New-Object System.Collections.Generic.List[string]
    $text = $Function.Doc.Text
    if ($text -notmatch '[@\\]brief\b') {
        [void]$reasons.Add('缺少 @brief')
    }

    $documented = @{}
    foreach ($match in [regex]::Matches($text, '[@\\]param(?:\s*\[(?<dir>[^\]]*)\])?\s+(?<name>[A-Za-z_][A-Za-z0-9_]*)')) {
        $name = $match.Groups['name'].Value
        $direction = $match.Groups['dir'].Value
        $valid = $match.Groups['dir'].Success -and ($direction -match '^\s*(?i:in|out|in\s*,\s*out)\s*$')
        if (-not $documented.ContainsKey($name)) { $documented[$name] = $false }
        if ($valid) { $documented[$name] = $true }
    }

    foreach ($parameter in $Function.Parameters) {
        if (-not $documented.ContainsKey($parameter)) {
            [void]$reasons.Add("缺少 @param $parameter")
        }
        elseif ($RequireDirection -and -not $documented[$parameter]) {
            [void]$reasons.Add("@param $parameter 缺少方向（新函数须写 [in]/[out]/[in,out]）")
        }
    }

    if ((-not $Function.IsVoid) -and ($text -notmatch '[@\\](?:returns?|retval)\b')) {
        [void]$reasons.Add('缺少 @return/@retval（非 void 函数）')
    }

    return $reasons.ToArray()
}

function Test-DoxygenExcludedPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][AllowEmptyString()][string]$RelativePath
    )

    $normalized = Get-HarnessNormalizedPath -RelativePath $RelativePath
    if ($normalized -notmatch '(?i)\.[ch]$') {
        return $true
    }
    if (Test-GeneratedWriteProtectedPath -RelativePath $normalized) {
        return $true
    }
    if ($normalized -match '(?i)(^|/)(Middlewares|third_party|thirdparty|vendor|build)/') {
        return $true
    }
    if ($null -ne (Get-AgentHookCubeMxFileKind -RepositoryRoot $RepositoryRoot -RelativePath $normalized)) {
        return $true
    }

    return $false
}

function ConvertTo-DoxygenProcessArgument {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$Argument
    )

    if (($Argument.Length -gt 0) -and ($Argument -notmatch '[\s"]')) {
        return $Argument
    }

    return '"' + ($Argument -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

# 以 UTF-8 原样取 git 输出；Windows PowerShell 管道会按系统代码页解码，可能吞掉紧跟中文的 `@`。
function Invoke-DoxygenGit {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string[]]$Arguments,
        [switch]$AllowFailure
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = Get-ExternalCommand -Name 'git'
    $allArguments = @('-C', $RepositoryRoot, '-c', 'core.quotepath=false') + $Arguments
    $startInfo.Arguments = (@($allArguments | ForEach-Object { ConvertTo-DoxygenProcessArgument -Argument $_ })) -join ' '
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = New-Object System.Text.UTF8Encoding $false

    $process = [System.Diagnostics.Process]::Start($startInfo)
    try {
        $errorTask = $process.StandardError.ReadToEndAsync()
        $output = $process.StandardOutput.ReadToEnd()
        $process.WaitForExit()
        $errorText = $errorTask.Result
        $exitCode = $process.ExitCode
    }
    finally {
        $process.Dispose()
    }

    if ($exitCode -ne 0) {
        if ($AllowFailure) {
            return $null
        }
        throw "Git 命令失败，退出码：$exitCode；参数：$($Arguments -join ' ')；$($errorText.Trim())"
    }

    return $output
}

function ConvertFrom-DoxygenDiff {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$DiffText
    )

    $entries = New-Object System.Collections.Generic.List[object]
    $current = $null
    $inHunk = $false
    $oldIsNull = $false
    foreach ($line in ($DiffText -split "`r?`n")) {
        if ($line.StartsWith('diff --git ')) {
            $current = $null
            $inHunk = $false
            $oldIsNull = $false
            continue
        }

        if (-not $inHunk) {
            if ($line.StartsWith('--- ')) {
                $oldIsNull = ($line -eq '--- /dev/null')
                continue
            }
            if ($line.StartsWith('+++ ')) {
                $target = $line.Substring(4).TrimEnd("`t")
                if ($target -eq '/dev/null') {
                    $current = $null
                }
                else {
                    $current = [pscustomobject]@{
                        Path = $target -replace '^b/', ''
                        IsNewFile = $oldIsNull
                        Ranges = New-Object System.Collections.Generic.List[object]
                        Removed = New-Object System.Text.StringBuilder
                    }
                    [void]$entries.Add($current)
                }
                continue
            }
        }

        $hunk = [regex]::Match($line, '^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@')
        if ($hunk.Success) {
            $inHunk = $true
            if ($null -ne $current) {
                $count = if ($hunk.Groups[2].Success) { [int]$hunk.Groups[2].Value } else { 1 }
                [void]$current.Ranges.Add([pscustomobject]@{ Start = [int]$hunk.Groups[1].Value; Count = $count })
            }
            continue
        }

        if ($inHunk -and ($null -ne $current) -and $line.StartsWith('-')) {
            [void]$current.Removed.Append($line.Substring(1)).Append("`n")
        }
    }

    return $entries.ToArray()
}

# 取本次要检查的文件及其新增/修改的行范围。
function Get-DoxygenChangeSet {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][ValidateSet('WorkingTree', 'Index', 'Range')][string]$Source,
        [string]$BaseRef,
        [string]$HeadRef = 'HEAD'
    )

    $arguments = @('diff', '-U0', '--no-color', '--no-ext-diff', '--src-prefix=a/', '--dst-prefix=b/', '--diff-filter=ACMR')
    if ($Source -eq 'Index') {
        $arguments += '--cached'
    }
    elseif ($Source -eq 'Range') {
        $arguments += @($BaseRef, $HeadRef)
    }
    else {
        $hasHead = $null -ne (Invoke-DoxygenGit -RepositoryRoot $RepositoryRoot `
                -Arguments @('rev-parse', '--verify', '--quiet', 'HEAD') -AllowFailure)
        $arguments += $(if ($hasHead) { 'HEAD' } else { $script:DoxygenEmptyTree })
    }
    $arguments += @('--', '*.c', '*.h')

    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($entry in @(ConvertFrom-DoxygenDiff -DiffText (Invoke-DoxygenGit -RepositoryRoot $RepositoryRoot -Arguments $arguments))) {
        [void]$entries.Add([pscustomobject]@{
            Path = $entry.Path
            IsNewFile = $entry.IsNewFile
            Ranges = $entry.Ranges.ToArray()
            RemovedText = $entry.Removed.ToString()
        })
    }

    if ($Source -eq 'WorkingTree') {
        $untracked = Invoke-DoxygenGit -RepositoryRoot $RepositoryRoot `
            -Arguments @('ls-files', '--others', '--exclude-standard', '--', '*.c', '*.h')
        foreach ($path in ($untracked -split "`r?`n")) {
            if (-not [string]::IsNullOrWhiteSpace($path)) {
                [void]$entries.Add([pscustomobject]@{
                    Path = $path.Trim()
                    IsNewFile = $true
                    Ranges = @()
                    RemovedText = ''
                })
            }
        }
    }

    return $entries.ToArray()
}

function Get-DoxygenFileText {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][ValidateSet('WorkingTree', 'Index', 'Range')][string]$Source,
        [Parameter(Mandatory)][string]$RelativePath,
        [string]$HeadRef = 'HEAD'
    )

    if ($Source -eq 'WorkingTree') {
        $fullPath = Join-Path -Path $RepositoryRoot -ChildPath $RelativePath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            return $null
        }
        return [System.IO.File]::ReadAllText($fullPath, (New-Object System.Text.UTF8Encoding $false))
    }

    $object = if ($Source -eq 'Index') { ":$RelativePath" } else { "${HeadRef}:$RelativePath" }
    return Invoke-DoxygenGit -RepositoryRoot $RepositoryRoot -Arguments @('show', $object) -AllowFailure
}

function Test-DoxygenFunctionTouched {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Function,
        [Parameter(Mandatory)]$Change
    )

    if ($Change.IsNewFile) {
        return $true
    }

    foreach ($range in $Change.Ranges) {
        if ($range.Count -gt 0) {
            $last = $range.Start + $range.Count - 1
            if (($range.Start -le $Function.EndLine) -and ($last -ge $Function.StartLine)) {
                return $true
            }
        }
        elseif (($range.Start -ge $Function.StartLine) -and ($range.Start -lt $Function.EndLine)) {
            return $true
        }
    }

    return $false
}

function Test-DoxygenFunctionIsNew {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Function,
        [Parameter(Mandatory)]$Change
    )

    if ($Change.IsNewFile) {
        return $true
    }

    $nameAdded = $false
    foreach ($range in $Change.Ranges) {
        if (($range.Count -gt 0) -and ($Function.NameLine -ge $range.Start) -and
            ($Function.NameLine -le ($range.Start + $range.Count - 1))) {
            $nameAdded = $true
        }
    }
    if (-not $nameAdded) {
        return $false
    }

    $namePattern = '(?<![A-Za-z0-9_])' + [regex]::Escape($Function.Name) + '\s*\('
    return -not [regex]::IsMatch($Change.RemovedText, $namePattern)
}

# 差异范围内的检查：返回对象含 Violations（字符串数组，每个函数一条）、CheckedFunctionCount、FileCount。
function Get-DoxygenCheckResult {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [ValidateSet('WorkingTree', 'Index')][string]$Snapshot = 'WorkingTree',
        [string]$BaseRef,
        [string]$HeadRef = 'HEAD',
        [string[]]$ChangedPath
    )

    $source = if ([string]::IsNullOrWhiteSpace($BaseRef)) { $Snapshot } else { 'Range' }
    $filter = $null
    if ($PSBoundParameters.ContainsKey('ChangedPath')) {
        $filter = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
        foreach ($path in @(Get-HarnessUniquePaths -Path $ChangedPath)) { [void]$filter.Add($path) }
    }

    $violations = New-Object System.Collections.Generic.List[string]
    $skipped = New-Object System.Collections.Generic.List[string]
    $checked = 0
    $fileCount = 0
    $changes = @(Get-DoxygenChangeSet -RepositoryRoot $RepositoryRoot -Source $source -BaseRef $BaseRef -HeadRef $HeadRef)
    foreach ($change in $changes) {
        $path = Get-HarnessNormalizedPath -RelativePath $change.Path
        if (($null -ne $filter) -and -not $filter.Contains($path)) { continue }
        if (Test-DoxygenExcludedPath -RepositoryRoot $RepositoryRoot -RelativePath $path) { continue }

        $text = Get-DoxygenFileText -RepositoryRoot $RepositoryRoot -Source $source -RelativePath $path -HeadRef $HeadRef
        if ($null -eq $text) { continue }

        $fileCount++
        try {
            $definitions = @(Get-CFunctionDefinition -Text $text)
        }
        catch {
            # 解析器自身出错时跳过该文件，不让无法解析的写法挡住提交。
            [void]$skipped.Add($path)
            continue
        }

        foreach ($definition in $definitions) {
            if (-not (Test-DoxygenFunctionTouched -Function $definition -Change $change)) { continue }
            $checked++
            $isNew = Test-DoxygenFunctionIsNew -Function $definition -Change $change
            $reasons = @(Get-DoxygenFunctionViolation -Function $definition -RequireDirection:$isNew)
            if ($reasons.Count -gt 0) {
                [void]$violations.Add("${path}:$($definition.NameLine): $($definition.Name)：$($reasons -join '；')")
            }
        }
    }

    return [pscustomobject]@{
        Source = $source
        FileCount = $fileCount
        CheckedFunctionCount = $checked
        Violations = $violations.ToArray()
        SkippedFiles = $skipped.ToArray()
    }
}

# 全仓库存量统计（不看差异，不要求方向），只供报告，调用方不得据此失败。
function Get-DoxygenReport {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot
    )

    $tracked = Invoke-DoxygenGit -RepositoryRoot $RepositoryRoot -Arguments @('ls-files', '--', '*.c', '*.h')
    $paths = @($tracked -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $functionCount = 0
    $violationCount = 0
    $fileCount = 0
    $byDirectory = @{}
    foreach ($path in $paths) {
        if (Test-DoxygenExcludedPath -RepositoryRoot $RepositoryRoot -RelativePath $path) { continue }
        $text = Get-DoxygenFileText -RepositoryRoot $RepositoryRoot -Source WorkingTree -RelativePath $path
        if ($null -eq $text) { continue }

        $fileCount++
        try { $definitions = @(Get-CFunctionDefinition -Text $text) } catch { continue }
        foreach ($definition in $definitions) {
            $functionCount++
            if (@(Get-DoxygenFunctionViolation -Function $definition).Count -gt 0) {
                $violationCount++
                $directory = ($path -split '/')[0]
                $byDirectory[$directory] = 1 + $(if ($byDirectory.ContainsKey($directory)) { $byDirectory[$directory] } else { 0 })
            }
        }
    }

    return [pscustomobject]@{
        FileCount = $fileCount
        FunctionCount = $functionCount
        ViolationCount = $violationCount
        ByDirectory = $byDirectory
    }
}

# 推送更新对应的差异起点：已有远端分支用远端 SHA；新分支取尚未在远端的最早提交的父提交。
function Get-DoxygenPushBaseRef {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$RemoteName,
        [Parameter(Mandatory)]$Update
    )

    if ($Update.LocalSha -match '^0+$') {
        return $null
    }
    if ($Update.RemoteSha -notmatch '^0+$') {
        return $Update.RemoteSha
    }

    $commits = @(Get-HarnessGitOutputLines -RepositoryRoot $RepositoryRoot -Arguments @(
            'rev-list', $Update.LocalSha, '--not', "--remotes=$RemoteName"))
    if ($commits.Count -eq 0) {
        return $null
    }

    $oldest = $commits[$commits.Count - 1]
    # 单行输出会被管道展开成字符串，必须先包成数组再取 [0]。
    $parentLine = @(Get-HarnessGitOutputLines -RepositoryRoot $RepositoryRoot -Arguments @(
            'rev-list', '--parents', '-n', '1', $oldest))[0]
    $fields = @($parentLine -split '\s+')
    if ($fields.Count -ge 2) {
        return $fields[1]
    }

    return $script:DoxygenEmptyTree
}

function Invoke-DoxygenCheck {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [ValidateSet('WorkingTree', 'Index')][string]$Snapshot = 'WorkingTree',
        [string]$BaseRef,
        [string]$HeadRef = 'HEAD',
        [string[]]$ChangedPath
    )

    $parameters = @{
        RepositoryRoot = $RepositoryRoot
        Snapshot = $Snapshot
        BaseRef = $BaseRef
        HeadRef = $HeadRef
    }
    if ($PSBoundParameters.ContainsKey('ChangedPath')) {
        $parameters.ChangedPath = $ChangedPath
    }
    $result = Get-DoxygenCheckResult @parameters

    if ($result.Violations.Count -gt 0) {
        $header = "Doxygen 检查失败（STY-2），共 $($result.Violations.Count) 个函数："
        $hint = 'STY-2：自维护函数定义紧邻完整中文 Doxygen（@brief、每个形参的 @param、非 void 的 @return/@retval）；' +
            '新函数的 @param 须带 [in]/[out]/[in,out]。未被本次差异触及的存量函数不报。规则见 docs/shape/code-style.md。'
        throw ($header + [Environment]::NewLine +
            (($result.Violations | ForEach-Object { "  $_" }) -join [Environment]::NewLine) +
            [Environment]::NewLine + [Environment]::NewLine + $hint)
    }

    $scope = switch ($result.Source) {
        'Index' { '暂存区' }
        'Range' { "$BaseRef..$HeadRef" }
        default { '工作树' }
    }
    Write-NativeUtf8Line -Text ("Doxygen 检查通过：$scope 中 $($result.FileCount) 个改动文件内 " +
        "$($result.CheckedFunctionCount) 个改动函数合规。")
    foreach ($skippedPath in $result.SkippedFiles) {
        Write-NativeUtf8Line -Text "Doxygen 检查已跳过无法解析的文件：$skippedPath"
    }
}

function Write-DoxygenReport {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot
    )

    $report = Get-DoxygenReport -RepositoryRoot $RepositoryRoot
    Write-NativeUtf8Line -Text ("Doxygen 存量报告（仅计数，不失败）：$($report.FileCount) 个手写 C/H 文件，" +
        "$($report.FunctionCount) 个函数定义，其中 $($report.ViolationCount) 个不满足 STY-2 的机械项（不含方向检查）。")
    foreach ($directory in ($report.ByDirectory.Keys | Sort-Object)) {
        Write-NativeUtf8Line -Text "  ${directory}: $($report.ByDirectory[$directory])"
    }
}

$isDotSourced = $MyInvocation.InvocationName -eq '.'
if (-not $isDotSourced) {
    Complete-Utf8EntryScript -Action {
        $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
        if ($Report) {
            Write-DoxygenReport -RepositoryRoot $repositoryRoot
            return
        }

        $parameters = @{
            RepositoryRoot = $repositoryRoot
            Snapshot = $entrySnapshot
            BaseRef = $BaseRef
            HeadRef = $HeadRef
        }
        if ($entryChangedPathSpecified) {
            $parameters.ChangedPath = $entryChangedPath
        }
        Invoke-DoxygenCheck @parameters
    }
}
