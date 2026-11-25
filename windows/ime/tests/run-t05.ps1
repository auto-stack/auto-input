# AutoIME personal English terms - full chain (PLAN-078 T-05, V-05).
#
# Seeds an isolated user data dir through the installed command
# (autoime-config terms add), then replays the shared/user-terms.json corpus
# through the real librime engine. This covers the whole chain:
#   manager CLI -> personal table -> deployment compile -> candidate recall,
#   AS -> AutoScape (personal), SQ -> AutoScape before the built-in SQLite,
#   as/As/A -> no AutoScape (case-sensitive, exact code only).
#
# Prerequisites (named, never silently skipped): manager/bin/autoime-config.exe
# (run manager/scripts/prepare.ps1), windows/ime/build/autoime_deploy.exe and
# windows/probe/build/rime_probe.exe (build those trees first).
#
# Usage: pwsh -NoProfile -File windows/ime/tests/run-t05.ps1 [-Keep]
param([switch]$Keep)

$ErrorActionPreference = 'Stop'

$ime = Split-Path -Parent $PSScriptRoot
$autoime = Split-Path -Parent (Split-Path -Parent $ime)
$manager = Join-Path $autoime 'manager'
$managerExe = Join-Path $manager 'bin/autoime-config.exe'
$deploy = Join-Path $ime 'build/autoime_deploy.exe'
$probe = Join-Path $autoime 'windows/probe/build/rime_probe.exe'
$resources = Join-Path $autoime 'shared/resources'
$cases = Join-Path $autoime 'shared/tests/user-terms.json'
foreach ($path in @($managerExe, $deploy, $probe, $cases,
                    (Join-Path $resources 'autoime_pinyin.schema.yaml'))) {
  if (-not (Test-Path $path)) { throw "[run-t05] preparation missing: $path" }
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ('autoime-t05-' + $PID + '-' + [DateTime]::UtcNow.Ticks)
$user = Join-Path $root 'rime'
New-Item -ItemType Directory -Force -Path $user | Out-Null

$env:AUTOIME_MANAGER_DIR = $manager
$env:AUTOIME_DEPLOY_EXE = $deploy
$env:AUTOIME_RESOURCES_DIR = $resources

$start = Get-Date
& $managerExe terms add --code AS --text AutoScape --user-data-dir $user
if ($LASTEXITCODE -ne 0) { throw "[run-t05] terms add AS failed ($LASTEXITCODE)" }
& $managerExe terms add --code SQ --text AutoScape --user-data-dir $user
if ($LASTEXITCODE -ne 0) { throw "[run-t05] terms add SQ failed ($LASTEXITCODE)" }

$added = ((Get-Date) - $start).TotalSeconds
$start = Get-Date
$output = & $probe --schema autoime_pinyin --resources-dir $resources --user-data-dir $user --cases $cases 2>&1
$code = $LASTEXITCODE
$elapsed = ((Get-Date) - $start).TotalSeconds
$output | Where-Object { $_ -match '^RESULT case-ut-|^RESULT case-en-|^SUMMARY' } | ForEach-Object { Write-Host $_ }
Write-Host ('[run-t05] manager_adds={0:F3}s probe={1:F3}s exit={2}' -f $added, $elapsed, $code)

if (-not $Keep) {
  Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
} else {
  Write-Host "[run-t05] kept $root"
}

if ($code -ne 0) { throw "[run-t05] corpus failed (exit $code)" }
$failed = @($output | Where-Object { $_ -match '^RESULT case-ut-.*=FAIL' })
if ($failed.Count -gt 0) { throw "[run-t05] $($failed.Count) personal-term case(s) failed" }
Write-Host '[run-t05] OK'
