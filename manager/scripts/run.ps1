# AutoIME manager — development run entry (PLAN-072 T-01).
#
# Runs the prepared launcher against the in-repo Auto source so the manager
# can be exercised without an install. The launcher is built by
# scripts/prepare.ps1 (explicit preparation; this script does not build).
#
# Usage:
#   pwsh -NoProfile -File manager/scripts/run.ps1 -Program probe phrases list
#   pwsh -NoProfile -File manager/scripts/run.ps1 phrases add --code zlby ...
param(
  [ValidateSet('probe', 'src')][string]$Program = 'src',
  [Parameter(ValueFromRemainingArguments = $true)][string[]]$Args
)

$ErrorActionPreference = 'Stop'

$manager = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exe = Join-Path $manager 'bin/autoime-config.exe'
if (-not (Test-Path $exe)) {
  throw "[run] preparation missing: $exe (run manager/scripts/prepare.ps1 first)"
}

$programDir = Join-Path $manager $Program
if (-not (Test-Path (Join-Path $programDir 'main.at'))) {
  throw "[run] program source missing: $programDir\main.at"
}

$env:AUTOIME_MANAGER_DIR = $programDir
& $exe @Args
exit $LASTEXITCODE
