# AutoIME host-level acceptance probe (PLAN-072 T-04, V-07).
#
# Drives a real desktop window through the TSF text service. Windows 11's
# Notepad is a packaged app whose window is not reachable by class name, so this
# script hosts a plain WinForms text box — an ordinary application window that
# receives keys through the same TSF path a Notepad/Edge user uses.
#
# Flow: focus the text box -> type the code -> press "1" -> read the committed
# text; if the phrase does not appear, cycle the input switcher (Win+Space) and
# retry. The phrase must already be added with the installed manager.
#
# Usage:
#   pwsh -NoProfile -File manager/tests/host-textbox.ps1 -Code vhost -Text 安装包验收词
param(
  [Parameter(Mandatory = $true)][string]$Code,
  [Parameter(Mandatory = $true)][string]$Text,
  [int]$MaxToggles = 6
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class HostWin32 {
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@

$VK_LWIN = 0x5B; $VK_SPACE = 0x20
$KEYUP = 0x0002

function SendKeysTo($text) { [System.Windows.Forms.SendKeys]::SendWait($text) }
function Tap([byte]$vk) {
  [HostWin32]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
  [HostWin32]::keybd_event($vk, 0, $KEYUP, [UIntPtr]::Zero)
}
function ToggleIme() {
  [HostWin32]::keybd_event($VK_LWIN, 0, 0, [UIntPtr]::Zero)
  Tap $VK_SPACE
  [HostWin32]::keybd_event($VK_LWIN, 0, $KEYUP, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 350
}

$form = New-Object System.Windows.Forms.Form
$form.Text = 'AutoIME host acceptance'
$form.Width = 640; $form.Height = 160
$form.StartPosition = 'CenterScreen'
$form.TopMost = $true
$box = New-Object System.Windows.Forms.TextBox
$box.Multiline = $true
$box.Dock = 'Fill'
$box.Font = New-Object System.Drawing.Font('Consolas', 14)
$form.Controls.Add($box)

$form.Add_Shown({
  $form.Activate()
  $box.Focus()
})
[void]$form.Show()
$form.Activate()
$box.Focus()
for ($i = 0; $i -lt 10; $i++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 100 }

$matched = $false
try {
  for ($i = 0; $i -lt $MaxToggles; $i++) {
    $box.Clear()
    $box.Focus()
    [System.Windows.Forms.Application]::DoEvents()
    SendKeysTo $Code
    Start-Sleep -Milliseconds 450
    SendKeysTo '1'
    Start-Sleep -Milliseconds 450
    [System.Windows.Forms.Application]::DoEvents()
    $got = $box.Text.Trim()
    Write-Host ("[host] toggle {0}: committed=[{1}]" -f $i, $got)
    if ($got -eq $Text) { $matched = $true; break }
    if ($got.Length -gt 0) { $box.Clear() }
    ToggleIme
    $form.Activate(); $box.Focus()
    for ($k = 0; $k -lt 5; $k++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 80 }
  }
} finally {
  $form.Close()
  $form.Dispose()
}

if (-not $matched) {
  Write-Host '[host] FAIL: AutoIME did not commit the synthetic phrase in a real window'
  exit 1
}
Write-Host ("[host] PASS: real window committed '{0}' for code '{1}'" -f $Text, $Code)
exit 0
