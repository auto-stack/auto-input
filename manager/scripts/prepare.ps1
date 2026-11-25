# AutoIME manager — explicit preparation (PLAN-072 T-01).
#
# Resolves the ACE source root in the repository-mandated order
# ($AUTO_LANG_ROOT -> group sibling auto-lang -> main checkout), renders the
# launcher manifest from Cargo.toml.in, builds the thin AV launcher and copies
# it to manager/bin/. Missing prerequisites are named failures: this script
# never falls back to a different runtime.
#
# Usage:
#   pwsh -NoProfile -File manager/scripts/prepare.ps1 [-Configuration release|debug]
param(
  [ValidateSet('release', 'debug')][string]$Configuration = 'release',
  # The shipped launcher is fully self-contained: static MSVC CRT so the
  # offline package depends on no VC++ redistributable on the target machine.
  [bool]$StaticCrt = $true
)

$ErrorActionPreference = 'Stop'

$manager = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$native = Join-Path $manager 'native'

function Resolve-AutoLangRoot {
  if ($env:AUTO_LANG_ROOT -and (Test-Path (Join-Path $env:AUTO_LANG_ROOT 'experimental/ace/crates/core/Cargo.toml'))) {
    return (Resolve-Path $env:AUTO_LANG_ROOT).Path
  }
  # Standalone checkout with AutoLang as a sibling.
  $repoRoot = Split-Path -Parent $manager
  $sibling = Join-Path (Split-Path -Parent $repoRoot) 'auto-lang'
  if (Test-Path (Join-Path $sibling 'experimental/ace/crates/core/Cargo.toml')) {
    return (Resolve-Path $sibling).Path
  }
  $main = 'D:/autostack/auto-lang'
  if (Test-Path (Join-Path $main 'experimental/ace/crates/core/Cargo.toml')) {
    return $main
  }
  throw ('[prepare] ACE source root not found. Tried $AUTO_LANG_ROOT, group sibling ' +
         "'$sibling', and '$main'. The manager runs on the new ACE AV only; there is no fallback.")
}

$langRoot = Resolve-AutoLangRoot
Write-Host "[prepare] ACE source root: $langRoot"

$template = Join-Path $native 'Cargo.toml.in'
$manifest = Join-Path $native 'Cargo.toml'
$text = Get-Content $template -Raw -Encoding UTF8
$text = $text.Replace('@AUTO_LANG_ROOT@', ($langRoot -replace '\\', '/'))
Set-Content -Path $manifest -Value $text -Encoding UTF8 -NoNewline
Write-Host "[prepare] rendered $manifest"

if ($StaticCrt) {
  # Static CRT keeps the launcher dependent only on OS DLLs (verified with
  # `dumpbin /dependents`); the offline package ships no VC++ redistributable.
  # R-03 repair: the ACE front end binds module sources recursively, so the
  # shipped launcher reserves 32 MiB of stack (same value as the auto-os
  # desktop host). RUSTFLAGS replaces any target config, hence both flags here.
  $env:RUSTFLAGS = '-C target-feature=+crt-static -C link-arg=/STACK:33554432'
  Write-Host '[prepare] static CRT + 32 MiB stack enabled'
}

$rustc = (Get-Command rustc -ErrorAction SilentlyContinue)
if (-not $rustc) { throw '[prepare] rustc not found on PATH (explicit prerequisite)' }
$cargo = (Get-Command cargo -ErrorAction SilentlyContinue)
if (-not $cargo) { throw '[prepare] cargo not found on PATH (explicit prerequisite)' }

$binDir = Join-Path $manager 'bin'
$binExe = Join-Path $binDir 'autoime-config.exe'
$identityPath = Join-Path $binDir 'identity.txt'
$oldHash = if (Test-Path $binExe) { (Get-FileHash $binExe -Algorithm SHA256).Hash } else { '' }
$oldRev = ''
if (Test-Path $identityPath) {
  $oldText = Get-Content $identityPath -Raw
  if ($oldText -match '(?m)^ace_rev=([0-9a-f]{40})$') { $oldRev = $Matches[1] }
}

Push-Location $native
try {
  $cargoArgs = @('build', '--bin', 'autoime-config')
  if ($Configuration -eq 'release') { $cargoArgs += '--release' }
  & cargo @cargoArgs
  if ($LASTEXITCODE -ne 0) { throw "[prepare] cargo build failed ($LASTEXITCODE)" }
} finally {
  Pop-Location
}

$outDir = if ($Configuration -eq 'release') { 'release' } else { 'debug' }
# Cargo's target dir may be redirected (daily builds reuse a prepared ACE
# cache); locate the artifact where cargo actually wrote it.
$targetDir = if ($env:CARGO_TARGET_DIR) { $env:CARGO_TARGET_DIR } else { Join-Path $native 'target' }
$exe = Join-Path $targetDir "$outDir/autoime-config.exe"
if (-not (Test-Path $exe)) { throw "[prepare] build product missing: $exe" }
$bin = Join-Path $manager 'bin'
New-Item -ItemType Directory -Force $bin | Out-Null
Copy-Item $exe (Join-Path $bin 'autoime-config.exe') -Force

$exePath = Join-Path $bin 'autoime-config.exe'
$hash = (Get-FileHash $exePath -Algorithm SHA256).Hash
$bytes = (Get-Item $exePath).Length
$rustcVersion = (& rustc --version).Trim()
$langHead = (& git -C $langRoot rev-parse HEAD).Trim()
# The launcher is a static artifact: when cargo reuses the previously linked
# binary (same hash), the truthful ACE revision is the one that binary was
# built with, not whatever HEAD happens to be now. A rebuilt binary records
# the current HEAD.
$langRev = if ($hash -eq $oldHash -and $oldRev) { $oldRev } else { $langHead }

# Durable build identity: the installer ships this file next to the sources and
# the delivery record cites the same hash. No secrets, no phrase data.
$sourceHashes = Get-ChildItem (Join-Path $manager 'src') -Filter *.at | Sort-Object Name |
  ForEach-Object { 'source={0} {1}' -f $_.Name, (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
$identity = @(
  'format=autoime-manager-build@1',
  "configuration=$Configuration",
  "crt_static=$([int]($StaticCrt -eq $true))",
  "launcher_sha256=$hash",
  "launcher_bytes=$bytes",
  "rustc=$rustcVersion",
  "ace_root=$($langRoot -replace '\\', '/')",
  "ace_rev=$langRev",
  "built_at=$((Get-Date).ToString('o'))"
) + $sourceHashes
Set-Content -Path (Join-Path $bin 'identity.txt') -Value $identity -Encoding UTF8
Write-Host "[prepare] launcher: $exePath"
Write-Host "[prepare] identity: rustc=$rustcVersion ace_rev=$langRev sha256=$hash crt_static=$StaticCrt bytes=$bytes"
