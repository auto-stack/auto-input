# AutoIME manager test runner (PLAN-072 T-02).
#
# Explicit verification entry: it does NOT build. The launcher must already be
# prepared (manager/scripts/prepare.ps1); a missing binary is a named failure.
#
# Every case runs the real prepared launcher on the new ACE AV with an isolated
# user-data directory. Per-case wall time is printed and the run fails on any
# exit/status/stdout/stderr/file mismatch.
#
# Usage:
#   pwsh -NoProfile -File manager/scripts/test.ps1 [-Exe <path>] [-Case <id>] [-Keep]
#     [-DeployExe <autoime_deploy.exe>] [-Resources <resources dir>] [-RimeDll <rime.dll>]
param(
  [string]$Exe = '',
  [string]$Case = '',
  [string]$DeployExe = '',
  [string]$Resources = '',
  [string]$RimeDll = 'C:\Program Files\Rime\weasel-0.17.4\rime.dll',
  [switch]$Keep
)

$ErrorActionPreference = 'Stop'

$manager = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Exe) { $Exe = Join-Path $manager 'bin/autoime-config.exe' }
if (-not (Test-Path $Exe)) {
  throw "[test] preparation missing: $Exe (run manager/scripts/prepare.ps1 first)"
}
# Bind the launcher to the source tree under test (dev layout), so the
# suite can never silently exercise a different program dir.
$env:AUTOIME_MANAGER_DIR = $manager

# Mutating cases go through the native phrase transaction, which needs the
# real deploy tool + engine. These are named prerequisites, never a silently
# skipped branch: the manager has no non-AV apply path.
if (-not $DeployExe) { $DeployExe = Join-Path $manager '..\windows\ime\build\autoime_deploy.exe' }
if (-not (Test-Path $DeployExe)) { throw "[test] preparation missing: $DeployExe (build windows/ime first)" }
if (-not $Resources) { $Resources = Join-Path $manager '..\shared\resources' }
if (-not (Test-Path $Resources)) { throw "[test] preparation missing: $Resources" }
if (-not (Test-Path $RimeDll)) { throw "[test] preparation missing: $RimeDll" }
$shared = Join-Path $Resources 'data'
if (-not (Test-Path $shared)) {
  # Dev resources dir carries only the AutoIME schema; the shared Rime data
  # (luna/stroke dictionaries) comes from the installed Weasel bundle.
  $weasel = $env:AUTOIME_WEASEL_ROOT
  if (-not $weasel) { $weasel = 'C:\Program Files\Rime\weasel-0.17.4' }
  $shared = Join-Path $weasel 'data'
}
if (-not (Test-Path $shared)) { throw "[test] preparation missing: $shared (Rime shared data)" }
# N-05 repair: the runner pushes into each case dir, so every external path must
# be absolute before it reaches CreateProcess. Documented relative invocations
# (manager/README.md, windows/README.md) now reproduce the same results.
$Exe = (Resolve-Path $Exe).Path
$DeployExe = (Resolve-Path $DeployExe).Path
$Resources = (Resolve-Path $Resources).Path
$RimeDll = (Resolve-Path $RimeDll).Path
$shared = (Resolve-Path $shared).Path
$env:AUTOIME_DEPLOY_EXE = $DeployExe
$env:AUTOIME_RESOURCES_DIR = $Resources
$env:AUTOIME_RIME_DLL = $RimeDll
$env:AUTOIME_RIME_SHARED_DATA_DIR = $shared
$exeHash = (Get-FileHash $Exe -Algorithm SHA256).Hash

$casesPath = Join-Path $manager 'tests/phrases.cases.json'
if (-not (Test-Path $casesPath)) { throw "[test] case table missing: $casesPath" }
$casesHash = (Get-FileHash $casesPath -Algorithm SHA256).Hash
$table = Get-Content $casesPath -Raw -Encoding UTF8 | ConvertFrom-Json

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("autoime-manager-tests-" + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $scratch | Out-Null

Write-Host "[test] launcher: $Exe sha256=$exeHash"
Write-Host "[test] cases: $casesPath sha256=$casesHash"
Write-Host "[test] scratch: $scratch"

$utf8 = New-Object System.Text.UTF8Encoding($false)

function Write-CaseFile([string]$root, $file) {
  $path = Join-Path $root $file.path
  $dir = Split-Path -Parent $path
  if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
  if ($file.PSObject.Properties.Name -contains 'base64' -and $file.base64) {
    [System.IO.File]::WriteAllBytes($path, [System.Convert]::FromBase64String($file.base64))
  } elseif ($file.PSObject.Properties.Name -contains 'text') {
    [System.IO.File]::WriteAllText($path, [string]$file.text, $utf8)
  } else {
    throw "[test] case file entry without text/base64: $($file.path)"
  }
}

function Read-Expected([string]$root, $file) {
  $path = Join-Path $root $file.path
  if (-not (Test-Path $path)) { return $null }
  if ($file.PSObject.Properties.Name -contains 'base64' -and $file.base64) {
    return [System.Convert]::ToBase64String([System.IO.File]::ReadAllBytes($path))
  }
  return [System.IO.File]::ReadAllText($path, $utf8)
}

function Normalize-Lines($lines) {
  if ($null -eq $lines) { return '' }
  $arr = @($lines)
  if ($arr.Count -eq 0) { return '' }
  return (($arr -join "`n"))
}

# Warm-up: the first launcher start pays the OS/binary cold path. It is not a
# case and its time is reported separately so per-case numbers stay comparable.
$warmDir = Join-Path $scratch '_warmup'
New-Item -ItemType Directory -Force $warmDir | Out-Null
$warmStarted = Get-Date
$null = & $Exe phrases list --user-data-dir $warmDir 2> $null
$warmMs = [int]((Get-Date) - $warmStarted).TotalMilliseconds
Write-Host ("[test] warm-up: {0} ms (exit {1})" -f $warmMs, $LASTEXITCODE)
if ($LASTEXITCODE -ne 0) { throw "[test] warm-up failed: launcher cannot run the manager sources" }

$failures = @()
$results = @()
$selected = @($table.cases | Where-Object { (-not $Case) -or ($_.id -eq $Case) })
if ($selected.Count -eq 0) { throw "[test] no case matched '$Case'" }

foreach ($c in $selected) {
  $caseDir = Join-Path $scratch $c.id
  New-Item -ItemType Directory -Force $caseDir | Out-Null
  $dataDir = Join-Path $caseDir $table.user_data_dir
  New-Item -ItemType Directory -Force $dataDir | Out-Null
  # Isolation belt: if a command falls back to its default profile, it must
  # fall back into this case's directory, never the real user profile.
  $env:AUTOIME_RIME_USER_DATA_DIR = $dataDir

  foreach ($f in @($c.files)) { Write-CaseFile $caseDir $f }

  $started = Get-Date
  $caseFail = $null
  $stepIndex = 0
  Push-Location $caseDir
  try {
    foreach ($step in @($c.steps)) {
      $stepIndex++
      $stepArgs = @($step.args)
      if (-not ($step.PSObject.Properties.Name -contains 'no_user_dir_arg' -and $step.no_user_dir_arg)) {
        $stepArgs += @('--user-data-dir', $dataDir)
      }
      $tracked = Join-Path $dataDir 'custom_phrase.txt'
      $beforeHash = if (Test-Path $tracked) { (Get-FileHash $tracked -Algorithm SHA256).Hash } else { '' }
      $errFile = Join-Path $caseDir ".stderr-$stepIndex.txt"
      $stdout = & $Exe @stepArgs 2> $errFile
      $code = $LASTEXITCODE
      # Exit 7 is the contract's retryable "deployment busy" state (a host or a
      # concurrent deployment holds the machine-wide gate). Retry once before
      # judging the case; a second busy result is still a failure.
      if ($code -eq 7) {
        Start-Sleep -Milliseconds 1500
        $stdout = & $Exe @stepArgs 2> $errFile
        $code = $LASTEXITCODE
      }
      $stderr = if (Test-Path $errFile) { Get-Content $errFile -Raw -Encoding UTF8 } else { '' }
      if ($null -eq $stderr) { $stderr = '' }

      if ($code -ne [int]$step.exit) {
        $caseFail = "step $stepIndex exit $code != $($step.exit): $($stepArgs -join ' ')"
        break
      }
      if ($step.PSObject.Properties.Name -contains 'stdout') {
        $want = Normalize-Lines $step.stdout
        $got = Normalize-Lines $stdout
        if ($want -ne $got) {
          $caseFail = "step $stepIndex stdout mismatch`n  want=[$want]`n  got =[$got]"
          break
        }
      }
      if ($step.PSObject.Properties.Name -contains 'stderr_has') {
        foreach ($needle in @($step.stderr_has)) {
          if (-not $stderr.Contains($needle)) {
            $caseFail = "step $stepIndex stderr missing '$needle' (got: $($stderr.Trim()))"
            break
          }
        }
        if ($caseFail) { break }
      }
      if ($step.PSObject.Properties.Name -contains 'final_unchanged' -and $step.final_unchanged) {
        $afterHash = if (Test-Path $tracked) { (Get-FileHash $tracked -Algorithm SHA256).Hash } else { '' }
        if ($beforeHash -ne $afterHash) {
          $caseFail = "step $stepIndex was expected to be a no-op but rewrote the phrase file"
          break
        }
      }
    }

    if (-not $caseFail) {
      foreach ($f in @($c.final)) {
        if ($f.PSObject.Properties.Name -contains 'exists') {
          $present = Test-Path (Join-Path $caseDir $f.path)
          if ([bool]$f.exists -ne $present) {
            $caseFail = "final file exists=$([bool]$f.exists) mismatch: $($f.path)"
            break
          }
          continue
        }
        $got = Read-Expected $caseDir $f
        $want = $null
        if ($f.PSObject.Properties.Name -contains 'base64' -and $f.base64) { $want = [string]$f.base64 }
        else { $want = [string]$f.text }
        if ($null -eq $got) {
          $caseFail = "final file missing: $($f.path)"
          break
        }
        if ($got -ne $want) {
          $caseFail = "final file mismatch: $($f.path)`n  want=[$($want -replace "`n", '\n')]`n  got =[$($got -replace "`n", '\n')]"
          break
        }
      }
    }
  } finally {
    Pop-Location
    Remove-Item Env:AUTOIME_RIME_USER_DATA_DIR -ErrorAction SilentlyContinue
  }
  $elapsed = [int]((Get-Date) - $started).TotalMilliseconds
  $results += [pscustomobject]@{ id = $c.id; ms = $elapsed; ok = (-not $caseFail) }
  if ($caseFail) {
    Write-Host ("[test] FAIL {0} ({1} ms): {2}" -f $c.id, $elapsed, $caseFail)
    $failures += $c.id
  } else {
    Write-Host ("[test] PASS {0} ({1} ms)" -f $c.id, $elapsed)
  }
}

Write-Host ''
Write-Host ("[test] {0} cases, {1} passed, {2} failed" -f $results.Count, ($results.Count - $failures.Count), $failures.Count)
$results | ForEach-Object { Write-Host ("  {0,-45} {1,6} ms  {2}" -f $_.id, $_.ms, $(if ($_.ok) { 'ok' } else { 'FAIL' })) }

if (-not $Keep) {
  Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
} else {
  Write-Host "[test] kept scratch: $scratch"
}

if ($failures.Count -gt 0) { exit 1 }
exit 0
