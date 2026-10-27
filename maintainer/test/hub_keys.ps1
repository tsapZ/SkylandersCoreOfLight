# hub_keys.ps1 -Docs <scratch documents> -Out <shot folder> -Keys "F,R,E,E,shot:name,...": the
# launcher (this repository's out\build\release) on a scratch Documents copy, always --offline (no
# releases, nothing downloaded or installed), driven by posted key presses (virtual key names or
# numbers), mouse steps and window captures at "shot:<name>". Waits are milliseconds (wait:700).
# rclick moves the real mouse: only while nobody uses the PC. -X 20000 puts the window off screen.
param([Parameter(Mandatory = $true)][string]$Docs, [Parameter(Mandatory = $true)][string]$Out,
      [Parameter(Mandatory = $true)][string]$Keys, [string]$Extra = "", [string]$Exe = "",
      [string]$Size = "", [int]$X = 0)
$exe = if ($Exe) { $Exe } else { (Resolve-Path (Join-Path $PSScriptRoot "..\..\out\build\release\coreoflight.exe")).Path }
if ($Docs -match '\\Documents\\?$') { throw "-Docs must be a scratch folder, not the real Documents" }
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W5 {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool repaint);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
New-Item -ItemType Directory -Force $Out | Out-Null
$p = Start-Process -FilePath $exe -ArgumentList "--offline --documents=$Docs $Extra" -PassThru
Start-Sleep -Seconds 3
for ($i = 0; $i -lt 30; $i++) { $p.Refresh(); $h = $p.MainWindowHandle; if ($h -ne [IntPtr]::Zero) { break }; Start-Sleep -Milliseconds 500 }
Start-Sleep -Seconds 1
if ($Size) { $wh = $Size.Split("x"); [W5]::MoveWindow($h, $X, 0, [int]$wh[0], [int]$wh[1], $true) | Out-Null; Start-Sleep -Milliseconds 800 }
function Shot([string]$name) {
  $r = New-Object W5+RECT; [W5]::GetClientRect($h, [ref]$r) | Out-Null
  $bmp = New-Object System.Drawing.Bitmap ([Math]::Max(1, $r.R - $r.L)), ([Math]::Max(1, $r.B - $r.T))
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  [W5]::PrintWindow($h, $dc, 3) | Out-Null
  $g.ReleaseHdc($dc); $g.Dispose(); $bmp.Save((Join-Path $Out "$name.png")); $bmp.Dispose()
}
$names = @{ "LEFT" = 37; "UP" = 38; "RIGHT" = 39; "DOWN" = 40; "ENTER" = 13; "ESC" = 27; "BACK" = 8; "SPACE" = 32; "TAB" = 9 }
foreach ($k in $Keys.Split(",")) {
  $k = $k.Trim()
  if ($k.StartsWith("shot:")) { Start-Sleep -Milliseconds 700; Shot $k.Substring(5); continue }
  if ($k.StartsWith("wait:")) { Start-Sleep -Milliseconds ([int]$k.Substring(5)); continue }
  # Mouse, in client pixels: move:x;y, click:x;y (left button), wheel:n (notches, + up).
  if ($k -match '^(move|click):(\d+);(\d+)$') {
    $lp = [IntPtr](([int]$Matches[3] -shl 16) -bor [int]$Matches[2])
    [W5]::PostMessage($h, 0x200, [IntPtr]0, $lp) | Out-Null
    Start-Sleep -Milliseconds 150
    if ($Matches[1] -eq "click") {
      [W5]::PostMessage($h, 0x201, [IntPtr]1, $lp) | Out-Null
      Start-Sleep -Milliseconds 100
      [W5]::PostMessage($h, 0x202, [IntPtr]0, $lp) | Out-Null
    }
    Start-Sleep -Milliseconds 400
    continue
  }
  # A real click (the cursor put there for it, then back): rclick:x;y in client pixels.
  if ($k -match '^rclick:(\d+);(\d+)$') {
    $saved = New-Object W5+POINT; [W5]::GetCursorPos([ref]$saved) | Out-Null
    $pt = New-Object W5+POINT; $pt.X = [int]$Matches[1]; $pt.Y = [int]$Matches[2]; [W5]::ClientToScreen($h, [ref]$pt) | Out-Null
    [W5]::SetCursorPos($pt.X, $pt.Y) | Out-Null; Start-Sleep -Milliseconds 150
    [W5]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80; [W5]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250; [W5]::SetCursorPos($saved.X, $saved.Y) | Out-Null
    Start-Sleep -Milliseconds 300
    continue
  }
  if ($k -match '^wheel:(-?\d+)$') {
    $delta = [int]$Matches[1] * 120
    [W5]::PostMessage($h, 0x20A, [IntPtr]([int64]($delta -band 0xFFFF) -shl 16), [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 400
    continue
  }
  $vk = if ($names.ContainsKey($k.ToUpper())) { $names[$k.ToUpper()] } elseif ($k -match '^\d+$') { [int]$k } else { [int][char]$k.ToUpper() }
  [W5]::PostMessage($h, 0x100, [IntPtr]$vk, [IntPtr]0) | Out-Null
  Start-Sleep -Milliseconds 120
  [W5]::PostMessage($h, 0x101, [IntPtr]$vk, [IntPtr]0xC0000001) | Out-Null
  Start-Sleep -Milliseconds 450
}
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
"done"
