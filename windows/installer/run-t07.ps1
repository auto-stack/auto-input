# AutoIME offline package acceptance (PLAN-078 T-07, V-07 package part).
#
# Verifies the artifact that ships without needing an elevated install:
#   1. dist\AutoInput-Setup-x64.exe exists and its recorded SHA-256 matches;
#   2. stage\ carries every runtime file the plan delivers (DLL, engine owner,
#      deploy tool, manager launcher + Auto sources, resources incl. the
#      personal-term template, locked Rime payload);
#   3. stage\resources is byte-identical to shared\resources (single source);
#   4. every bundle-manifest.json entry matches its locked SHA-256;
#   5. a fresh "install" directory copied from stage\ deploys from the packaged
#      rime.dll + data and recalls the frozen candidates;
#   6. an upgrade deploy keeps the user's custom_phrase.txt and personal terms.
#
# The elevated clean-install / two-host TSF walkthrough is a separate manual
# step (needs an admin prompt); this script never registers anything.
#
# Usage: pwsh -NoProfile -File windows/installer/run-t07.ps1 [-Keep]
param([switch]$Keep)

$ErrorActionPreference = 'Stop'

$installer = $PSScriptRoot
$autoime = Split-Path -Parent (Split-Path -Parent $installer)
$stage = Join-Path $installer 'stage'
$dist = Join-Path $installer 'dist/AutoInput-Setup-x64.exe'
$shaFile = "$dist.sha256"
$manifest = Join-Path $installer 'bundle-manifest.json'
$resources = Join-Path $autoime 'shared/resources'
$probe = Join-Path $autoime 'windows/probe/build/rime_probe.exe'

$checks = 0
$failures = 0
function Check([bool]$ok, [string]$what) {
  $script:checks++
  if ($ok) {
    Write-Host "PASS $what"
  } else {
    $script:failures++
    Write-Host "FAIL $what"
  }
}

foreach ($path in @($dist, $shaFile, $manifest, $probe,
                    (Join-Path $stage 'autoime_ime.dll'),
                    (Join-Path $stage 'autoime_engine.exe'))) {
  if (-not (Test-Path $path)) { throw "[run-t07] preparation missing: $path (run installer/build.cmd)" }
}

$start = Get-Date
$recorded = (Get-Content $shaFile -Raw).Trim().Split(' ')[0].ToUpperInvariant()
$actual = (Get-FileHash $dist -Algorithm SHA256).Hash.ToUpperInvariant()
Check ($recorded -eq $actual) "package SHA-256 matches ($actual)"
Check ((Get-Item $dist).Length -gt 5000000) 'package size is plausible'

$required = @(
  'autoime_ime.dll', 'autoime_engine.exe', 'autoime_deploy.exe',
  'autoime-config.exe', 'verify_registered.exe',
  'resources/autoime_pinyin.schema.yaml', 'resources/autoime_en.dict.yaml',
  'resources/autoime_user_terms.dict.yaml', 'resources/autoime_user_terms.schema.yaml',
  'resources/custom_phrase.txt', 'manager/src/main.at', 'manager/src/phrases.at',
  'manager/src/terms.at', 'manager/identity.txt', 'rime/rime.dll', 'rime/data/luna_pinyin.dict.yaml'
)
foreach ($rel in $required) {
  Check (Test-Path (Join-Path $stage $rel)) "stage has $rel"
}

# Single source of truth: packaged resources equal the repository resources.
$diff = @()
foreach ($file in Get-ChildItem (Join-Path $stage 'resources') -File) {
  $repo = Join-Path $resources $file.Name
  if (-not (Test-Path $repo)) { $diff += "$($file.Name): missing in repo"; continue }
  $a = (Get-FileHash $file.FullName -Algorithm SHA256).Hash
  $b = (Get-FileHash $repo -Algorithm SHA256).Hash
  if ($a -ne $b) { $diff += "$($file.Name): hash differs" }
}
Check ($diff.Count -eq 0) ("stage resources equal shared/resources" + $(if ($diff.Count) { " -> " + ($diff -join '; ') } else { '' }))

$bundle = Get-Content $manifest -Raw | ConvertFrom-Json
$bad = @()
foreach ($entry in $bundle.files) {
  $target = Join-Path (Join-Path $stage 'rime') $entry.dest
  if (-not (Test-Path $target)) { $bad += "$($entry.dest): missing"; continue }
  $hash = (Get-FileHash $target -Algorithm SHA256).Hash.ToUpperInvariant()
  if ($hash -ne $entry.sha256.ToUpperInvariant()) { $bad += "$($entry.dest): hash" }
}
Check ($bad.Count -eq 0) ("bundle manifest hashes match" + $(if ($bad.Count) { " -> " + ($bad -join '; ') } else { '' }))

# Simulated installed layout: copy the payload like the installer would.
$root = Join-Path ([System.IO.Path]::GetTempPath()) ('autoime-t07-' + $PID + '-' + [DateTime]::UtcNow.Ticks)
$install = Join-Path $root 'install'
New-Item -ItemType Directory -Force -Path $install | Out-Null
Copy-Item -Path (Join-Path $stage '*') -Destination $install -Recurse -Force
$user = Join-Path $root 'rime'
New-Item -ItemType Directory -Force -Path $user | Out-Null

$deploy = Join-Path $install 'autoime_deploy.exe'
$deployStart = Get-Date
$deployOut = & $deploy --resources-dir (Join-Path $install 'resources') --user-data-dir $user 2>&1
$deployCode = $LASTEXITCODE
$deployMs = ((Get-Date) - $deployStart).TotalMilliseconds
Check ($deployCode -eq 0) ("packaged deploy from a fresh install dir (exit $deployCode, $([Math]::Round($deployMs)) ms)")

$probeStart = Get-Date
$cases = Join-Path $autoime 'shared/tests/en-admission.json'
$probeOut = & $probe --schema autoime_pinyin --rime-dll (Join-Path $install 'rime/rime.dll') `
  --shared-data-dir (Join-Path $install 'rime/data') `
  --resources-dir (Join-Path $install 'resources') --cases $cases 2>&1
$probeCode = $LASTEXITCODE
$probeMs = ((Get-Date) - $probeStart).TotalMilliseconds
Check ($probeCode -eq 0) ("packaged payload recalls the frozen candidates (exit $probeCode, $([Math]::Round($probeMs)) ms)")

# Upgrade keeps user files: phrase + personal term, then redeploy.
$phrase = Join-Path $user 'custom_phrase.txt'
$terms = Join-Path $user 'autoime_user_terms.dict.yaml'
Add-Content -Path $phrase -Value "安装包验收词`tvhost`t500"
$termsText = @"
---
name: autoime_user_terms
version: "0.1.0"
sort: by_weight
columns:
  - text
  - code
  - weight
...
AutoScape	AS	1500
"@
Set-Content -Path $terms -Value ($termsText -replace "`r`n", "`n") -NoNewline
$upgradeOut = & $deploy --resources-dir (Join-Path $install 'resources') --user-data-dir $user 2>&1
$upgradeCode = $LASTEXITCODE
$keptPhrase = (Get-Content $phrase -Raw) -match '安装包验收词'
$keptTerms = (Get-Content $terms -Raw) -match 'AutoScape'
Check ($upgradeCode -eq 0 -and $keptPhrase -and $keptTerms) "upgrade keeps custom phrases and personal terms"

# PLAN-078 review F-02: the package must be what the machine actually runs,
# engine included (a restartreplace used to leave the previous engine in place).
$installedRoot = 'C:\Program Files\AutoIME'
if (Test-Path $installedRoot) {
  foreach ($name in @('autoime_ime.dll', 'autoime_engine.exe', 'autoime_deploy.exe')) {
    $installedFile = Join-Path $installedRoot $name
    $stageFile = Join-Path $stage $name
    $hi = if (Test-Path $installedFile) { (Get-FileHash $installedFile -Algorithm SHA256).Hash } else { '' }
    $hs = if (Test-Path $stageFile) { (Get-FileHash $stageFile -Algorithm SHA256).Hash } else { '' }
    Check ($hi -ne '' -and $hi -eq $hs) ("installed $name matches the package")
  }
  $engineFile = Join-Path $installedRoot 'autoime_engine.exe'
  if (Test-Path $engineFile) {
    $engineTime = (Get-Item $engineFile).LastWriteTime
    $stale = @(Get-Process autoime_engine -ErrorAction SilentlyContinue |
        Where-Object { $_.StartTime -lt $engineTime })
    Check ($stale.Count -eq 0) 'no engine owner runs an older image than the installed one'
  }
}

$elapsed = ((Get-Date) - $start).TotalSeconds
Write-Host ("[run-t07] checks={0} failures={1} elapsed={2:F1}s" -f $checks, $failures, $elapsed)
if (-not $Keep) {
  Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
} else {
  Write-Host "[run-t07] kept $root"
}
if ($failures -ne 0) { throw "[run-t07] $failures package check(s) failed" }
Write-Host '[run-t07] OK'
