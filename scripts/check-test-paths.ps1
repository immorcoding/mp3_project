[CmdletBinding()]
param()

# 主机测试路径漂移检查。
#
# 背景：scripts/rules/verification.psd1 的 HostModuleRules 用手写 PathPatterns 决定“改了某个路径应跑哪些主机测试模块”。
# 模块实际编译的头文件会随代码演进而增加，手写模式追不上，结果是改了依赖头却不选中依赖它的模块，只有 FULL 才发现。
#
# 做法（按需计算，不提交缓存）：
#   1. 对 AllHostModules 的每个模块，用 `cmake -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 把 Tests/<模块>/ 配置到临时目录
#      （只配置、不构建；各模块并行配置）。
#   2. 读取 compile_commands.json，对每条编译命令去掉 -o 后追加 `-MM`，让 GCC 预处理并输出依赖
#      （与真实构建同一组 -I / -D，GCC 自行解析 #if，不重新实现 include 解析）。
#   3. 把依赖换算成仓库相对路径，要求每条都被该模块的某个 PathPatterns 命中，或位于该模块自己的 Tests/<模块>/ 树。
#      未命中的以 `模块 <- 路径` 逐条报告并失败。
#   系统头不会出现在 -MM 输出里；CMake 在构建目录生成的文件（如 flash_ftl 的 host_config 副本）视为生成物，不计入。
#   依赖既不在仓库也不在构建目录时直接失败（路径形式不一致会被发现，而不是被静默忽略）。
#
# 不覆盖的内容（需要人工在 PathPatterns 维护或由 FULL 兜底）：
#   - 只在别的宏定义下才被包含的头文件：预处理按该模块自己的编译参数展开，其他 -D 组合下才出现的头不会被发现。
#   - 不经 #include 进入测试的输入：CMake 用 file(READ) 复制并改写的文件（如 flash_ftl 读取的 ffconf.h、fatfs.h）、
#     链接脚本、测试数据文件，以及 CMakeLists.txt 本身（Tests/<模块>/ 树整体豁免）。
#   - 反方向：PathPatterns 写得过宽（命中了模块并不依赖的路径）不会被报告。
#   - 目录级模式只要命中某个依赖即视为覆盖，新增文件是否落在模式内由路径规则本身保证。
# 本检查需要 cmake、ninja 与主机 GCC（MP3_HOST_CC，缺省为 PATH 中的 gcc），与 test-host.ps1 一致。

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'harness_helpers.ps1')

# 解析 `gcc -MM` 的 Makefile 依赖文本：去掉续行与目标名，还原 `\ ` 转义的空格。
function ConvertFrom-MakeDependencyText {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [AllowEmptyString()]
        [string]$Text
    )

    $joined = $Text -replace '\\\r?\n', ' '
    $joined = $joined -replace '^\s*[^\s:]+:(?=\s)', ''
    $paths = New-Object System.Collections.Generic.List[string]
    foreach ($token in ($joined -split '(?<!\\)\s+')) {
        if ([string]::IsNullOrWhiteSpace($token)) {
            continue
        }

        [void]$paths.Add(($token -replace '\\ ', ' '))
    }

    return $paths.ToArray()
}

# 把 compile_commands.json 的 command 拆为编译器与参数，并去掉 -o 及其参数（依赖输出不写目标文件）。
function Split-CompileCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Command
    )

    if ($Command -notmatch '^\s*(?:"([^"]+)"|(\S+))\s*(.*)$') {
        throw "无法解析编译命令：$Command"
    }

    $executable = if ($Matches[1]) { $Matches[1] } else { $Matches[2] }
    $arguments = $Matches[3] -replace '(^|\s)-o\s+(?:"[^"]*"|\S+)', '$1'
    $arguments = ($arguments -replace '\s{2,}', ' ').Trim()

    return @{ Executable = $executable; Arguments = $arguments }
}

# 依赖路径 -> 仓库相对路径；构建目录下的生成物返回空字符串；两者之外的路径无法归属，直接失败。
function Get-DependencyRelativePath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$ExternalRoot,
        [Parameter(Mandatory)][string]$Path
    )

    $full = [System.IO.Path]::GetFullPath($Path).Replace('\', '/')
    $repositoryPrefix = [System.IO.Path]::GetFullPath($RepositoryRoot).Replace('\', '/').TrimEnd('/') + '/'
    $externalPrefix = [System.IO.Path]::GetFullPath($ExternalRoot).Replace('\', '/').TrimEnd('/') + '/'

    if ($full.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($repositoryPrefix.Length)
    }

    if ($full.StartsWith($externalPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return ''
    }

    throw "依赖路径无法归属到仓库或构建目录：$Path（仓库 $RepositoryRoot）。"
}

# 并行运行一批外部进程（每批最多 Throttle 个），返回 Key -> @{ ExitCode; StdOut; StdErr }。
function Invoke-ProcessBatch {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][object[]]$Item,
        [int]$Throttle = 8
    )

    $results = @{}
    $utf8 = New-Object System.Text.UTF8Encoding $false
    for ($offset = 0; $offset -lt $Item.Count; $offset += $Throttle) {
        $last = [Math]::Min($offset + $Throttle, $Item.Count) - 1
        $running = New-Object System.Collections.Generic.List[object]
        foreach ($entry in $Item[$offset..$last]) {
            $startInfo = New-Object System.Diagnostics.ProcessStartInfo
            $startInfo.FileName = $entry.FilePath
            $startInfo.Arguments = $entry.Arguments
            # compile_commands.json 的 directory 用正斜杠，ProcessStartInfo 只接受本机分隔符。
            $startInfo.WorkingDirectory = [System.IO.Path]::GetFullPath($entry.WorkingDirectory)
            $startInfo.UseShellExecute = $false
            $startInfo.CreateNoWindow = $true
            $startInfo.RedirectStandardOutput = $true
            $startInfo.RedirectStandardError = $true
            $startInfo.StandardOutputEncoding = $utf8
            $startInfo.StandardErrorEncoding = $utf8
            $process = [System.Diagnostics.Process]::Start($startInfo)
            [void]$running.Add(@{
                    Key = $entry.Key
                    Process = $process
                    StdOut = $process.StandardOutput.ReadToEndAsync()
                    StdErr = $process.StandardError.ReadToEndAsync()
                })
        }

        foreach ($job in $running) {
            $job.Process.WaitForExit()
            $results[$job.Key] = @{
                ExitCode = $job.Process.ExitCode
                StdOut = $job.StdOut.Result
                StdErr = $job.StdErr.Result
            }
            $job.Process.Dispose()
        }
    }

    return $results
}

function Get-TestPathsCompiler {
    [CmdletBinding()]
    param()

    # 与 test-host.ps1 一致：不读通用 CC。
    if (-not [string]::IsNullOrWhiteSpace($env:MP3_HOST_CC)) {
        return Get-ExternalCommand -Name $env:MP3_HOST_CC
    }

    return Get-ExternalCommand -Name 'gcc'
}

# 计算每个模块真实编译所依赖的仓库相对路径：Module -> 排序去重的 string[]。
function Get-HostModuleDependency {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string[]]$Module,
        [Parameter(Mandatory)][string]$BuildRoot
    )

    $cmake = Get-ExternalCommand -Name 'cmake'
    [void](Get-ExternalCommand -Name 'ninja')
    $compiler = Get-TestPathsCompiler

    $configureItems = foreach ($name in $Module) {
        $sourceDirectory = Join-Path -Path $RepositoryRoot -ChildPath "Tests/$name"
        if (-not (Test-Path -LiteralPath (Join-Path $sourceDirectory 'CMakeLists.txt') -PathType Leaf)) {
            throw "主机测试模块 '$name' 缺少 Tests/$name/CMakeLists.txt。"
        }

        @{
            Key = $name
            FilePath = $cmake
            Arguments = "-S `"$sourceDirectory`" -B `"$(Join-Path $BuildRoot $name)`" -G Ninja " +
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON `"-DCMAKE_C_COMPILER=$compiler`" -DCMAKE_BUILD_TYPE=Debug"
            WorkingDirectory = $RepositoryRoot
        }
    }

    $configured = Invoke-ProcessBatch -Item @($configureItems)
    foreach ($name in $Module) {
        if ($configured[$name].ExitCode -ne 0) {
            throw "CMake 配置模块 '$name' 失败：$($configured[$name].StdOut)$($configured[$name].StdErr)"
        }
    }

    # 编译命令去掉 -o 后相同的（同一源文件、同一组参数）只预处理一次。
    $scanItems = New-Object System.Collections.Generic.List[object]
    $seen = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($name in $Module) {
        $databasePath = Join-Path -Path (Join-Path $BuildRoot $name) -ChildPath 'compile_commands.json'
        $entries = @()
        if (Test-Path -LiteralPath $databasePath -PathType Leaf) {
            # Windows PowerShell 5.1 经管道时把 JSON 数组当作单个对象；用 -InputObject 后 foreach 才能逐条展开。
            $entries = @(foreach ($parsedEntry in (ConvertFrom-Json -InputObject ([System.IO.File]::ReadAllText($databasePath)))) {
                    $parsedEntry
                })
        }

        if ($entries.Count -eq 0) {
            throw "模块 '$name' 的 compile_commands.json 缺失或为空，无法计算依赖。"
        }

        foreach ($entry in $entries) {
            $command = Split-CompileCommand -Command ([string]$entry.command)
            $key = "$name|$($entry.directory)|$($command.Executable)|$($command.Arguments)"
            if (-not $seen.Add($key)) {
                continue
            }

            [void]$scanItems.Add(@{
                    Key = $key
                    Module = $name
                    Directory = [string]$entry.directory
                    File = [string]$entry.file
                    FilePath = $command.Executable
                    Arguments = $command.Arguments + ' -MM -MT dep'
                    WorkingDirectory = [string]$entry.directory
                })
        }
    }

    $throttle = [Math]::Max(2, [Math]::Min(8, [Environment]::ProcessorCount))
    $scanned = Invoke-ProcessBatch -Item $scanItems.ToArray() -Throttle $throttle

    $dependency = @{}
    foreach ($name in $Module) {
        $dependency[$name] = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    }

    foreach ($scanItem in $scanItems) {
        $result = $scanned[$scanItem.Key]
        if ($result.ExitCode -ne 0) {
            throw "预处理依赖失败（模块 '$($scanItem.Module)'，$($scanItem.File)）：$($result.StdErr)"
        }

        $paths = @(ConvertFrom-MakeDependencyText -Text $result.StdOut)
        if ($paths.Count -eq 0) {
            throw "预处理未输出任何依赖（模块 '$($scanItem.Module)'，$($scanItem.File)）。"
        }

        foreach ($path in $paths) {
            $rooted = if ([System.IO.Path]::IsPathRooted($path)) { $path } else { Join-Path $scanItem.Directory $path }
            $relative = Get-DependencyRelativePath -RepositoryRoot $RepositoryRoot -ExternalRoot $BuildRoot -Path $rooted
            if ($relative -ne '') {
                [void]$dependency[$scanItem.Module].Add($relative)
            }
        }
    }

    $sorted = @{}
    foreach ($name in $Module) {
        $sorted[$name] = @($dependency[$name] | Sort-Object)
    }

    return $sorted
}

# 纯判定：返回“模块 <- 路径”形式的未覆盖项。自身 Tests/<模块>/ 树豁免。
function Get-UncoveredHostModulePath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][hashtable]$Rules,
        [Parameter(Mandatory)][hashtable]$Dependency
    )

    $ruleByName = @{}
    foreach ($moduleRule in $Rules.HostModuleRules) {
        $ruleByName[[string]$moduleRule.Name] = $moduleRule
    }

    $uncovered = New-Object System.Collections.Generic.List[string]
    foreach ($name in @($Rules.AllHostModules | Sort-Object)) {
        if (-not $ruleByName.ContainsKey([string]$name)) {
            throw "AllHostModules 中的模块 '$name' 在 HostModuleRules 里没有规则。"
        }

        $ownTree = '^Tests/' + [regex]::Escape([string]$name) + '/'
        $paths = @(@($Dependency[[string]$name]) |
            ForEach-Object { Get-HarnessNormalizedPath -RelativePath $_ } |
            Sort-Object -Unique)
        foreach ($path in $paths) {
            if ($path -match $ownTree) {
                continue
            }

            if (Test-HarnessPathMatchesAny -RelativePath $path -Pattern $ruleByName[[string]$name].PathPatterns) {
                continue
            }

            [void]$uncovered.Add("$name <- $path")
        }
    }

    return $uncovered.ToArray()
}

function Invoke-TestPathsCheck {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [hashtable]$Rules = (Get-HarnessRules)
    )

    $modules = @($Rules.AllHostModules | ForEach-Object { [string]$_ })
    $buildRoot = Join-Path -Path ([System.IO.Path]::GetTempPath()) `
        -ChildPath ('mp3-testpaths-' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
        $dependency = Get-HostModuleDependency -RepositoryRoot $RepositoryRoot -Module $modules -BuildRoot $buildRoot
    }
    finally {
        if (Test-Path -LiteralPath $buildRoot) {
            $resolvedBuild = (Resolve-Path -LiteralPath $buildRoot).Path
            $resolvedTemp = (Resolve-Path -LiteralPath ([System.IO.Path]::GetTempPath())).Path.TrimEnd('\')
            if (-not $resolvedBuild.StartsWith($resolvedTemp + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "拒绝清理临时目录之外的路径：$resolvedBuild"
            }

            Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
        }
    }

    $uncovered = @(Get-UncoveredHostModulePath -Rules $Rules -Dependency $dependency)
    if ($uncovered.Count -gt 0) {
        $header = "主机测试路径检查失败：以下 $($uncovered.Count) 个依赖被模块实际编译，却不在其 PathPatterns 内（改动它们不会选中该模块）："
        $footer = '请在 scripts/rules/verification.psd1 对应 HostModuleRules 条目补 PathPatterns，并在 harness_helpers.Tests.ps1 加路由断言。'
        throw ($header + [Environment]::NewLine + ($uncovered -join [Environment]::NewLine) + [Environment]::NewLine + $footer)
    }

    $dependencyCount = 0
    foreach ($name in $modules) {
        $dependencyCount += @($dependency[$name]).Count
    }

    return [pscustomobject]@{
        ModuleCount = $modules.Count
        DependencyCount = $dependencyCount
    }
}

$isDotSourced = $MyInvocation.InvocationName -eq '.'
if (-not $isDotSourced) {
    Complete-Utf8EntryScript -Action {
        $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
        $summary = Invoke-TestPathsCheck -RepositoryRoot $repositoryRoot
        Write-NativeUtf8Line -Text "主机测试路径检查通过：$($summary.ModuleCount) 个模块，共 $($summary.DependencyCount) 条依赖均被 PathPatterns 覆盖。"
    }
}
