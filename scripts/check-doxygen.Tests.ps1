Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# check-doxygen 行为自测：函数定义解析、Doxygen 缺项判定，以及在临时 Git 仓库里
# 验证“只报改动函数、存量不报”的范围控制（Index / WorkingTree / Range）。
. (Join-Path -Path $PSScriptRoot -ChildPath 'check-doxygen.ps1')

$inheritedGitEnvironment = Suspend-InheritedGitEnvironment
$git = Get-ExternalCommand -Name 'git'
$fixture = Join-Path -Path ([System.IO.Path]::GetTempPath()) `
    -ChildPath ('mp3-doxygen-tests-' + [Guid]::NewGuid().ToString('N'))

function Assert-DoxygenTrue {
    param([bool]$Condition, [Parameter(Mandatory)][string]$Message)
    if (-not $Condition) { throw "断言失败：$Message" }
}

# 纯文本用例：返回 Text 中所有函数定义的违规说明（每个函数一条，合格的不出现）。
function Get-TextProblem {
    param([Parameter(Mandatory)][string]$Text, [switch]$RequireDirection)
    $problems = New-Object System.Collections.Generic.List[string]
    foreach ($definition in @(Get-CFunctionDefinition -Text $Text)) {
        foreach ($reason in @(Get-DoxygenFunctionViolation -Function $definition -RequireDirection:$RequireDirection)) {
            [void]$problems.Add("$($definition.Name): $reason")
        }
    }
    return @($problems)
}

function Assert-TextClean {
    param([string]$Text, [string]$Label, [switch]$RequireDirection)
    $problems = @(Get-TextProblem -Text $Text -RequireDirection:$RequireDirection)
    Assert-DoxygenTrue ($problems.Count -eq 0) "$Label 应通过，实际：$($problems -join ' | ')"
}

function Assert-TextProblem {
    param([string]$Text, [string]$Label, [string]$Pattern, [switch]$RequireDirection)
    $problems = @(Get-TextProblem -Text $Text -RequireDirection:$RequireDirection)
    Assert-DoxygenTrue ($problems.Count -gt 0) "$Label 应报告问题"
    Assert-DoxygenTrue (($problems -join "`n") -match $Pattern) "$Label 应匹配 '$Pattern'，实际：$($problems -join ' | ')"
}

function Invoke-FixtureGit {
    param([Parameter(Mandatory)][string[]]$Arguments)
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $null = & $git -C $fixture -c core.autocrlf=false -c core.safecrlf=false @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($exitCode -ne 0) { throw "夹具 git 失败（$exitCode）：$($Arguments -join ' ')" }
}

function Write-FixtureFile {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Text, [switch]$Crlf)
    $target = Join-Path -Path $fixture -ChildPath $Path
    [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($target))
    $content = if ($Crlf) { $Text -replace "`r?`n", "`r`n" } else { $Text }
    [System.IO.File]::WriteAllText($target, $content, (New-Object System.Text.UTF8Encoding $false))
}

function Reset-Fixture {
    Invoke-FixtureGit -Arguments @('reset', '-q', '--hard')
    Invoke-FixtureGit -Arguments @('clean', '-fdq')
}

function Save-FixtureCommit {
    param([Parameter(Mandatory)][string]$Message)
    Invoke-FixtureGit -Arguments @('add', '-A')
    Invoke-FixtureGit -Arguments @(
        '-c', 'commit.gpgsign=false', '-c', 'user.email=doxygen-test@example.invalid',
        '-c', 'user.name=doxygen-test', 'commit', '-q', '-m', $Message
    )
}

function Get-FixtureViolation {
    param([ValidateSet('WorkingTree', 'Index', 'Range')][string]$Source = 'WorkingTree', [string]$BaseRef, [string]$HeadRef = 'HEAD')
    $parameters = @{ RepositoryRoot = $fixture }
    if ($Source -eq 'Range') { $parameters.BaseRef = $BaseRef; $parameters.HeadRef = $HeadRef }
    else { $parameters.Snapshot = $Source }
    return @((Get-DoxygenCheckResult @parameters).Violations)
}

function Assert-FixtureClean {
    param([string]$Label, [string]$Source = 'WorkingTree', [string]$BaseRef, [string]$HeadRef = 'HEAD')
    $violations = @(Get-FixtureViolation -Source $Source -BaseRef $BaseRef -HeadRef $HeadRef)
    Assert-DoxygenTrue ($violations.Count -eq 0) "$Label 应通过，实际：$($violations -join ' | ')"
}

function Assert-FixtureViolation {
    param([string]$Label, [string]$Pattern, [string]$Source = 'WorkingTree', [string]$BaseRef, [string]$HeadRef = 'HEAD')
    $violations = @(Get-FixtureViolation -Source $Source -BaseRef $BaseRef -HeadRef $HeadRef)
    Assert-DoxygenTrue ($violations.Count -gt 0) "$Label 应报告违规"
    Assert-DoxygenTrue (($violations -join "`n") -match $Pattern) "$Label 应匹配 '$Pattern'，实际：$($violations -join ' | ')"
}

$goodDirected = @'
/**
 * @brief 加法。
 * @param[in] a 左值。
 * @param[in] b 右值。
 * @return 两数之和。
 */
int add(int a, int b)
{
    return a + b;
}
'@
# 脚本文件是 CRLF，这里统一成 LF，便于下面用 `n 构造替换。
$goodDirected = $goodDirected -replace "`r`n", "`n"

try {
    # ---------- 解析与缺项判定（纯文本） ----------
    Assert-TextClean -Text $goodDirected -Label '完整带方向的文档' -RequireDirection

    Assert-TextProblem -Label '缺注释块' -Pattern 'add: 缺少' -Text @'
int add(int a, int b)
{
    return a + b;
}
'@

    Assert-TextProblem -Label '普通块注释不是 Doxygen' -Pattern 'add: 缺少' -Text @'
/* 加法 */
int add(int a, int b)
{
    return a + b;
}
'@

    Assert-TextProblem -Label '行注释不是 Doxygen' -Pattern 'add: 缺少' -Text @'
// 加法
int add(int a, int b)
{
    return a + b;
}
'@

    Assert-TextProblem -Label '缺 @brief' -Pattern '@brief' -Text @'
/**
 * @param[in] a 左值。
 * @param[in] b 右值。
 * @return 和。
 */
int add(int a, int b)
{
    return a + b;
}
'@

    Assert-TextProblem -Label '缺某个 @param' -Pattern '@param b' -Text @'
/**
 * @brief 加法。
 * @param[in] a 左值。
 * @return 和。
 */
int add(int a, int b)
{
    return a + b;
}
'@

    Assert-TextProblem -Label '非 void 缺 @return' -Pattern '@return' -Text @'
/**
 * @brief 加法。
 * @param[in] a 左值。
 * @param[in] b 右值。
 */
int add(int a, int b)
{
    return a + b;
}
'@

    Assert-TextClean -Label '只写 @retval 也算返回说明' -Text @'
/**
 * @brief 打开。
 * @retval 0 成功。
 * @retval -1 失败。
 */
int open_thing(void)
{
    return 0;
}
'@

    Assert-TextClean -Label 'void 函数无需 @return，无参无需 @param' -Text @'
/**
 * @brief 复位。
 */
void reset_thing(void)
{
}
'@

    Assert-TextProblem -Label 'void* 返回仍算非 void' -Pattern '@return' -Text @'
/**
 * @brief 取指针。
 */
void *get_ptr(void)
{
    return 0;
}
'@

    Assert-TextClean -Label '头文件 static inline 与声明' -Text @'
#ifndef X_H
#define X_H
#ifdef __cplusplus
extern "C" {
#endif

int declared_only(int a);
typedef struct { int v; } thing_t;

/**
 * @brief 取值。
 * @param[in] t 对象。
 * @return 值。
 */
static inline int thing_get(const thing_t *t)
{
    return t->v;
}

#ifdef __cplusplus
}
#endif
#endif
'@

    Assert-TextProblem -Label '头文件 static inline 缺文档' -Pattern 'thing_get: 缺少' -Text @'
#ifdef __cplusplus
extern "C" {
#endif
static inline int thing_get(const thing_t *t)
{
    return t->v;
}
#ifdef __cplusplus
}
#endif
'@

    Assert-TextClean -Label '声明不要求文档' -Text @'
int add(int a, int b);
extern void reset_thing(void);
static int helper(int a);
'@

    Assert-TextProblem -Label '文件头 @file 注释不能充当函数文档' -Pattern 'main_loop: 缺少' -Text @'
/**
 * @file loop.c
 * @brief 文件说明。
 */
void main_loop(void)
{
}
'@

    Assert-TextProblem -Label '文档与函数之间有空行视为未紧邻' -Pattern 'f: 缺少' -Text @'
/**
 * @brief 说明。
 */

void f(void)
{
}
'@

    Assert-TextClean -Label '文档与函数之间允许预处理行与属性' -Text @'
/**
 * @brief 弱符号。
 * @param[in] v 值。
 */
#if defined(USE_WEAK)
__attribute__((weak)) void weak_thing(int v)
{
    (void)v;
}
#endif
'@

    Assert-TextClean -Label '多行参数、函数指针参数与数组参数' -Text @'
/**
 * @brief 注册。
 * @param[in] name 名称。
 * @param[in] cb 回调。
 * @param[in] table 表。
 * @return 0 成功。
 */
static int register_thing(const char *name,
                          void (*cb)(int, void *),
                          int table[4])
{
    (void)name; (void)cb; (void)table;
    return 0;
}
'@

    Assert-TextProblem -Label '函数指针参数名也要有 @param' -Pattern '@param cb' -Text @'
/**
 * @brief 注册。
 * @param[in] name 名称。
 * @return 0 成功。
 */
static int register_thing(const char *name, void (*cb)(int))
{
    return 0;
}
'@

    Assert-TextClean -Label '注释和字符串里的大括号/分号不影响解析' -Text @'
/**
 * @brief 带干扰字符。
 * @return 0。
 */
int tricky(void)
{
    const char *s = "}{;/*";
    char c = '{';
    /* } */
    // }
    (void)s; (void)c;
    return 0;
}

/**
 * @brief 下一个。
 */
void after_tricky(void)
{
}
'@

    Assert-TextClean -Label '含中文说明与 CRLF' -Text ("/**`r`n * @brief 中文简介，带@符号测试。`r`n * @param[in] 值 不是标识符。`r`n * @param[in] v 值。`r`n */`r`nvoid cn(int v)`r`n{`r`n}`r`n")

    # 无法解析的写法必须静默跳过，而不是误报。
    Assert-TextClean -Label 'K&R、返回函数指针、宏包装不报' -Text @'
int old_style(a, b)
int a;
int b;
{
    return a + b;
}

void (*get_handler(int id))(int)
{
    return 0;
}

TEST_CASE(some_case)
{
}

static const int table[] = { 1, 2, 3 };
struct cfg { int a; } cfg_default = { 1 };
'@

    # 方向：新函数必须有方向，旧函数容忍。
    $undirected = @'
/**
 * @brief 加法。
 * @param a 左值。
 * @param b 右值。
 * @return 和。
 */
int add(int a, int b)
{
    return a + b;
}
'@
    Assert-TextClean -Label '旧无方向写法在不要求方向时通过' -Text $undirected
    Assert-TextProblem -Label '新函数无方向' -Pattern '@param a.*方向' -Text $undirected -RequireDirection
    Assert-TextClean -Label '[in,out] 与 [out] 都是合法方向' -RequireDirection -Text @'
/**
 * @brief 交换。
 * @param[in,out] a 左值。
 * @param[out] b 输出。
 */
void swap_out(int *a, int *b)
{
}
'@

    # 函数范围信息供范围控制使用
    $defs = @(Get-CFunctionDefinition -Text ($goodDirected + "`nvoid second(void)`n{`n}`n"))
    Assert-DoxygenTrue ($defs.Count -eq 2) '应解析出 2 个函数'
    Assert-DoxygenTrue ($defs[0].StartLine -eq 1 -and $defs[0].NameLine -eq 7 -and $defs[0].EndLine -eq 10) `
        "add 的行范围应为 1/7/10，实际 $($defs[0].StartLine)/$($defs[0].NameLine)/$($defs[0].EndLine)"

    # ---------- 路径排除 ----------
    foreach ($excluded in @(
            'Drivers/STM32H7xx_HAL_Driver/Src/a.c'
            'Middlewares/Third_Party/minimp3/minimp3.h'
            'Middlewares/ST/x/y.c'
            'Tests/flash_ftl/stubs/Middlewares/Third_Party/FreeRTOS/Source/include/task.h'
            'Tools/external_loader/third_party/cmsis/core_cm7.h'
            'build/gen/x.c'
            'cmake/stm32cubemx/x.c'
            'docs/readme.md'
            'Service/foo/foo.cpp'
        )) {
        Assert-DoxygenTrue (Test-DoxygenExcludedPath -RepositoryRoot $fixture -RelativePath $excluded) "$excluded 应被排除"
    }
    foreach ($included in @('Service/foo/foo.c', 'Components/x/x.h', 'Tests/playback_task/t.c', 'APP/tasks/a.c')) {
        Assert-DoxygenTrue (-not (Test-DoxygenExcludedPath -RepositoryRoot $fixture -RelativePath $included)) "$included 应被检查"
    }

    # ---------- 临时 Git 仓库：范围控制 ----------
    [void][System.IO.Directory]::CreateDirectory($fixture)
    Invoke-FixtureGit -Arguments @('init', '-q')

    $legacy = @'
/**
 * @brief 旧函数。
 * @param x 旧无方向写法。
 * @return 值。
 */
int legacy_documented(int x)
{
    return x;
}

int legacy_naked(int x)
{
    return x + 1;
}

int legacy_second(int y)
{
    return y;
}
'@
    $legacy = ($legacy -replace "`r`n", "`n") + "`n"
    Write-FixtureFile -Path 'src/legacy.c' -Text $legacy
    Write-FixtureFile -Path 'Drivers/gen.c' -Text "int gen_naked(int a)`n{`n    return a;`n}`n"
    Write-FixtureFile -Path 'Middlewares/Third_Party/x/vendor.c' -Text "int vendor_naked(int a)`n{`n    return a;`n}`n"
    Write-FixtureFile -Path 'Core/Src/cubemx.c' -Text "/* STMicroelectronics */`nint cube_naked(int a)`n{`n    /* USER CODE BEGIN 0 */`n    return a;`n    /* USER CODE END 0 */`n}`n"
    Write-FixtureFile -Path 'Core/Src/own.c' -Text "/**`n * @brief own。`n * @param[in] a a。`n * @return a。`n */`nint own_ok(int a)`n{`n    return a;`n}`n"
    Save-FixtureCommit -Message 'baseline'
    Invoke-FixtureGit -Arguments @('tag', 'baseline')
    $baselineRef = 'baseline'

    Assert-FixtureClean -Label '无改动'

    # 1. 新增完整文档的函数 -> 通过；存量裸函数不报
    Write-FixtureFile -Path 'src/new.c' -Text $goodDirected
    Assert-FixtureClean -Label '新增完整文档（未跟踪文件）'
    Reset-Fixture

    # 2. 新文件各类缺项 -> 失败
    Write-FixtureFile -Path 'src/new.c' -Text "int add(int a, int b)`n{`n    return a + b;`n}`n"
    Assert-FixtureViolation -Label '新文件缺注释块' -Pattern 'src/new\.c:\d+: .*add.*缺少'
    Reset-Fixture
    Write-FixtureFile -Path 'src/new.c' -Text ($goodDirected -replace '@brief', '@details')
    Assert-FixtureViolation -Label '新文件缺 @brief' -Pattern '@brief'
    Reset-Fixture
    Write-FixtureFile -Path 'src/new.c' -Text ($goodDirected -replace '(?m)^ \* @param\[in\] b.*\r?\n', '')
    Assert-FixtureViolation -Label '新文件缺 @param' -Pattern '@param b'
    Reset-Fixture
    Write-FixtureFile -Path 'src/new.c' -Text ($goodDirected -replace '(?m)^ \* @return.*\r?\n', '')
    Assert-FixtureViolation -Label '新文件缺 @return' -Pattern '@return'
    Reset-Fixture
    Write-FixtureFile -Path 'src/new.c' -Text ($goodDirected -replace '@param\[in\]', '@param')
    Assert-FixtureViolation -Label '新文件新函数缺方向' -Pattern '方向'
    Reset-Fixture

    # 3. 头文件 static inline
    Write-FixtureFile -Path 'src/new.h' -Text "static inline int twice(int a)`n{`n    return a * 2;`n}`nint declared_only(int a);`n"
    Assert-FixtureViolation -Label '头文件 static inline 缺文档' -Pattern 'src/new\.h:\d+: .*twice'
    Reset-Fixture

    # 4. 存量函数未触及 -> 不报；改动文档良好的旧函数 -> 容忍旧无方向
    Write-FixtureFile -Path 'src/legacy.c' -Text ($legacy -replace 'return x;', 'return x + 100;')
    Assert-FixtureClean -Label '改动文档良好的旧函数（无方向）'
    Reset-Fixture
    Write-FixtureFile -Path 'src/legacy.c' -Text ($legacy + "`n" + $goodDirected.Replace('add', 'add_extra'))
    Assert-FixtureClean -Label '在文件尾追加完整新函数，裸旧函数不报'
    Reset-Fixture

    # 5. 改动裸旧函数 -> 只报这个，不报相邻裸函数
    Write-FixtureFile -Path 'src/legacy.c' -Text ($legacy -replace 'return x \+ 1;', 'return x + 2;')
    Assert-FixtureViolation -Label '改动裸旧函数' -Pattern 'legacy_naked'
    $violations = @(Get-FixtureViolation)
    Assert-DoxygenTrue (@($violations | Where-Object { $_ -match 'legacy_second' }).Count -eq 0) '相邻未触及函数不应被报告'
    Reset-Fixture

    # 6. 只删除行也算改动
    Write-FixtureFile -Path 'src/legacy.c' -Text ($legacy -replace "(?m)^    return x \+ 1;\r?\n", "    (void)x;`n")
    Assert-FixtureViolation -Label '函数体内改动（替换）' -Pattern 'legacy_naked'
    Reset-Fixture
    Write-FixtureFile -Path 'src/legacy.c' -Text ($legacy -replace "int legacy_naked\(int x\)`n\{`n    return x \+ 1;`n\}`n`n", "int legacy_naked(int x)`n{`n}`n`n")
    Assert-FixtureViolation -Label '函数体内纯删除' -Pattern 'legacy_naked'
    Reset-Fixture

    # 7. 旧函数改名/换参数仍属“改动”，不要求方向；新增同名不存在的函数要求方向
    Write-FixtureFile -Path 'src/legacy.c' -Text ($legacy -replace 'int legacy_documented\(int x\)', 'int legacy_documented(int x, int z)')
    Assert-FixtureViolation -Label '旧函数新增参数但没补 @param' -Pattern '@param z'
    Reset-Fixture

    # 8. 生成目录/第三方/CubeMX 写保护文件忽略
    Write-FixtureFile -Path 'Drivers/gen.c' -Text "int gen_naked(int a)`n{`n    return a + 1;`n}`n"
    Write-FixtureFile -Path 'Middlewares/Third_Party/x/vendor.c' -Text "int vendor_naked(int a)`n{`n    return a + 1;`n}`n"
    Write-FixtureFile -Path 'Core/Src/cubemx.c' -Text "/* STMicroelectronics */`nint cube_naked(int a)`n{`n    /* USER CODE BEGIN 0 */`n    return a + 1;`n    /* USER CODE END 0 */`n}`n"
    Write-FixtureFile -Path 'build/out.c' -Text "int built(int a)`n{`n    return a;`n}`n"
    Write-FixtureFile -Path 'Tools/x/third_party/lib.c' -Text "int lib_naked(int a)`n{`n    return a;`n}`n"
    Assert-FixtureClean -Label '生成/第三方/CubeMX/build 路径'
    # 自维护的 Core 新文件不在豁免内
    Write-FixtureFile -Path 'Core/Src/own2.c' -Text "int own_naked(int a)`n{`n    return a;`n}`n"
    Assert-FixtureViolation -Label 'Core 下自维护新文件仍检查' -Pattern 'own2\.c'
    Reset-Fixture

    # 9. 索引与工作树不一致
    Write-FixtureFile -Path 'src/new.c' -Text "int add(int a, int b)`n{`n    return a + b;`n}`n"
    Invoke-FixtureGit -Arguments @('add', 'src/new.c')
    Write-FixtureFile -Path 'src/new.c' -Text $goodDirected
    Assert-FixtureViolation -Label '索引里是坏版本' -Pattern 'src/new\.c' -Source 'Index'
    Assert-FixtureClean -Label '工作树已修好' -Source 'WorkingTree'
    Invoke-FixtureGit -Arguments @('add', 'src/new.c')
    Assert-FixtureClean -Label '索引也修好后' -Source 'Index'
    Reset-Fixture

    Write-FixtureFile -Path 'src/new.c' -Text $goodDirected
    Invoke-FixtureGit -Arguments @('add', 'src/new.c')
    Write-FixtureFile -Path 'src/new.c' -Text "int add(int a, int b)`n{`n    return a + b;`n}`n"
    Assert-FixtureClean -Label '索引是好版本、工作树变坏时 Index 通过' -Source 'Index'
    Assert-FixtureViolation -Label '工作树坏版本' -Pattern 'src/new\.c' -Source 'WorkingTree'
    Reset-Fixture

    # 10. CRLF 文件
    Write-FixtureFile -Path 'src/crlf.c' -Text $goodDirected -Crlf
    Assert-FixtureClean -Label 'CRLF 完整文档'
    Write-FixtureFile -Path 'src/crlf.c' -Text "int add(int a, int b)`n{`n    return a + b;`n}`n" -Crlf
    Assert-FixtureViolation -Label 'CRLF 缺文档' -Pattern 'crlf\.c'
    Reset-Fixture

    # 11. 范围模式（CHANGED/推送）：BaseRef..HeadRef
    Write-FixtureFile -Path 'src/new.c' -Text "int add(int a, int b)`n{`n    return a + b;`n}`n"
    Save-FixtureCommit -Message 'bad'
    Assert-FixtureViolation -Label '范围内引入坏函数' -Pattern 'src/new\.c' -Source 'Range' -BaseRef $baselineRef
    Assert-FixtureClean -Label '工作树干净时 WorkingTree 不报已提交的坏函数'
    Write-FixtureFile -Path 'src/new.c' -Text $goodDirected
    Save-FixtureCommit -Message 'fix'
    Assert-FixtureClean -Label '范围终点已修好' -Source 'Range' -BaseRef $baselineRef
    Assert-FixtureViolation -Label '只看到修复前一段' -Pattern 'src/new\.c' -Source 'Range' -BaseRef $baselineRef -HeadRef 'HEAD~1'

    # 12. 存量报告：只计数，不抛出
    $inventory = Get-DoxygenReport -RepositoryRoot $fixture
    Assert-DoxygenTrue ($inventory.FunctionCount -ge 5) "报告应统计全部手写函数，实际 $($inventory.FunctionCount)"
    Assert-DoxygenTrue ($inventory.ViolationCount -ge 2) "报告应统计存量违规，实际 $($inventory.ViolationCount)"
    Assert-DoxygenTrue ($inventory.ViolationCount -lt $inventory.FunctionCount) '合格函数不计入违规'

    # 13. Invoke-DoxygenCheck：违规时抛出带 STY-2 与文件行号的文本
    Write-FixtureFile -Path 'src/bad.c' -Text "int bad(void)`n{`n    return 0;`n}`n"
    $failure = $null
    try { Invoke-DoxygenCheck -RepositoryRoot $fixture -Snapshot WorkingTree } catch { $failure = $_.Exception.Message }
    Assert-DoxygenTrue ($null -ne $failure -and $failure -match 'STY-2' -and $failure -match 'src/bad\.c:1') "失败信息应指向 STY-2 与 src/bad.c:1，实际：$failure"
    Reset-Fixture
}
finally {
    $resolved = [System.IO.Path]::GetFullPath($fixture)
    $temp = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\')
    if (-not $resolved.StartsWith($temp + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "拒绝清理临时目录外路径：$resolved"
    }
    if (Test-Path -LiteralPath $resolved) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    Restore-InheritedGitEnvironment -Saved $inheritedGitEnvironment
}

Write-Output 'check-doxygen 行为测试通过。'
