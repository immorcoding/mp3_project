Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build_helpers.ps1')
$savedGit = Suspend-InheritedGitEnvironment
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('evidence-fixture-' + [Guid]::NewGuid().ToString('N'))
$utf8 = New-Object Text.UTF8Encoding($true)
function Assert-Evidence {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}
try {
    [void](New-Item -ItemType Directory -Path (Join-Path $fixture 'scripts') -Force)
    [void](New-Item -ItemType Directory -Path (Join-Path $fixture 'Tools/gui_simulator') -Force)
    Copy-Item (Join-Path $PSScriptRoot 'capture-verification.ps1') (Join-Path $fixture 'scripts')
    [IO.File]::WriteAllText((Join-Path $fixture '.gitignore'), "build/`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $fixture 'tracked.txt'), 'seed', $utf8)
    & git -C $fixture init --quiet
    & git -C $fixture add .
    & git -C $fixture -c user.name=EvidenceTest -c user.email=evidence@example.invalid commit -qm seed
    Assert-Evidence ($LASTEXITCODE -eq 0) 'fixture commit failed'
    $commit = (& git -C $fixture rev-parse HEAD).Trim()
    $probe = @'
param([string]$HostCompiler, [string]$SdlSourceDir)
[Console]::WriteLine("console 中文")
[Console]::Error.WriteLine("stderr 中文")
Write-Output "pipeline output"
Write-Host "host output"
& $env:ComSpec /d /c "echo native output"
Write-Output "compiler=$HostCompiler;sdl=$SdlSourceDir"
exit 7
'@
    [IO.File]::WriteAllText((Join-Path $fixture 'scripts/verify.ps1'), $probe, $utf8)
    [IO.File]::WriteAllText((Join-Path $fixture 'Tools/gui_simulator/run-scenarios.ps1'), ($probe -replace 'exit 7', 'exit 0'), $utf8)
    $runner = Join-Path $fixture 'scripts/capture-verification.ps1'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $runner -Mode FULL -HostCompiler "compiler path's gcc.exe"
    Assert-Evidence ($LASTEXITCODE -eq 7) 'child nonzero must propagate unchanged'
    $runs = @(Get-ChildItem (Join-Path $fixture 'build/evidence') -Directory)
    Assert-Evidence ($runs.Count -eq 1) 'one isolated run directory expected'
    $run = $runs[0].FullName
    $meta = Get-Content (Join-Path $run 'metadata.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-Evidence ($meta.commit -eq $commit -and $meta.exitCode -eq 7 -and $meta.mode -eq 'FULL') 'metadata commit/exit/mode mismatch'
    Assert-Evidence ($meta.dirtyBefore -and $meta.dirtyAfter) 'dirty fixture must be recorded'
    Assert-Evidence ($meta.command.Contains('verify.ps1') -and $meta.parameters.HostCompiler -eq "compiler path's gcc.exe") 'command/parameters missing'
    $stdout = Get-Content (Join-Path $run 'stdout.log') -Raw -Encoding UTF8
    $stderr = Get-Content (Join-Path $run 'stderr.log') -Raw -Encoding UTF8
    foreach ($needle in @('console 中文', 'pipeline output', 'host output', 'native output', "compiler path's gcc.exe")) {
        Assert-Evidence ($stdout.Contains($needle)) "stdout missing: $needle"
    }
    Assert-Evidence ($stderr.Contains('stderr 中文')) 'Console stderr missing'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $runner -Mode FULL
    Assert-Evidence ($LASTEXITCODE -eq 7) 'second failure must still propagate'
    Assert-Evidence (@(Get-ChildItem (Join-Path $fixture 'build/evidence') -Directory).Count -eq 2) 'runs must not overwrite'
    Assert-Evidence ((Get-Content (Join-Path $run 'stdout.log') -Raw -Encoding UTF8) -eq $stdout) 'previous log overwritten'
    $custom = Join-Path $fixture "build/custom path's evidence"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $runner -Mode GUI -OutputDirectory $custom -SdlSourceDir "SDL path's source"
    Assert-Evidence ($LASTEXITCODE -eq 0) 'GUI success must pass'
    $guiRun = @(Get-ChildItem $custom -Directory)[0].FullName
    $gui = Get-Content (Join-Path $guiRun 'metadata.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-Evidence ($gui.mode -eq 'GUI' -and $gui.exitCode -eq 0) 'GUI metadata incorrect'
    Assert-Evidence ((Get-Content (Join-Path $guiRun 'stdout.log') -Raw -Encoding UTF8).Contains("SDL path's source")) 'GUI argument quoting failed'
    # 干净快照和 PowerShell 终止错误均需有明确证据，不能被包装吞掉。
    [IO.File]::WriteAllText((Join-Path $fixture 'scripts/verify.ps1'), "throw 'fixture failure'", $utf8)
    & git -C $fixture add .
    & git -C $fixture -c user.name=EvidenceTest -c user.email=evidence@example.invalid commit -qm probe
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $runner -Mode FULL
    Assert-Evidence ($LASTEXITCODE -eq 1) 'terminating error must fail'
    $failed = @(Get-ChildItem (Join-Path $fixture 'build/evidence') -Directory | Sort-Object Name)[-1].FullName
    $failureMeta = Get-Content (Join-Path $failed 'metadata.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-Evidence (-not $failureMeta.dirtyBefore -and -not $failureMeta.dirtyAfter) 'ignored evidence must preserve clean status'
    Assert-Evidence ((Get-Content (Join-Path $failed 'stderr.log') -Raw -Encoding UTF8).Contains('fixture failure')) 'terminating error text lost'
    $headFile = Join-Path $fixture '.git/HEAD'
    $savedHead = [IO.File]::ReadAllText($headFile)
    [IO.File]::WriteAllText((Join-Path $fixture 'scripts/verify.ps1'), "[IO.File]::WriteAllText((Join-Path `$PSScriptRoot '../.git/HEAD'), 'invalid'); exit 0", $utf8)
    try {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $runner -Mode FULL
        Assert-Evidence ($LASTEXITCODE -eq 1) 'post-run Git failure must fail capture'
        $gitFailed = @(Get-ChildItem (Join-Path $fixture 'build/evidence') -Directory | Sort-Object Name)[-1].FullName
        $gitMeta = Get-Content (Join-Path $gitFailed 'metadata.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        Assert-Evidence ($gitMeta.exitCode -eq 1 -and $gitMeta.childExitCode -eq 0) 'post-run failure manifest must match wrapper exit'
        Assert-Evidence ($null -eq $gitMeta.commitAfter -and $gitMeta.snapshotStatus -eq 'git-unavailable') 'unavailable snapshot must be explicit'
    }
    finally { [IO.File]::WriteAllText($headFile, $savedHead, (New-Object Text.UTF8Encoding($false))) }
    $moveHead = @'
& git -c user.name=EvidenceTest -c user.email=evidence@example.invalid commit --allow-empty -qm changed-head
exit $LASTEXITCODE
'@
    [IO.File]::WriteAllText((Join-Path $fixture 'scripts/verify.ps1'), $moveHead, $utf8)
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $runner -Mode FULL
    Assert-Evidence ($LASTEXITCODE -eq 1) 'HEAD change must prevent single-commit evidence'
    $changed = @(Get-ChildItem (Join-Path $fixture 'build/evidence') -Directory | Sort-Object Name)[-1].FullName
    $changedMeta = Get-Content (Join-Path $changed 'metadata.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-Evidence ($changedMeta.commitAfter -ne $changedMeta.commit -and $changedMeta.snapshotStatus -eq 'head-changed') 'both commits and changed snapshot required'
    Assert-Evidence ($changedMeta.exitCode -eq 1 -and $changedMeta.childExitCode -eq 0) 'HEAD change manifest must match wrapper exit'
    Write-Output '验证证据捕获测试通过。'
}
finally {
    Restore-InheritedGitEnvironment -Saved $savedGit
    $resolved = [IO.Path]::GetFullPath($fixture)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'fixture outside temp root' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
