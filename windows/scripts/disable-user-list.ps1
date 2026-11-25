# PLAN-069 / PLAN-078: remove AutoIME from the current user's input method lists
# (mirror of enable-user-list.ps1; only touches AutoIME entries). The remaining
# zh-CN entries keep their relative order and are renumbered so Windows always
# sees a contiguous list with a valid default.
$cls = '{02E1E367-4389-429E-B13C-483EB5555D37}'
$prof = '{E8713320-7AE4-40D8-9850-AF2EA17AFD00}'
$up = 'HKCU:\Control Panel\International\User Profile\zh-Hans-CN'
if (Test-Path $up) {
  Remove-ItemProperty -Path $up -Name "0804:$cls$prof" -ErrorAction SilentlyContinue
}

$base = "HKCU:\Software\Microsoft\CTF\SortOrder\AssemblyItem\0x00000804\{34745C63-B2F0-4784-8B67-5E12C8701A31}"
if (Test-Path $base) {
  $entries = @()
  $keys = @(Get-ChildItem $base | Where-Object { $_.PSChildName -match '^\d{8}$' } | Sort-Object { [int]$_.PSChildName })
  foreach ($key in $keys) {
    $p = Get-ItemProperty $key.PSPath
    if ($p.CLSID -eq $cls -and $p.Profile -eq $prof) { continue }
    $entries += [pscustomobject]@{ CLSID = $p.CLSID; Profile = $p.Profile; KeyboardLayout = $p.KeyboardLayout }
  }
  $index = 0
  foreach ($entry in $entries) {
    $path = Join-Path $base ('{0:D8}' -f $index)
    New-Item -Path $path -Force | Out-Null
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
}
# PLAN-078 follow-up: drop the per-user language profile (the taskbar icon data).
if (Test-Path "HKCU:\Software\Microsoft\CTF\TIP\$cls") {
  Remove-Item -Path "HKCU:\Software\Microsoft\CTF\TIP\$cls" -Recurse -Force
}

Write-Output "AutoIME removed from user input lists"
