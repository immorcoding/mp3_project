Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'agent_hook_helpers.ps1')
. (Join-Path -Path (Split-Path -Parent $PSScriptRoot) -ChildPath 'check-commit-msg.ps1')

function Assert-HookEqual {
    param(
        [Parameter(Mandatory)][AllowEmptyString()] $Expected,
        [AllowNull()] $Actual,
        [Parameter(Mandatory)][string]$Message
    )

    if ([string]$Expected -ne [string]$Actual) {
        throw "$Message 期望 '$Expected'，实际 '$Actual'。"
    }
}

$repositoryRoot = Get-AgentHookRepositoryRoot

# 路径提取：Claude 的 file_path / notebook_path 与 Codex apply_patch 补丁头。
$claudePaths = @(Get-AgentHookEditPath -ToolInput ([pscustomobject]@{ file_path = 'E:/repo/GUI/ui.c' }))
Assert-HookEqual -Expected 'E:/repo/GUI/ui.c' -Actual ($claudePaths -join ',') -Message 'Claude file_path 应被提取'

$patch = @(
    '*** Begin Patch'
    '*** Update File: Service/log/log_service.c'
    '@@'
    '-a'
    '+b'
    '*** Add File: GUI/new.c'
    '+x'
    '*** Delete File: Core/Src/old.c'
    '*** Update File: Platform/a.c'
    '*** Move to: Platform/b.c'
    '*** End Patch'
) -join "`n"
$codexPaths = @(Get-AgentHookEditPath -ToolInput ([pscustomobject]@{ command = $patch }))
Assert-HookEqual -Expected 'Service/log/log_service.c,GUI/new.c,Core/Src/old.c,Platform/a.c,Platform/b.c' `
    -Actual ($codexPaths -join ',') -Message 'apply_patch 的增删改与移动目标都应被提取'

# 相对化：仓库内绝对路径转相对，仓库外返回 $null。
$inside = ConvertTo-AgentHookRelativePath -RepositoryRoot 'E:\Projects\x' -Path 'E:\Projects\x\Service\a.c'
Assert-HookEqual -Expected 'Service/a.c' -Actual $inside -Message '仓库内绝对路径应转相对'
$outside = ConvertTo-AgentHookRelativePath -RepositoryRoot 'E:\Projects\x' -Path 'E:\Projects\xy\Service\a.c'
Assert-HookEqual -Expected '' -Actual $outside -Message '同前缀的仓库外路径不得误判为仓库内'
$dotDot = ConvertTo-AgentHookRelativePath -RepositoryRoot 'E:\Projects\x' -Path 'E:\Projects\x\APP\..\GUI\ui.c'
Assert-HookEqual -Expected 'GUI/ui.c' -Actual $dotDot -Message '绝对路径中的 .. 应先规范化'
$relativeDotDot = ConvertTo-AgentHookRelativePath -RepositoryRoot 'E:\Projects\x' -Path 'APP/../GUI/ui.c'
Assert-HookEqual -Expected 'GUI/ui.c' -Actual $relativeDotDot -Message '相对路径中的 .. 应先规范化'
$fromSubdirectory = ConvertTo-AgentHookRelativePath -RepositoryRoot 'E:\Projects\x' -Path '../GUI/x.c' -BaseDirectory 'E:\Projects\x\Service'
Assert-HookEqual -Expected 'GUI/x.c' -Actual $fromSubdirectory -Message '相对路径应以 Hook 输入的 cwd 为基准'
$gitBash = ConvertTo-AgentHookRelativePath -RepositoryRoot 'E:\Projects\x' -Path '/e/Projects/x/GUI/ui.c'
Assert-HookEqual -Expected 'GUI/ui.c' -Actual $gitBash -Message 'Git Bash 形式的绝对路径应识别为仓库内'

# 编辑决策：Claude 可 ask；Codex 的 ask 会放行，须降为 deny。
$cases = @(
    @{ Path = 'Middlewares/Third_Party/LVGL/src/core/lv_obj.c'; Tool = 'Edit'; Expected = 'deny' }
    @{ Path = 'Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c'; Tool = 'Write'; Expected = 'deny' }
    @{ Path = 'Drivers/CMSIS/Include/core_cm7.h'; Tool = 'Edit'; Expected = 'deny' }
    @{ Path = 'io_sheet.ioc'; Tool = 'Edit'; Expected = 'deny' }
    @{ Path = 'stm32h743zgtx_flash.ld'; Tool = 'apply_patch'; Expected = 'deny' }
    @{ Path = 'startup_stm32h743xx.s'; Tool = 'Edit'; Expected = 'deny' }
    @{ Path = 'Middlewares/Third_Party/FreeRTOS/Config/FreeRTOSConfig.h'; Tool = 'Edit'; Expected = 'ask' }
    @{ Path = 'Middlewares/Third_Party/FreeRTOS/Config/FreeRTOSConfig.h'; Tool = 'apply_patch'; Expected = 'deny' }
    @{ Path = 'Middlewares/Third_Party/LVGL/lv_conf.h'; Tool = 'Edit'; Expected = 'ask' }
    @{ Path = 'FATFS/Target/ffconf.h'; Tool = 'Edit'; Expected = 'ask' }
    @{ Path = 'Core/Inc/stm32h7xx_hal_conf.h'; Tool = 'Edit'; Expected = 'ask' }
    @{ Path = 'Tools/external_loader/include/stm32h7xx_hal_conf.h'; Tool = 'apply_patch'; Expected = 'allow' }
    @{ Path = 'Core/Src/system_stm32h7xx.c'; Tool = 'Edit'; Expected = 'deny' }
    @{ Path = 'Core/Src/syscalls.c'; Tool = 'Edit'; Expected = 'deny' }
    @{ Path = 'Middlewares/Third_Party/FatFs/src/ff.c'; Tool = 'Edit'; Expected = 'deny' }
    @{ Path = 'Core/Src/main.c'; Tool = 'Edit'; Expected = 'ask' }
    @{ Path = 'Core/Src/main.c'; Tool = 'apply_patch'; Expected = 'deny' }
    @{ Path = 'Core/Src/brand_new_file.c'; Tool = 'Write'; Expected = 'allow' }
    @{ Path = 'FATFS/Target/bsp_driver_user_diskio.c'; Tool = 'Edit'; Expected = 'allow' }
    @{ Path = 'Service/gui/gui_service.c'; Tool = 'apply_patch'; Expected = 'allow' }
    @{ Path = 'Service/gui/view/gui_service_view_music.c'; Tool = 'Edit'; Expected = 'allow' }
)
foreach ($case in $cases) {
    $decision = Get-AgentHookEditDecision -RepositoryRoot $repositoryRoot -RelativePath $case.Path -ToolName $case.Tool
    Assert-HookEqual -Expected $case.Expected -Actual $decision.Decision -Message "$($case.Tool) 写 $($case.Path) 的决策"
}

# shape 写入分支：领域文件只在写入分支改，其他分支写 inbox；分离 HEAD 不判断。
$shapeCases = @(
    @{ Path = 'docs/shape/git.md'; Branch = 'feature/x'; Expected = 'deny' }
    @{ Path = 'docs/shape/git.md'; Branch = 'main'; Expected = 'allow' }
    @{ Path = 'docs/shape/git.md'; Branch = ''; Expected = 'allow' }
    @{ Path = 'docs/shape/inbox/feature-x.md'; Branch = 'feature/x'; Expected = 'allow' }
    @{ Path = 'docs/verification.md'; Branch = 'feature/x'; Expected = 'allow' }
)
foreach ($case in $shapeCases) {
    foreach ($tool in @('Edit', 'apply_patch')) {
        $decision = Get-AgentHookEditDecision -RepositoryRoot $repositoryRoot -RelativePath $case.Path -ToolName $tool `
            -CurrentBranch $case.Branch -WriterBranch 'main'
        Assert-HookEqual -Expected $case.Expected -Actual $decision.Decision -Message "$tool 在分支 '$($case.Branch)' 写 $($case.Path) 的决策"
    }
}
$shapeDecision = Get-AgentHookEditDecision -RepositoryRoot $repositoryRoot -RelativePath 'docs/shape/git.md' -ToolName 'Edit' `
    -CurrentBranch 'feature/x' -WriterBranch 'main'
if ($shapeDecision.Reason -notmatch 'docs/shape/inbox/feature-x\.md') {
    throw "shape 拒绝原因应指出 inbox，实际：$($shapeDecision.Reason)"
}

# Shell：拦截跳过 Hook、强推、生成目录豁免变量与改写 hooksPath；放行日常命令。
$blocked = @(
    'git commit --no-verify -m "x"'
    'git commit -nm "x"'
    'git push --force origin main'
    'git push -f'
    'git push --force-with-lease=main:abc origin main'
    'git push origin +main'
    'git push --force-with-lease origin my-feature'
    'git -C E:/repo push -f origin main'
    'git -c push.default=current push --force'
    'cd repo && git push --force-if-includes origin main'
    'git push origin main --force'
    'git push --forc origin main'
    "git`tpush`t-f"
    'GIT push -F'
    'git --git-dir x push -f'
    'git --work-tree x push -f'
    'git --config-env x=y push -f'
    '/usr/bin/git push -f'
    'C:/Git/cmd/git.exe push -f'
    '.\git.exe push -f'
    'git -c alias.p=push p -f'
    'git commit "--no-verify" -m x'
    'git commit ''--no-verify'' -m x'
    'git commit --no-veri -m x'
    'git commit -m "a|b" --no-verify'
    '$env:ALLOW_GENERATED_UPDATE = ''1''; git commit -m x'
    'setx ALLOW_GENERATED_UPDATE 1'
    'Set-Item env:ALLOW_GENERATED_UPDATE 1'
    'export ALLOW_GENERATED_UPDATE=1'
    'git config core.hooksPath /tmp/none'
    'git config --unset core.hooksPath'
    'git config --global core.hooksPath /tmp/none'
    'git -c core.hooksPath=/tmp/none commit -m x'
)
foreach ($command in $blocked) {
    if ($null -eq (Get-AgentHookShellViolation -Command $command)) {
        throw "应拦截命令：$command"
    }
}
$allowed = @(
    'git status -sb'
    'git commit -m "feat(gui): 唱盘旋转"'
    'git commit --amend -m "fix(fs): 修复"'
    'git commit --amend --no-edit'
    'git commit -m "docs: 说明 -name 用法"'
    'git commit -m "fix: 去掉 -n 参数"'
    'git push --follow-tags origin main'
    'git push origin main'
    'git push -u origin v0.5.0-reshape'
    'git -C E:/repo push origin main'
    'git --git-dir x push origin main'
    'git --exec-path push origin main'
    'git push -- force'
    'powershell.exe -NoProfile -File scripts/verify_changed.ps1 -Push -RemoteName origin -RemoteLocation https://github.com/x/y.git'
    'git log --format=%H -- scripts/push-helper.ps1 -f'
    './scripts/verify.ps1'
    'git config --get core.hooksPath'
    'git config --local --get core.hooksPath'
    'git config get core.hooksPath'
    'git config --list --show-origin'
    'echo $env:ALLOW_GENERATED_UPDATE'
)
foreach ($command in $allowed) {
    $violation = Get-AgentHookShellViolation -Command $command
    if ($null -ne $violation) {
        throw "不应拦截命令：$command（$violation）"
    }
}

# 单文件分层检查：映射表与越层判定。
Assert-HookEqual -Expected 'service' -Actual (Get-LayerSourceKind -RelativePath 'Service\gui\gui_service.c') -Message 'Service 源文件映射'
Assert-HookEqual -Expected 'bridge' -Actual (Get-LayerSourceKind -RelativePath 'Adapters/bridge/x/y.h') -Message 'bridge 映射'
Assert-HookEqual -Expected '' -Actual (Get-LayerSourceKind -RelativePath 'Service/README.md') -Message '非 C 文件不检查'
Assert-HookEqual -Expected '' -Actual (Get-LayerSourceKind -RelativePath 'Core/Src/main.c') -Message '扫描根外不检查'

$layerFixtureRoot = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ('agent-hook-layer-' + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path (Join-Path $layerFixtureRoot 'Service/probe') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $layerFixtureRoot 'Service/probe/bad.c') -Value @('#include <stdint.h>', '#include "main.h"') -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $layerFixtureRoot 'Service/probe/good.c') -Value @('#include <stdint.h>', '#include "Platform/power/platform_power.h"') -Encoding ASCII

    $bad = @(Get-LayerFileIncludeViolation -RepositoryRoot $layerFixtureRoot -RelativePath 'Service/probe/bad.c')
    Assert-HookEqual -Expected 1 -Actual $bad.Count -Message 'Service 包含 main.h 应报 1 处越层'
    if ($bad[0] -notlike 'Service/probe/bad.c:2:*') {
        throw "越层描述应带路径与行号，实际：$($bad[0])"
    }
    $good = @(Get-LayerFileIncludeViolation -RepositoryRoot $layerFixtureRoot -RelativePath 'Service/probe/good.c')
    Assert-HookEqual -Expected 0 -Actual $good.Count -Message 'Service 经 Platform 进入硬件不应报越层'
    $missing = @(Get-LayerFileIncludeViolation -RepositoryRoot $layerFixtureRoot -RelativePath 'Service/probe/deleted.c')
    Assert-HookEqual -Expected 0 -Actual $missing.Count -Message '已删除文件不检查'
}
finally {
    if (Test-Path -LiteralPath $layerFixtureRoot) {
        Remove-Item -LiteralPath $layerFixtureRoot -Recurse -Force
    }
}

# 提交标题（GIT-2）。
foreach ($title in @('feat(gui): 唱盘旋转', 'fix: 修复', 'refactor(fs)!: Volume 化接口', 'docs(shape): 标准', 'Merge branch ''x''', 'Revert "feat: x"', 'fixup! feat: x')) {
    $violation = Get-CommitTitleViolation -Title $title
    if ($null -ne $violation) {
        throw "合规标题被拒：$title"
    }
}
foreach ($title in @('', '2026/9/30 12:00 旧格式', 'feat:缺空格', 'Feat(gui): 大写类型', 'feature(gui): 未知类型', 'feat(): 空 scope', 'feat(gui): ')) {
    if ($null -eq (Get-CommitTitleViolation -Title $title)) {
        throw "不合规标题被放行：'$title'"
    }
}

# 端到端：以子进程经 stdin 驱动入口脚本，核对退出码与 ask 输出。
$powershell = (Get-Command -Name 'powershell.exe' -CommandType Application | Select-Object -First 1).Source
$previousOutputEncoding = $OutputEncoding
$OutputEncoding = New-Object System.Text.UTF8Encoding($false)
try {
    function Invoke-GuardProcess {
        param([string]$Mode, [string]$Json)
        # 拒绝路径会写 stderr；PowerShell 5.1 在 Stop 下会把它升级为终止错误。
        $ErrorActionPreference = 'Continue'
        $global:LASTEXITCODE = 0
        $stdout = $Json | & $powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'guard.ps1') -Mode $Mode 2>$null
        return @{ ExitCode = $LASTEXITCODE; Stdout = ($stdout -join "`n") }
    }

    $vendorAbsolute = (Join-Path $repositoryRoot 'Drivers/CMSIS/Include/core_cm7.h') -replace '\\', '/'
    $result = Invoke-GuardProcess -Mode edit -Json (@{ tool_name = 'Edit'; tool_input = @{ file_path = $vendorAbsolute } } | ConvertTo-Json -Compress)
    Assert-HookEqual -Expected 2 -Actual $result.ExitCode -Message 'guard: Claude 写 Drivers/ 应以 2 拒绝'

    $result = Invoke-GuardProcess -Mode edit -Json (@{ tool_name = 'apply_patch'; tool_input = @{ command = "*** Begin Patch`n*** Update File: Core/Src/main.c`n*** End Patch" } } | ConvertTo-Json -Compress)
    Assert-HookEqual -Expected 2 -Actual $result.ExitCode -Message 'guard: Codex 改 USER CODE 文件应以 2 拒绝'

    $result = Invoke-GuardProcess -Mode edit -Json (@{ tool_name = 'Edit'; tool_input = @{ file_path = 'Core/Src/main.c' } } | ConvertTo-Json -Compress)
    Assert-HookEqual -Expected 0 -Actual $result.ExitCode -Message 'guard: Claude 改 USER CODE 文件走 ask，不以退出码拒绝'
    if ($result.Stdout -notmatch '"permissionDecision":\s*"ask"') {
        throw "guard: Claude 改 USER CODE 文件应输出 ask，实际：$($result.Stdout)"
    }

    $result = Invoke-GuardProcess -Mode edit -Json (@{ tool_name = 'Edit'; tool_input = @{ file_path = 'Service/gui/gui_service.c' } } | ConvertTo-Json -Compress)
    Assert-HookEqual -Expected 0 -Actual $result.ExitCode -Message 'guard: 自维护源码放行'
    Assert-HookEqual -Expected '' -Actual $result.Stdout.Trim() -Message 'guard: 放行时不输出决策'

    $result = Invoke-GuardProcess -Mode shell -Json (@{ tool_name = 'Bash'; tool_input = @{ command = 'git push --force origin main' } } | ConvertTo-Json -Compress)
    Assert-HookEqual -Expected 2 -Actual $result.ExitCode -Message 'guard: 强推应以 2 拒绝'

    $result = Invoke-GuardProcess -Mode shell -Json (@{ tool_name = 'Bash'; tool_input = @{ command = 'git status' } } | ConvertTo-Json -Compress)
    Assert-HookEqual -Expected 0 -Actual $result.ExitCode -Message 'guard: 日常命令放行'

    $messageFile = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ('commit-msg-' + [Guid]::NewGuid().ToString('N') + '.txt')
    $ErrorActionPreference = 'Continue'
    try {
        $commitMsgScript = Join-Path (Split-Path -Parent $PSScriptRoot) 'check-commit-msg.ps1'
        [System.IO.File]::WriteAllText($messageFile, "# 注释行`nfeat(harness): 统一 Agent Hook`n", (New-Object System.Text.UTF8Encoding($false)))
        & $powershell -NoProfile -ExecutionPolicy Bypass -File $commitMsgScript -MessagePath $messageFile 2>$null | Out-Null
        Assert-HookEqual -Expected 0 -Actual $LASTEXITCODE -Message 'commit-msg: 合规标题通过'

        [System.IO.File]::WriteAllText($messageFile, "2026/9/30 12:00 旧格式`n", (New-Object System.Text.UTF8Encoding($false)))
        & $powershell -NoProfile -ExecutionPolicy Bypass -File $commitMsgScript -MessagePath $messageFile 2>$null | Out-Null
        Assert-HookEqual -Expected 1 -Actual $LASTEXITCODE -Message 'commit-msg: 旧格式应失败'
    }
    finally {
        $ErrorActionPreference = 'Stop'
        Remove-Item -LiteralPath $messageFile -Force -ErrorAction SilentlyContinue
    }
}
finally {
    $OutputEncoding = $previousOutputEncoding
    $global:LASTEXITCODE = 0
}

Write-Output 'Agent Hook 与 commit-msg 测试通过。'
