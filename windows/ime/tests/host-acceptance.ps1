# AutoIME host acceptance probe (PLAN-073 T-04, V-06).
#
# Verifies the INSTALLED build through real desktop hosts (no fake target):
#   1. a WinForms text box (ordinary TSF client window),
#   2. Windows Notepad driven with hardware-level key events.
# It also checks that Shift+symbol input does not toggle Chinese/English mode
# (the Test-only host bug fixed in f379f58) via %TEMP%\autoime.log.
#
# Quality runs on an isolated EMPTY user data dir (AUTOIME_RIME_USER_DATA_DIR)
# so the acceptance matches the frozen empty-profile baseline instead of the
# user's learned dictionary.
#
# Preconditions (explicit preparation, this script never builds):
#   - the same build is registered: %ProgramFiles%\AutoIME\autoime_ime.dll must
#     match -BuildDll (default: the repo's ime\build\autoime_ime.dll);
#   - %ProgramFiles%\AutoIME\resources contains autoime_words.dict.yaml (0.2.0);
#   - run from an interactive desktop session (windows must be foregroundable).
#
# Usage:
#   pwsh -NoProfile -File ime\tests\host-acceptance.ps1 [-BuildDll PATH]
#        [-SkipNotepad] [-KeepUserDir]
# Exit: 0 all checks passed, 1 a check failed, 2 preparation failed.
param(
  [string]$BuildDll = '',
  [switch]$SkipNotepad,
  [switch]$KeepUserDir
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public static class HostAccept {
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool attach);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern IntPtr GetFocus();
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public static List<string> VisibleWindows() {
    var r = new List<string>();
    EnumWindows((h, l) => {
      if (IsWindowVisible(h)) {
        var sb = new StringBuilder(300);
        GetWindowText(h, sb, 300);
        uint pid; GetWindowThreadProcessId(h, out pid);
        if (sb.Length > 0) r.Add(pid + "|" + h.ToInt64() + "|" + sb.ToString());
      }
      return true;
    }, IntPtr.Zero);
    return r;
  }
}
"@

$KEYUP = 0x0002
$script:checks = 0
$script:failures = 0
function Check([bool]$condition, [string]$name) {
  $script:checks++
  if ($condition) { Write-Host "PASS $name" }
  else { $script:failures++; Write-Host "FAIL $name" }
}
function Tap([byte]$vk, [bool]$shift = $false) {
  if ($shift) { [HostAccept]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero) }
  [HostAccept]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
  [HostAccept]::keybd_event($vk, 0, $KEYUP, [UIntPtr]::Zero)
  if ($shift) { [HostAccept]::keybd_event(0x10, 0, $KEYUP, [UIntPtr]::Zero) }
  Start-Sleep -Milliseconds 60
}
function Type-Letters([string]$text) {
  foreach ($ch in $text.ToCharArray()) {
    $up = [char]::ToUpperInvariant($ch)
    if ($up -ge 'A' -and $up -le 'Z') { Tap ([byte][char]$up) }
    else { throw "unmapped char: $ch" }
  }
}
# Case-preserving variant: uppercase characters are sent with Shift, exactly
# like a user typing an explicit English trigger code (PLAN-078 T-05).
function Type-Code([string]$text) {
  foreach ($ch in $text.ToCharArray()) {
    $up = [char]::ToUpperInvariant($ch)
    if ($up -ge 'A' -and $up -le 'Z') {
      Tap ([byte][char]$up) -Shift:([char]::IsUpper($ch))
    } else { throw "unmapped char: $ch" }
  }
}
function ToggleIme {
  [HostAccept]::keybd_event(0x5B, 0, 0, [UIntPtr]::Zero)               # LWin
  Tap 0x20                                                             # Space
  [HostAccept]::keybd_event(0x5B, 0, $KEYUP, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}
# PLAN-078 T-07 safety: Windows refuses SetForegroundWindow from a background
# process. Attach to the current foreground thread, bring the target up and
# re-check; the caller MUST verify the foreground window before typing so keys
# can never leak into an unrelated application.
function Force-Foreground([IntPtr]$h) {
  if ($h -eq [IntPtr]::Zero) { return $false }
  $myThread = [HostAccept]::GetCurrentThreadId()
  # Windows' foreground lock makes a single attempt unreliable; retry briefly
  # with a fresh attach each time and re-check the actual foreground window.
  for ($attempt = 0; $attempt -lt 6; $attempt++) {
    if ([HostAccept]::GetForegroundWindow() -eq $h) { return $true }
    $pidHolder = 0
    $fgThread = [HostAccept]::GetWindowThreadProcessId([HostAccept]::GetForegroundWindow(), [ref]$pidHolder)
    $attach = $false
    if ($fgThread -ne 0 -and $fgThread -ne $myThread) {
      $attach = [HostAccept]::AttachThreadInput($myThread, $fgThread, $true)
    }
    try {
      if ([HostAccept]::IsIconic($h)) { [void][HostAccept]::ShowWindow($h, 9) }  # SW_RESTORE
      [void][HostAccept]::BringWindowToTop($h)
      [void][HostAccept]::SetForegroundWindow($h)
    } finally {
      if ($attach) { [void][HostAccept]::AttachThreadInput($myThread, $fgThread, $false) }
    }
    Start-Sleep -Milliseconds 150
  }
  return ([HostAccept]::GetForegroundWindow() -eq $h)
}
function Assert-Foreground([IntPtr]$h, [string]$what) {
  if (Force-Foreground $h) { return $true }
  Write-Host "[host] SAFETY: $what is not the foreground window; refusing to type"
  Check $false "$what holds the foreground before typing"
  return $false
}
function Get-ForegroundPid {
  $h = [HostAccept]::GetForegroundWindow(); $fg = 0
  [void][HostAccept]::GetWindowThreadProcessId($h, [ref]$fg)
  return $fg
}
function LogCount([string]$token) {
  $path = Join-Path $env:TEMP 'autoime.log'
  if (-not (Test-Path $path)) { return 0 }
  return @(Select-String -Path $path -Pattern $token -SimpleMatch).Count
}
$ZH_NIHAO = [string][char]0x4F60 + [char]0x597D
$ZH_ROS = 'ROS 2'
$ZH_SHI = [string][char]0x662F
$SCRIPT:USER_DIR = Join-Path $env:TEMP ('autoime-host-user-' + [guid]::NewGuid().ToString('N'))

# ---------------------------------------------------------------- preparation
$installed = Join-Path $env:ProgramFiles 'AutoIME\autoime_ime.dll'
if (-not (Test-Path $installed)) {
  Write-Host "[host] preparation failed: $installed missing (install the build first)"
  exit 2
}
if (-not $BuildDll) { $BuildDll = Join-Path $PSScriptRoot '..\build\autoime_ime.dll' }
if (-not (Test-Path $BuildDll)) {
  Write-Host "[host] preparation failed: build dll missing (build ime first): $BuildDll"
  exit 2
}
$installedHash = (Get-FileHash $installed -Algorithm SHA256).Hash
$buildHash = (Get-FileHash $BuildDll -Algorithm SHA256).Hash
Write-Host "[host] installed=$installedHash"
Write-Host "[host] build    =$buildHash"
if ($installedHash -ne $buildHash) {
  Write-Host '[host] preparation failed: installed build differs from the verified build (same-build gate)'
  exit 2
}
$resources = Join-Path $env:ProgramFiles 'AutoIME\resources'
Check (Test-Path (Join-Path $resources 'autoime_words.dict.yaml')) 'prep installed resources contain autoime_words.dict.yaml'
$repoResources = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))) 'shared\resources'
$installedSchema = Join-Path $resources 'autoime_pinyin.schema.yaml'
$repoSchema = Join-Path $repoResources 'autoime_pinyin.schema.yaml'
Check ((Get-FileHash $installedSchema -Algorithm SHA256).Hash -eq (Get-FileHash $repoSchema -Algorithm SHA256).Hash) 'prep installed schema equals the repository schema'
Check (Test-Path (Join-Path $resources 'autoime_user_terms.dict.yaml')) 'prep installed resources contain the personal-term template'
Check (Test-Path (Join-Path $resources 'autoime_user_terms.schema.yaml')) 'prep installed resources contain the personal-term dependency schema'

New-Item -ItemType Directory -Force $SCRIPT:USER_DIR | Out-Null
# PLAN-078 T-05: pre-seed one personal term so the real TSF path exercises the
# user overlay (deployment preserves an existing file and compiles it).
$termsSeed = @(
  '---',
  'name: autoime_user_terms',
  'version: "0.1.0"',
  'sort: by_weight',
  'columns:',
  '  - text',
  '  - code',
  '  - weight',
  '...',
  '',
  "AutoScape`tAS`t1500",
  ''
) -join "`n"
Set-Content -Path (Join-Path $SCRIPT:USER_DIR 'autoime_user_terms.dict.yaml') -Value $termsSeed -Encoding UTF8 -NoNewline
$env:AUTOIME_RIME_USER_DATA_DIR = $SCRIPT:USER_DIR
$env:AUTOIME_RESOURCES_DIR = $resources
Write-Host "[host] isolated user data dir: $SCRIPT:USER_DIR"

# ------------------------------------------- 1. WinForms TSF client (real TSF)
function Run-SelfHostedChecks {
  $form = New-Object System.Windows.Forms.Form
  $form.Text = 'AutoIME host acceptance'
  $form.Width = 720; $form.Height = 200
  $form.StartPosition = 'CenterScreen'
  $form.TopMost = $true
  $box = New-Object System.Windows.Forms.TextBox
  $box.Multiline = $true
  $box.Dock = 'Fill'
  $form.Controls.Add($box)
  $form.Add_Shown({ $form.Activate(); $box.Focus() })
  [void]$form.Show()
  $form.Activate(); $box.Focus()
  for ($i = 0; $i -lt 10; $i++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 80 }

  $formHwnd = $form.Handle
  # Every typing batch runs only while this form is the VERIFIED foreground
  # window; otherwise keys could reach an unrelated application.
  function Type-IntoForm([string]$code, [byte]$label, [bool]$casePreserving = $false) {
    if (-not (Assert-Foreground $formHwnd 'self-host form')) { return $null }
    # Foreground is not enough: the window may not own the keyboard focus, in
    # which case keybd_event would reach whatever window does. Claim focus on
    # our own text box (allowed inside this thread) and verify it.
    $boxHwnd = $box.Handle
    [void][HostAccept]::SetFocus($boxHwnd)
    [System.Windows.Forms.Application]::DoEvents()
    if ([HostAccept]::GetFocus() -ne $boxHwnd) {
      Write-Host '[host] SAFETY: the test text box does not own keyboard focus; refusing to type'
      Check $false 'self-host text box owns keyboard focus'
      return $null
    }
    [void]$box.Clear()
    [void]$box.Focus()
    [System.Windows.Forms.Application]::DoEvents()
    # Re-assert foreground before EVERY key: other applications on this machine
    # can steal the foreground mid-batch, and a leaked key would type into them.
    foreach ($ch in $code.ToCharArray()) {
      if (-not (Force-Foreground $formHwnd)) { return $null }
      if ([HostAccept]::GetFocus() -ne $boxHwnd) { [void][HostAccept]::SetFocus($boxHwnd) }
      $up = [char]::ToUpperInvariant($ch)
      if ($up -ge 'A' -and $up -le 'Z') {
        if ($casePreserving) { Tap ([byte][char]$up) -Shift:([char]::IsUpper($ch)) }
        else { Tap ([byte][char]$up) }
      } else { return $null }
    }
    Start-Sleep -Milliseconds 420
    if (-not (Force-Foreground $formHwnd)) { return $null }
    Tap $label
    for ($k = 0; $k -lt 14; $k++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 50 }
    return [string]$box.Text.Trim()
  }

  # Readiness + identity probe: only AutoIME commits "ros" as "ROS 2".
  $active = $false
  $safe = $true
  for ($t = 0; $t -lt 30 -and -not $active -and $safe; $t++) {
    $got = Type-IntoForm 'ros' 0x31
    if ($null -eq $got) { $safe = $false; break }
    if ($got -eq $ZH_ROS) { $active = $true }
    else {
      $box.Clear(); Start-Sleep -Milliseconds 200
      if (($t % 5) -eq 4) { ToggleIme; Start-Sleep -Milliseconds 200 }
    }
  }
  Check $active 'self-host AutoIME active (probe ros -> ROS 2)'
  if (-not $active -or -not $safe) { $form.Close(); $form.Dispose(); return }

  $cases = @(
    @{ code = 'zlby'; label = 0x31; expect = '增量编译'; name = 'self-host zlby -> 增量编译' },
    @{ code = 'lxpy'; label = 0x31; expect = '离线拼音'; name = 'self-host lxpy -> 离线拼音' },
    @{ code = 'hxck'; label = 0x31; expect = '候选窗口'; name = 'self-host hxck -> 候选窗口' },
    @{ code = 'shide'; label = 0x31; expect = '是的'; name = 'self-host shide -> 是的' },
    @{ code = 'sd'; label = 0x31; expect = '是的'; name = 'self-host sd -> 是的' }
  )
  foreach ($c in $cases) {
    $got = Type-IntoForm $c.code ([byte]$c.label)
    if ($null -eq $got) { $safe = $false; break }
    Write-Host "[host] $($c.code) -> [$got] expected [$($c.expect)]"
    Check ($got -eq $c.expect) $c.name
  }

  # PLAN-078 T-01/T-05 through the real TSF path: single-letter `s` stays
  # Chinese, `SQ` is the explicit case-sensitive trigger, `AS` is the personal
  # term seeded into this isolated profile.
  if ($safe) {
    $got = Type-IntoForm 's' 0x31
    if ($null -eq $got) { $safe = $false } else {
      Check ($got -eq $ZH_SHI) 'self-host s -> 是 (single letter stays Chinese)'
    }
  }
  if ($safe) {
    $got = Type-IntoForm 'SQ' 0x31 $true
    if ($null -eq $got) { $safe = $false } else {
      Write-Host "[host] SQ -> [$got] expected [SQLite]"
      Check ($got -eq 'SQLite') 'self-host SQ -> SQLite (explicit trigger)'
    }
  }
  if ($safe) {
    $got = Type-IntoForm 'AS' 0x31 $true
    if ($null -eq $got) { $safe = $false } else {
      Write-Host "[host] AS -> [$got] expected [AutoScape]"
      Check ($got -eq 'AutoScape') 'self-host AS -> AutoScape (personal term)'
    }
  }

  $before = LogCount 'shift_click_toggle'
  $symbols = @(
    @{ vk = 0xBA; expect = '：'; name = 'self-host Shift+; -> ：' },
    @{ vk = 0xBB; expect = '+'; name = 'self-host Shift+= -> +' },
    @{ vk = 0xBF; expect = '？'; name = 'self-host Shift+/ -> ？' },
    @{ vk = 0x31; expect = '！'; name = 'self-host Shift+1 -> ！' }
  )
  foreach ($s in $symbols) {
    $box.Clear(); $box.Focus(); [System.Windows.Forms.Application]::DoEvents()
    Tap ([byte]$s.vk) -Shift:$true
    for ($k = 0; $k -lt 14; $k++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 50 }
    Write-Host "[host] symbol got [$($box.Text)] expected [$($s.expect)]"
    Check ($box.Text -eq $s.expect) $s.name
  }
  $after = LogCount 'shift_click_toggle'
  Check ($after -eq $before) 'no shift_click_toggle recorded for Shift+symbols'
  $form.Close(); $form.Dispose()
}

# ------------------------------------------------------ 2. Notepad (real host)
function Read-NotepadText {
  Set-Clipboard -Value ''
  [System.Windows.Forms.SendKeys]::SendWait('^a')
  Start-Sleep -Milliseconds 120
  [System.Windows.Forms.SendKeys]::SendWait('^c')
  Start-Sleep -Milliseconds 220
  return ((Get-Clipboard -Raw) -replace "`r`n", "`n").TrimEnd("`n")
}
function Clear-Notepad {
  [System.Windows.Forms.SendKeys]::SendWait('^a')
  Start-Sleep -Milliseconds 100
  [System.Windows.Forms.SendKeys]::SendWait('{DEL}')
  Start-Sleep -Milliseconds 150
}

function Run-NotepadChecks {
  $file = Join-Path $env:TEMP ('autoime-host-' + [guid]::NewGuid().ToString('N') + '.txt')
  Set-Content -Path $file -Value '' -Encoding UTF8 -NoNewline
  $p = Start-Process notepad.exe -ArgumentList ('"' + $file + '"') -PassThru
  $win = $null
  $leaf = Split-Path $file -Leaf
  for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 120
    $win = [HostAccept]::VisibleWindows() | Where-Object { $_ -match [regex]::Escape($leaf) } | Select-Object -First 1
    if ($win) { break }
  }
  if (-not $win) {
    Check $false 'notepad window found'
    try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch {}
    Remove-Item $file -ErrorAction SilentlyContinue
    return
  }
  $parts = $win -split '\|'
  $winPid = [int]$parts[0]
  $hwnd = [IntPtr][long]$parts[1]
  Write-Host "[host] notepad window pid=$winPid title=$($parts[2])"
  [void][HostAccept]::SetForegroundWindow($hwnd)
  Start-Sleep -Milliseconds 500
  $okFocus = ((Get-ForegroundPid) -eq $winPid)
  Check $okFocus 'notepad is the foreground window'
  if (-not $okFocus) {
    foreach ($procId in @($p.Id, $winPid)) { try { Stop-Process -Id $procId -Force -ErrorAction Stop } catch {} }
    Remove-Item $file -ErrorAction SilentlyContinue
    return
  }

  # Readiness + identity probe (fresh user dir: first activation deploys).
  $active = $false
  $safe = $true
  for ($t = 0; $t -lt 40 -and -not $active -and $safe; $t++) {
    if (-not (Assert-Foreground $hwnd 'notepad')) { $safe = $false; break }
    Clear-Notepad
    Type-Letters 'ros'
    Start-Sleep -Milliseconds 350
    Tap 0x31
    Start-Sleep -Milliseconds 350
    if ((Read-NotepadText) -eq $ZH_ROS) { $active = $true }
    if (($t % 8) -eq 7 -and -not $active) {
      ToggleIme
      Start-Sleep -Milliseconds 300
      if (-not (Assert-Foreground $hwnd 'notepad after toggle')) { $safe = $false }
    }
  }
  Check $active 'notepad AutoIME active (probe ros -> ROS 2)'
  if (-not $active) {
    foreach ($procId in @($p.Id, $winPid)) { try { Stop-Process -Id $procId -Force -ErrorAction Stop } catch {} }
    Remove-Item $file -ErrorAction SilentlyContinue
    return
  }

  $before = LogCount 'shift_click_toggle'
  if (-not (Assert-Foreground $hwnd 'notepad sequence start')) {
    foreach ($procId in @($p.Id, $winPid)) { try { Stop-Process -Id $procId -Force -ErrorAction Stop } catch {} }
    Remove-Item $file -ErrorAction SilentlyContinue
    return
  }
  Clear-Notepad
  $seq = @(
    @{ code = 'zlby'; label = 0x31; expect = '增量编译' },
    @{ code = 'lxpy'; label = 0x31; expect = '离线拼音' },
    @{ code = 'hxck'; label = 0x31; expect = '候选窗口' },
    @{ code = 'shide'; label = 0x31; expect = '是的' },
    @{ code = 'sd'; label = 0x31; expect = '是的' }
  )
  $expected = ''
  foreach ($c in $seq) {
    if (-not (Assert-Foreground $hwnd "notepad $($c.code)")) { $safe = $false; break }
    Type-Letters $c.code
    Start-Sleep -Milliseconds 450
    Tap ([byte]$c.label)
    Start-Sleep -Milliseconds 700
    $expected += $c.expect
  }
  if ($safe -and (Assert-Foreground $hwnd 'notepad shift+semicolon')) {
    Tap 0xBA -Shift:$true   # Shift+; -> ：
    Start-Sleep -Milliseconds 800
    $expected += '：'
  } else {
    $safe = $false
  }
  $got1 = Read-NotepadText
  Check ($got1 -eq $expected) 'notepad Shift+; commits ：'
  if ($safe -and (Assert-Foreground $hwnd 'notepad shift+equals')) {
    Tap 0x27               # Right: collapse the Ctrl+A selection to the caret end
    Start-Sleep -Milliseconds 150
    Tap 0xBB -Shift:$true   # Shift+= -> +
    Start-Sleep -Milliseconds 800
    $expected += '+'
  } else {
    $safe = $false
  }
  $got2 = Read-NotepadText
  Check ($got2 -eq $expected) 'notepad Shift+= commits +'
  Tap 0x27
  Start-Sleep -Milliseconds 200
  $got = Read-NotepadText
  if ($got -ne $expected) {
    Start-Sleep -Milliseconds 700
    $got = Read-NotepadText
  }
  Write-Host "[host] notepad text=[$got]"
  Write-Host "[host] expected     =[$expected]"
  Check ($got -eq $expected) 'notepad typed sequence matches (terms, 是的, ：, +)'
  $after = LogCount 'shift_click_toggle'
  Check ($after -eq $before) 'notepad Shift+symbols did not toggle the mode'
  foreach ($procId in @($p.Id, $winPid)) { try { Stop-Process -Id $procId -Force -ErrorAction Stop } catch {} }
  Remove-Item $file -ErrorAction SilentlyContinue
}

Run-SelfHostedChecks
if (-not $SkipNotepad) { Run-NotepadChecks }

Write-Host ('SUMMARY {"suite": "host-acceptance", "checks": ' + $script:checks + ', "failures": ' + $script:failures + ', "user_data_dir": "' + ($SCRIPT:USER_DIR -replace '\\', '\\\\') + '"}')
if ($KeepUserDir) {
  Write-Host "[host] kept user data dir: $SCRIPT:USER_DIR"
} else {
  Remove-Item Env:AUTOIME_RIME_USER_DATA_DIR -ErrorAction SilentlyContinue
  Remove-Item Env:AUTOIME_RESOURCES_DIR -ErrorAction SilentlyContinue
  Remove-Item -Recurse -Force $SCRIPT:USER_DIR -ErrorAction SilentlyContinue
}
if ($script:failures -gt 0) { exit 1 }
exit 0
