# AutoIME phrase-transaction integration suite (PLAN-072 T-03).
#
# Exercises the NATIVE transaction surface (`autoime_deploy.exe phrases apply`)
# against a real librime install: source re-check/conflict, lock busy, crash
# recovery at both recovery-relevant points, rollback on a failed maintenance,
# and new-session candidate verification. Explicit preparation: the deploy tool
# and rime.dll/data must already exist; this script never builds.
#
# Prerequisites (named, not guessed):
#   -Exe        autoime_deploy.exe (windows/ime/build)
#   -Resources  AutoIME resources dir (shared/resources + data)
#   -RimeDll    rime.dll (defaults to the Weasel install)
#   -Scratch    isolated scratch root (default: temp)
#
# Usage:
#   pwsh -NoProfile -File manager/tests/txn.ps1 -Exe <deploy.exe> -Resources <dir>
param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Resources,
  [string]$RimeDll = 'C:\Program Files\Rime\weasel-0.17.4\rime.dll',
  [string]$Scratch = '',
  [string]$Case = '',
  [string]$ManagerExe = '',
  [string]$Probe = '',
  [string]$BaselineCases = '',
  [switch]$Keep
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Exe)) { throw "[txn] preparation missing: $Exe" }
if (-not (Test-Path $Resources)) { throw "[txn] preparation missing: $Resources" }
if (-not (Test-Path $RimeDll)) { throw "[txn] preparation missing: $RimeDll" }
$shared = Join-Path $Resources 'data'
if (-not (Test-Path $shared)) {
  # Dev resources dir carries only the AutoIME schema; the shared Rime data
  # (luna/stroke dictionaries) comes from the installed Weasel bundle.
  $weasel = $env:AUTOIME_WEASEL_ROOT
  if (-not $weasel) { $weasel = 'C:\Program Files\Rime\weasel-0.17.4' }
  $shared = Join-Path $weasel 'data'
}
if (-not (Test-Path $shared)) { throw "[txn] preparation missing: $shared (Rime shared data)" }

if (-not $Scratch) {
  $Scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("autoime-txn-" + [System.Guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force $Scratch | Out-Null

$env:AUTOIME_RIME_DLL = $RimeDll
$env:AUTOIME_RIME_SHARED_DATA_DIR = $shared
$env:AUTOIME_RESOURCES_DIR = $Resources
$utf8 = New-Object System.Text.UTF8Encoding($false)
$script:results = @()
$script:failures = @()

function New-UserDir([string]$name, [string]$phraseContent) {
  $dir = Join-Path $Scratch $name
  New-Item -ItemType Directory -Force $dir | Out-Null
  if ($null -ne $phraseContent) {
    [System.IO.File]::WriteAllText((Join-Path $dir 'custom_phrase.txt'), $phraseContent, $utf8)
  }
  return $dir
}

function Write-Request([string]$userDir, [string]$source, [bool]$sourceMissing,
                       [string]$staged, [string]$verify, [int]$timeoutMs) {
  $token = [guid]::NewGuid().ToString('N').Substring(0,8)
  $req = Join-Path $userDir (".autoime-apply-$token.req")
  $lines = @("user=$userDir")
  if ($sourceMissing) { $lines += 'source_missing=1' }
  else {
    $snap = Join-Path $userDir (".autoime-source-$token.txt")
    Copy-Item $source $snap -Force
    $lines += "source=$snap"
    $lines += 'source_missing=0'
  }
  $lines += "staged=$staged"
  if ($verify) { $lines += "verify=$verify" }
  $lines += "timeout_ms=$timeoutMs"
  [System.IO.File]::WriteAllText($req, (($lines -join "`n") + "`n"), $utf8)
  return $req
}

function Add-Case([string]$id, [scriptblock]$body) {
  if ($Case -and -not $id.StartsWith($Case)) { return }
  $started = Get-Date
  $problem = $null
  try { $problem = & $body } catch { $problem = "exception: $_" }
  $ms = [int]((Get-Date) - $started).TotalMilliseconds
  $script:results += [pscustomobject]@{ id = $id; ms = $ms; ok = (-not $problem) }
  if ($problem) {
    Write-Host ("[txn] FAIL {0} ({1} ms): {2}" -f $id, $ms, $problem)
    $script:failures += $id
  } else {
    Write-Host ("[txn] PASS {0} ({1} ms)" -f $id, $ms)
  }
}

# Shared preparation (not a timed case): one warm engine build whose compiled
# set already contains the phrase T3.8 removes.
$primeAbsent = Join-Path $Scratch '_prime-absent'
New-Item -ItemType Directory -Force $primeAbsent | Out-Null
[System.IO.File]::WriteAllText((Join-Path $primeAbsent 'custom_phrase.txt'),
  "原词`torig`t1`n移除词`tgone`t9`n", $utf8)
$primeStarted = Get-Date
& $Exe --user-data-dir $primeAbsent *> $null
if ($LASTEXITCODE -ne 0) { throw "[txn] preparation failed: cannot prime the engine (exit $LASTEXITCODE)" }
Write-Host ("[txn] preparation: primed warm user dir in {0} ms" -f [int]((Get-Date) - $primeStarted).TotalMilliseconds)

function Assert-Equal($want, $got, [string]$what) {
  if ($want -ne $got) { return "$what mismatch: want=[$want] got=[$got]" }
  return $null
}

# --- T-3.1 conflict: the data file changed after the manager read it ---------
Add-Case 'T3.1-stale-source-conflicts' {
  $user = New-UserDir 'stale' "高速缓存`tgaosuhuancun`t1`n"
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "高速缓存`tgaosuhuancun`t1`n新增`tzlby`t100`n", $utf8)
  $req = Write-Request $user $target $false $staged '' 3000
  # External edit between the read and the apply.
  Start-Sleep -Milliseconds 30
  [System.IO.File]::WriteAllText($target, "高速缓存`tgaosuhuancun`t1`n外部编辑`tother`t5`n", $utf8)
  $before = [System.IO.File]::ReadAllText($target, $utf8)
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  if ($code -ne 5) { return "expected conflict exit 5, got $code" }
  $after = [System.IO.File]::ReadAllText($target, $utf8)
  return (Assert-Equal $before $after 'data file (must keep the external edit)')
}

# --- T-3.2 conflict: a file that appeared after the manager saw "missing" ----
Add-Case 'T3.2-appeared-file-conflicts' {
  $user = New-UserDir 'appeared' $null
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "新增`tabc`t1`n", $utf8)
  $req = Write-Request $user $target $true $staged '' 3000
  [System.IO.File]::WriteAllText($target, "迟到的文件`tlate`t1`n", $utf8)
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  if ($code -ne 5) { return "expected conflict exit 5, got $code" }
  return (Assert-Equal "迟到的文件`tlate`t1`n" ([System.IO.File]::ReadAllText($target, $utf8)) 'data file')
}

# --- T-3.3 two writers: the first commits, the second is a conflict ----------
Add-Case 'T3.3-second-writer-conflicts' {
  $user = New-UserDir 'race' "原词`torig`t1`n"
  $target = Join-Path $user 'custom_phrase.txt'
  $stagedA = Join-Path $user 'stagedA.txt'
  $stagedB = Join-Path $user 'stagedB.txt'
  [System.IO.File]::WriteAllText($stagedA, "原词`torig`t1`n甲`taaa`t1`n", $utf8)
  [System.IO.File]::WriteAllText($stagedB, "原词`torig`t1`n乙`tbbb`t1`n", $utf8)
  # Both writers read the same source.
  $reqA = Write-Request $user $target $false $stagedA '' 10000
  $reqB = Write-Request $user $target $false $stagedB '' 10000
  & $Exe phrases apply --request $reqA *> $null
  $codeA = $LASTEXITCODE
  & $Exe phrases apply --request $reqB *> $null
  $codeB = $LASTEXITCODE
  if ($codeA -ne 0) { return "first writer failed with $codeA" }
  if ($codeB -ne 5) { return "second writer expected conflict 5, got $codeB" }
  $body = [System.IO.File]::ReadAllText($target, $utf8)
  if ($body -notmatch '甲') { return "first writer's entry missing: $body" }
  if ($body -match '乙') { return "second writer overwrote the first: $body" }
  return $null
}

# --- T-3.4 busy: another deployment holds the gate ---------------------------
Add-Case 'T3.4-busy-lock-retryable' {
  $user = New-UserDir 'busy' "原词`torig`t1`n"
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "原词`torig`t1`n新词`tnew`t1`n", $utf8)
  $req = Write-Request $user $target $false $staged '' 150
  # A long deploy in another process holds the gate.
  $holder = Start-Process -FilePath $Exe -ArgumentList @('--user-data-dir', $user) -PassThru -WindowStyle Hidden
  Start-Sleep -Milliseconds 400
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  try { $holder | Wait-Process -Timeout 60 -ErrorAction SilentlyContinue } catch {}
  if ($code -ne 7) { return "expected busy exit 7, got $code" }
  $body = [System.IO.File]::ReadAllText($target, $utf8)
  if ($body -match '新词') { return "busy apply must not publish: $body" }
  return $null
}

# --- T-3.5 crash after publish: next run restores the previous file ----------
Add-Case 'T3.5-crash-after-publish-recovers' {
  $user = New-UserDir 'crash-pub' "原词`torig`t1`n"
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "原词`torig`t1`n崩溃词`tcrash`t1`n", $utf8)
  $req = Write-Request $user $target $false $staged '' 3000
  $env:AUTOIME_TEST_CRASH_AFTER_PUBLISH = '1'
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  Remove-Item Env:AUTOIME_TEST_CRASH_AFTER_PUBLISH -ErrorAction SilentlyContinue
  if ($code -ne 99) { return "expected simulated crash 99, got $code" }
  if (-not (Test-Path (Join-Path $user '.autoime-phrase-txn'))) { return 'journal missing after crash' }
  # Any next deployment settles the interrupted transaction.
  & $Exe --user-data-dir $user *> $null
  $code2 = $LASTEXITCODE
  if (Test-Path (Join-Path $user '.autoime-phrase-txn')) { return 'journal not cleared by recovery' }
  $body = [System.IO.File]::ReadAllText($target, $utf8)
  if ($body -notmatch '原词') { return "old entry missing after recovery: $body" }
  if ($body -match '崩溃词') { return "crash-published entry survived recovery: $body" }
  if ($code2 -ne 0) { return "engine not ready after recovery: exit $code2" }
  return $null
}

# --- T-3.6 crash after journal (before publish): old file intact ------------
Add-Case 'T3.6-crash-before-publish-recovers' {
  $user = New-UserDir 'crash-pre' "原词`torig`t1`n"
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "原词`torig`t1`n未发布`tunpub`t1`n", $utf8)
  $req = Write-Request $user $target $false $staged '' 3000
  $env:AUTOIME_TEST_CRASH_AFTER_JOURNAL = '1'
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  Remove-Item Env:AUTOIME_TEST_CRASH_AFTER_JOURNAL -ErrorAction SilentlyContinue
  if ($code -ne 98) { return "expected simulated crash 98, got $code" }
  if (-not (Test-Path (Join-Path $user '.autoime-phrase-txn'))) {
    return 'journal missing after crash-98 (the seam must sit after WriteJournal)'
  }
  if (-not (Test-Path (Join-Path $user '.autoime-phrase-backup'))) {
    return 'backup missing after crash-98'
  }
  $body = [System.IO.File]::ReadAllText($target, $utf8)
  if ($body -match '未发布') { return "pre-publish crash published the staged file: $body" }
  & $Exe --user-data-dir $user *> $null
  if (Test-Path (Join-Path $user '.autoime-phrase-txn')) { return 'journal not cleared by recovery' }
  return (Assert-Equal "原词`torig`t1`n" ([System.IO.File]::ReadAllText($target, $utf8)) 'data file after recovery')
}

# --- T-3.7 verification present: a NEW session must see the phrase ----------
Add-Case 'T3.7-verify-present-publishes' {
  $user = New-UserDir 'verify-present' "原词`torig`t1`n"
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "原词`torig`t1`n验证词`ttvfy`t9`n", $utf8)
  $verify = Join-Path $user 'verify.txt'
  [System.IO.File]::WriteAllText($verify, "present`ttvfy`t验证词`n", $utf8)
  $req = Write-Request $user $target $false $staged $verify 3000
  $out = & $Exe phrases apply --request $req 2>&1
  $code = $LASTEXITCODE
  if ($code -ne 0) { return "expected success, got ${code}: $($out -join ' | ')" }
  $body = [System.IO.File]::ReadAllText($target, $utf8)
  return (Assert-Equal "原词`torig`t1`n验证词`ttvfy`t9`n" $body 'data file')
}

# --- T-3.8 verification absent: a removed phrase must disappear -------------
# Prepared prerequisite: the removal oracle only means something when the
# phrase is already visible, so the suite primes one engine build once (shared
# preparation, reported separately) and the case copies that warm user dir.
Add-Case 'T3.8-verify-absent-publishes' {
  $user = Join-Path $Scratch 'verify-absent'
  Copy-Item -Recurse -Force $primeAbsent $user
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "原词`torig`t1`n", $utf8)
  $verify = Join-Path $user 'verify.txt'
  [System.IO.File]::WriteAllText($verify, "absent`tgone`t移除词`n", $utf8)
  $req = Write-Request $user $target $false $staged $verify 3000
  $out = & $Exe phrases apply --request $req 2>&1
  $code = $LASTEXITCODE
  if ($code -ne 0) { return "expected success, got ${code}: $($out -join ' | ')" }
  return (Assert-Equal "原词`torig`t1`n" ([System.IO.File]::ReadAllText($target, $utf8)) 'data file')
}
# --- T-3.9 verification failure rolls back the phrase file ------------------
Add-Case 'T3.9-failed-verification-rolls-back' {
  $user = New-UserDir 'verify-fail' "原词`torig`t1`n"
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "原词`torig`t1`n不可见词`tzzz`t9`n", $utf8)
  $verify = Join-Path $user 'verify.txt'
  # The code does not produce this text, so the present check must fail.
  [System.IO.File]::WriteAllText($verify, "present`tzzz`t不存在的候选`n", $utf8)
  $req = Write-Request $user $target $false $staged $verify 3000
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  if ($code -ne 6) { return "expected deploy failure 6, got $code" }
  return (Assert-Equal "原词`torig`t1`n" ([System.IO.File]::ReadAllText($target, $utf8)) 'data file after rollback')
}

# --- T-3.10 missing staged file is a precondition failure -------------------
Add-Case 'T3.10-missing-staged-rejected' {
  $user = New-UserDir 'missing-staged' "原词`torig`t1`n"
  $target = Join-Path $user 'custom_phrase.txt'
  $req = Write-Request $user $target $false (Join-Path $user 'nope.txt') '' 3000
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  if ($code -ne 4) { return "expected io exit 4, got $code" }
  return (Assert-Equal "原词`torig`t1`n" ([System.IO.File]::ReadAllText($target, $utf8)) 'data file')
}

# --- T-3.11 independent process: the probe must see the added phrase --------
Add-Case 'T3.11-independent-probe-sees-phrase' {
  if (-not $ManagerExe) { return 'ManagerExe not provided (preparation)' }
  $env:AUTOIME_DEPLOY_EXE = $Exe
  if (-not $Probe) { return 'Probe not provided (preparation)' }
  if (-not (Test-Path $ManagerExe) -or -not (Test-Path $Probe)) { return 'probe/manager missing (preparation)' }
  $user = New-UserDir 'probe-user' $null
  Copy-Item (Join-Path $Resources 'custom_phrase.txt') (Join-Path $user 'custom_phrase.txt') -Force
  & $ManagerExe phrases add --code syntest --text '星河测试词' --user-data-dir $user *> $null
  if ($LASTEXITCODE -ne 0) { return "manager add failed: $LASTEXITCODE" }
  $cases = Join-Path $user 'probe.cases.json'
  [System.IO.File]::WriteAllText($cases,
    '{"format":"autoime-cases@1","schema":"autoime_pinyin","cases":[{"id":"synth-syntest","keys":"syntest","expect_first":"星河测试词"}]}',
    $utf8)
  $out = & $Probe --rime-dll $RimeDll --shared-data-dir $shared --resources-dir $Resources --user-data-dir $user --schema autoime_pinyin --cases $cases 2>&1
  $code = $LASTEXITCODE
  if ($code -ne 0) { return "probe failed: $code" }
  if (-not (($out -join "`n") -match 'RESULT case-synth-syntest=PASS')) { return 'probe did not report the synthetic case as PASS' }
  return $null
}

# --- T-3.15 frozen 071 baseline replay stays green -------------------------
Add-Case 'T3.15-baseline-cases-regression' {
  if (-not $Probe) { return 'Probe not provided (preparation)' }
  if (-not $BaselineCases -or -not (Test-Path $BaselineCases)) { return 'BaselineCases not provided (preparation)' }
  # Its own prepared dir: the probe's built-in t8 redeploys the user dir.
  $base = New-UserDir 'probe-baseline' $null
  Copy-Item (Join-Path $Resources 'custom_phrase.txt') (Join-Path $base 'custom_phrase.txt') -Force
  $baseOut = & $Probe --rime-dll $RimeDll --shared-data-dir $shared --resources-dir $Resources --user-data-dir $base --schema autoime_pinyin --cases $BaselineCases 2>&1
  if ($LASTEXITCODE -ne 0) { return "baseline probe failed: $LASTEXITCODE" }
  if (($baseOut -join "`n") -match '=FAIL') { return 'baseline cases regressed' }
  return $null
}

# --- T-3.12 Unicode user-data path -----------------------------------------
Add-Case 'T3.12-unicode-user-dir' {
  $user = Join-Path $Scratch '中文 目录\用户 数据'
  New-Item -ItemType Directory -Force $user | Out-Null
  $target = Join-Path $user 'custom_phrase.txt'
  [System.IO.File]::WriteAllText($target, "原词`torig`t1`n", $utf8)
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "原词`torig`t1`n中文路径词`tucnk`t7`n", $utf8)
  $verify = Join-Path $user 'verify.txt'
  [System.IO.File]::WriteAllText($verify, "present`tucnk`t中文路径词`n", $utf8)
  $req = Write-Request $user $target $false $staged $verify 3000
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  if ($code -ne 0) { return "expected success on a Unicode path, got $code" }
  return (Assert-Equal "原词`torig`t1`n中文路径词`tucnk`t7`n" ([System.IO.File]::ReadAllText($target, $utf8)) 'data file')
}

# --- T-3.13 learning db survives rollback ----------------------------------
Add-Case 'T3.13-learning-db-preserved' {
  $user = Join-Path $Scratch '_prime-absent'
  if (-not (Test-Path (Join-Path $user 'autoime_user.userdb'))) { return 'prepared warm dir has no learning db' }
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "原词`torig`t1`n坏验证`tbadq`t1`n", $utf8)
  $verify = Join-Path $user 'verify.txt'
  [System.IO.File]::WriteAllText($verify, "present`tbadq`t不存在的候选`n", $utf8)
  $req = Write-Request $user $target $false $staged $verify 3000
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  if ($code -ne 6) { return "expected rollback 6, got $code" }
  if (-not (Test-Path (Join-Path $user 'autoime_user.userdb'))) { return 'learning db removed by rollback' }
  return $null
}

# --- T-3.14 broken resources: no success claim -----------------------------
Add-Case 'T3.14-broken-resources-fails' {
  $broken = Join-Path $Scratch 'broken-resources'
  New-Item -ItemType Directory -Force $broken | Out-Null
  foreach ($name in 'autoime_terms.schema.yaml','autoime_en.schema.yaml','autoime_terms.dict.yaml','autoime_en.dict.yaml') {
    Copy-Item (Join-Path $Resources $name) (Join-Path $broken $name) -Force
  }
  $user = New-UserDir 'broken-user' "原词`torig`t1`n"
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "原词`torig`t1`n坏资源词`tbrk`t1`n", $utf8)
  $req = Write-Request $user $target $false $staged '' 3000
  $env:AUTOIME_RESOURCES_DIR = $broken
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  $env:AUTOIME_RESOURCES_DIR = $Resources
  if ($code -ne 6) { return "expected deploy failure 6, got $code" }
  return (Assert-Equal "原词`torig`t1`n" ([System.IO.File]::ReadAllText($target, $utf8)) 'data file')
}

# --- T-3.16 present beyond the first candidate page (R-02) -------------------
Add-Case 'T3.16-present-beyond-first-page' {
  $seed = "同码一`tqzx`t1`n同码二`tqzx`t1`n同码三`tqzx`t1`n同码四`tqzx`t1`n同码五`tqzx`t1`n"
  $user = New-UserDir 'samecode-present' $seed
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, $seed + "同码六`tqzx`t1`n", $utf8)
  $verify = Join-Path $user 'verify.txt'
  [System.IO.File]::WriteAllText($verify, "present`tqzx`t同码六`npresent`tqzx`t同码一`n", $utf8)
  $req = Write-Request $user $target $false $staged $verify 3000
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  if ($code -ne 0) { return "expected the 6th same-code text to verify, got $code" }
  $body = [System.IO.File]::ReadAllText($target, $utf8)
  if ($body -notmatch '同码六') { return "the 6th same-code text was not published: $body" }
  return $null
}

# --- T-3.17 absent must mean absent from every page (R-02) -------------------
Add-Case 'T3.17-absent-not-fooled-by-page' {
  # 同码一 gets the lowest weight so it sorts last (page 2 with page size 5).
  $seed = "同码二`tqzz`t10`n同码三`tqzz`t11`n同码四`tqzz`t12`n同码五`tqzz`t13`n同码六`tqzz`t14`n同码一`tqzz`t1`n"
  $user = New-UserDir 'samecode-absent' $seed
  $target = Join-Path $user 'custom_phrase.txt'
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, $seed, $utf8)  # no change: 同码一 remains
  $verify = Join-Path $user 'verify.txt'
  [System.IO.File]::WriteAllText($verify, "absent`tqzz`t同码一`n", $utf8)
  $req = Write-Request $user $target $false $staged $verify 3000
  & $Exe phrases apply --request $req *> $null
  $code = $LASTEXITCODE
  if ($code -ne 6) { return "absent check must fail when the text is still on a later page; got $code" }
  return (Assert-Equal $seed ([System.IO.File]::ReadAllText($target, $utf8)) 'data file after rollback')
}

# --- T-3.18 orphan backup debris is cleaned (R-07) ---------------------------
Add-Case 'T3.18-orphan-backup-cleaned' {
  $user = New-UserDir 'orphan-backup' "原词`torig`t1`n"
  $target = Join-Path $user 'custom_phrase.txt'
  [System.IO.File]::WriteAllText((Join-Path $user '.autoime-phrase-backup'), 'debris', $utf8)
  $staged = Join-Path $user 'staged.txt'
  [System.IO.File]::WriteAllText($staged, "原词`torig`t1`n新词`tnewb`t1`n", $utf8)
  $verify = Join-Path $user 'verify.txt'
  [System.IO.File]::WriteAllText($verify, "present`tnewb`t新词`n", $utf8)
  $req = Write-Request $user $target $false $staged $verify 3000
  & $Exe phrases apply --request $req *> $null
  if ($LASTEXITCODE -ne 0) { return "apply failed with orphan backup: $LASTEXITCODE" }
  if (Test-Path (Join-Path $user '.autoime-phrase-backup')) { return 'orphan backup survived a successful transaction' }
  return $null
}

Write-Host ''
Write-Host ("[txn] {0} cases, {1} passed, {2} failed" -f $script:results.Count, ($script:results.Count - $script:failures.Count), $script:failures.Count)
$results | ForEach-Object { Write-Host ("  {0,-42} {1,6} ms  {2}" -f $_.id, $_.ms, $(if ($_.ok) { 'ok' } else { 'FAIL' })) }

if (-not $Keep) { Remove-Item -Recurse -Force $Scratch -ErrorAction SilentlyContinue }
else { Write-Host "[txn] kept scratch: $Scratch" }

if ($script:failures.Count -gt 0) { exit 1 }
exit 0
