# AutoIME installer — stage the AV phrase manager (PLAN-072 T-04).
#
# Explicit preparation: manager/scripts/prepare.ps1 must have produced
# manager/bin/autoime-config.exe + identity.txt. This script stages the
# launcher, the Auto business sources and the build identity into the Inno
# staging tree, and refuses to continue when the launcher hash does not match
# the recorded identity (named failure, no package).
#
# Usage (called by build.cmd):
#   pwsh -NoProfile -File stage-manager.ps1 -Autoime <autoime root> -Stage <stage dir>
param(
  [Parameter(Mandatory = $true)][string]$Autoime,
  [Parameter(Mandatory = $true)][string]$Stage
)

$ErrorActionPreference = 'Stop'

$exe = Join-Path $Autoime 'manager\bin\autoime-config.exe'
$identity = Join-Path $Autoime 'manager\bin\identity.txt'
$srcDir = Join-Path $Autoime 'manager\src'
foreach ($path in @($exe, $identity, $srcDir)) {
  if (-not (Test-Path $path)) {
    throw "[stage-manager] preparation missing: $path (run manager\scripts\prepare.ps1 first)"
  }
}
$sources = @(Get-ChildItem $srcDir -Filter *.at)
if ($sources.Count -eq 0) {
  throw "[stage-manager] preparation missing: no Auto sources under $srcDir"
}

$stageManager = Join-Path $Stage 'manager'
$stageSrc = Join-Path $stageManager 'src'
New-Item -ItemType Directory -Force $stageSrc | Out-Null
Copy-Item $exe (Join-Path $Stage 'autoime-config.exe') -Force
foreach ($source in $sources) {
  Copy-Item $source.FullName (Join-Path $stageSrc $source.Name) -Force
}
Copy-Item $identity (Join-Path $stageManager 'identity.txt') -Force

$text = Get-Content $identity -Raw
if ($text -notmatch 'launcher_sha256=([0-9A-Fa-f]{64})') {
  throw '[stage-manager] identity.txt has no launcher_sha256'
}
$want = $Matches[1].ToUpperInvariant()
$got = (Get-FileHash (Join-Path $Stage 'autoime-config.exe') -Algorithm SHA256).Hash
if ($want -ne $got) {
  throw "[stage-manager] staged launcher hash mismatch: $got != $want"
}
# N-06 repair: verify every source identity line against the files being
# staged, so a source edit can never ship with a stale identity.txt.
$listed = @{}
foreach ($line in ($text -split "`r?`n")) {
  if ($line -match '^source=(?<name>[^ ]+) (?<hash>[0-9A-Fa-f]{64})$') {
    $listed[$Matches['name']] = $Matches['hash'].ToUpperInvariant()
  }
}
$actual = @{}
foreach ($source in $sources) {
  $actual[$source.Name] = (Get-FileHash $source.FullName -Algorithm SHA256).Hash
}
if ($listed.Count -ne $actual.Count) {
  throw "[stage-manager] identity lists $($listed.Count) sources but src has $($actual.Count); run manager\scripts\prepare.ps1"
}
foreach ($name in $actual.Keys) {
  if (-not $listed.ContainsKey($name)) {
    throw "[stage-manager] identity is missing source=$name; run manager\scripts\prepare.ps1"
  }
  if ($listed[$name] -ne $actual[$name]) {
    throw "[stage-manager] stale identity for $name ($($listed[$name]) != $($actual[$name])); run manager\scripts\prepare.ps1"
  }
}
Write-Host ("[stage-manager] source identity verified for {0} files" -f $actual.Count)

if ($text -notmatch 'crt_static=1') {
  Write-Warning '[stage-manager] launcher was built with a dynamic CRT; the offline package would need the VC++ redistributable'
}
Write-Host ("[stage-manager] staged launcher sha256={0} + {1} Auto sources + identity" -f $want, $sources.Count)
