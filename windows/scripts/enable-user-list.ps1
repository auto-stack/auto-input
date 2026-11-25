# PLAN-069 / PLAN-078: add AutoIME to the current user's input method list and
# make it the DEFAULT zh-CN input method (the freshly installed IME is what the
# user expects to type with). Windows keeps the per-user order under
# HKCU\Software\Microsoft\CTF\SortOrder\AssemblyItem\0x00000804\<assembly>;
# the entry at 00000000 is the default for new threads. The previous default
# stays in the list (renumbered), so the user can switch back with Win+Space.
$cls = '{02E1E367-4389-429E-B13C-483EB5555D37}'
$prof = '{E8713320-7AE4-40D8-9850-AF2EA17AFD00}'
$up = 'HKCU:\Control Panel\International\User Profile\zh-Hans-CN'
if (-not (Test-Path $up)) { Write-Output "zh-Hans-CN language not present"; exit 1 }
Set-ItemProperty -Path $up -Name "0804:$cls$prof" -Type DWord -Value 1

$base = "HKCU:\Software\Microsoft\CTF\SortOrder\AssemblyItem\0x00000804\{34745C63-B2F0-4784-8B67-5E12C8701A31}"
if (-not (Test-Path $base)) { New-Item -Path $base -Force | Out-Null }

# Read the current order, drop AutoIME, then put it first.
$entries = @()
$keys = @(Get-ChildItem $base | Where-Object { $_.PSChildName -match '^\d{8}$' } | Sort-Object { [int]$_.PSChildName })
foreach ($key in $keys) {
  $p = Get-ItemProperty $key.PSPath
  $entries += [pscustomobject]@{ CLSID = $p.CLSID; Profile = $p.Profile; KeyboardLayout = $p.KeyboardLayout }
}
$others = @($entries | Where-Object { -not ($_.CLSID -eq $cls -and $_.Profile -eq $prof) })
$ordered = @([pscustomobject]@{ CLSID = $cls; Profile = $prof; KeyboardLayout = 0 }) + $others

$index = 0
foreach ($entry in $ordered) {
  $path = Join-Path $base ('{0:D8}' -f $index)
  if (-not (Test-Path $path)) { New-Item -Path $path -Force | Out-Null }
  Set-ItemProperty -Path $path -Name CLSID -Value $entry.CLSID
  Set-ItemProperty -Path $path -Name Profile -Value $entry.Profile
  $layout = 0
  if ($null -ne $entry.KeyboardLayout) { $layout = [int]$entry.KeyboardLayout }
  Set-ItemProperty -Path $path -Name KeyboardLayout -Type DWord -Value $layout
  $index++
}
Get-ChildItem $base | Where-Object {
  $_.PSChildName -match '^\d{8}$' -and [int]$_.PSChildName -ge $index
} | Remove-Item -Force
# PLAN-078 follow-up: the taskbar input indicator reads the per-user language
# profile (HKCU CTF TIP). Without this entry the badge falls back to the
# language name ("简体") even though the switcher already shows the icon from
# the machine-wide registration.
$tip = "HKCU:\Software\Microsoft\CTF\TIP\$cls\LanguageProfile\0x00000804\$prof"
New-Item -Path $tip -Force | Out-Null
$icon = Join-Path $PSScriptRoot 'autoime.ico'
if (-not (Test-Path $icon)) { $icon = Join-Path $PSScriptRoot 'autoime_ime.dll' }
Set-ItemProperty -Path $tip -Name Description -Value 'AutoInput'
Set-ItemProperty -Path $tip -Name IconFile -Value $icon
Set-ItemProperty -Path $tip -Name IconIndex -Type DWord -Value 0
Set-ItemProperty -Path $tip -Name Enable -Type DWord -Value 1

Write-Output "AutoInput added to the user input list and set as the zh-CN default"
