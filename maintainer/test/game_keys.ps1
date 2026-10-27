# game_keys.ps1 -Repo <Repo> -Keys "wait:ms,0x75,shot:name,..." [-Extra ...]: a game's release build
# on scratch user data, brought to the front, driven by real key presses (virtual key codes in hex),
# with window captures (the ImGui overlays included). Ends the game at the end. For what the input
# script's virtual controller can't reach (ImGui text fields, the keyboard's own bindings).
#
# Real keys go to whatever window is in front: run this only while nobody uses the PC. Before every
# key it checks the game is in front and stops at once if not.
param([Parameter(Mandatory = $true)][string]$Repo, [Parameter(Mandatory = $true)][string]$Keys,
      [string]$Extra = "", [string]$Res = "1280x720", [string]$Out = "", [string]$WorkDir = "",
      [string]$UserData = "")
. (Join-Path $PSScriptRoot "testkit.ps1")
$repoDir = Get-RepoDir $Repo
$exe = (Get-ChildItem "$repoDir\out\build\win-amd64-release\*recompiled.exe" | Select-Object -First 1).FullName
$ud = Join-Path $script:Work "userdata\keys_$Repo"
# Another user data folder only inside the work folder, never the real one.
if ($UserData -and $UserData.StartsWith($script:Work, [StringComparison]::OrdinalIgnoreCase)) { $ud = $UserData }
if (-not $Out) { $Out = Join-Path $script:Work "runs\keys\$Repo" }
New-Item -ItemType Directory -Force $Out, $ud | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class K8 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, int data, IntPtr extra);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  // The cursor to a spot in the window's client area (fractions of its size), then wheel notches.
  public static void Wheel(IntPtr h, double fx, double fy, int notches) {
    RECT r; GetClientRect(h, out r); POINT p; p.X = (int)(r.R * fx); p.Y = (int)(r.B * fy);
    ClientToScreen(h, ref p); SetCursorPos(p.X, p.Y); System.Threading.Thread.Sleep(120);
    for (int i = 0; i < Math.Abs(notches); i++) {
      mouse_event(0x0800, 0, 0, notches > 0 ? 120 : -120, IntPtr.Zero); System.Threading.Thread.Sleep(150);
    }
  }
  public static IntPtr Find(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
  public static void Front(IntPtr h) {
    IntPtr fg = GetForegroundWindow(); uint pid; uint ft = GetWindowThreadProcessId(fg, out pid); uint me = GetCurrentThreadId();
    AttachThreadInput(me, ft, true); keybd_event(0x12, 0, 0, IntPtr.Zero); keybd_event(0x12, 0, 2, IntPtr.Zero);
    SetForegroundWindow(h); AttachThreadInput(me, ft, false);
  }
  public static void Key(byte vk) {
    byte sc = (byte)MapVirtualKey(vk, 0);
    // The arrow keys are "extended": without the flag they arrive as the number pad's keys.
    uint ext = (vk >= 0x21 && vk <= 0x28) ? 1u : 0u;
    keybd_event(vk, sc, ext, IntPtr.Zero); System.Threading.Thread.Sleep(90); keybd_event(vk, sc, ext | 2u, IntPtr.Zero);
  }
}
"@
# An empty value ("--scene_size=") made the game drop the whole command line, --user_data_root
# too, and run on the owner's real data folder (2026-10-25, three Giants runs at the title; nothing
# changed there). Refused here, and the log's data folder line is checked below.
if ($Extra -match '=(\s|$)') { "REFUSED: an empty value in -Extra ($Extra); use e.g. --scene_size=off"; exit 3 }
$logDir = "$repoDir\out\build\win-amd64-release\logs"
$logsBefore = @(Get-ChildItem $logDir -File -ErrorAction SilentlyContinue | ForEach-Object Name)
$gameArgs = @("--fullscreen=false", "--resolution=$Res", "--audio_mute=true", "--portal_mode=software", "`"--user_data_root=$ud`"")
if ($Extra) { $gameArgs += $Extra.Split(" ") }
# -WorkDir: another folder with a rom\ in it (a scratch copy of the disc).
if (-not $WorkDir) { $WorkDir = $repoDir }
$p = Start-Process -FilePath $exe -ArgumentList $gameArgs -WorkingDirectory $WorkDir -PassThru
# The run's own log must name the scratch data folder within 15 s, else the game is stopped at once.
$safe = $false
for ($i = 0; $i -lt 60 -and -not $safe; $i++) {
  Start-Sleep -Milliseconds 250
  $new = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue | Where-Object { $logsBefore -notcontains $_.Name }
  foreach ($l in $new) {
    $line = Select-String -Path $l.FullName -Pattern "Data folder: " -SimpleMatch -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($line) {
      if ($line.Line -like "*--user_data_root*") { $safe = $true }
      else { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; "ABORTED: the game used $($line.Line)"; exit 4 }
    }
  }
}
if (-not $safe) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; "ABORTED: no data folder line in the log within 15 s"; exit 4 }
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 60 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 500; $h = [K8]::Find([uint32]$p.Id) }
if ($h -eq [IntPtr]::Zero) { "no window"; exit 1 }
foreach ($k in $Keys.Split(",")) {
  if ($k.StartsWith("wait:")) { Start-Sleep -Milliseconds ([int]$k.Substring(5)); continue }
  if ($k.StartsWith("shot:")) {
    $r = New-Object K8+RECT; [K8]::GetClientRect($h, [ref]$r) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap ([Math]::Max(1, $r.R - $r.L)), ([Math]::Max(1, $r.B - $r.T))
    $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc(); [K8]::PrintWindow($h, $dc, 3) | Out-Null
    $g.ReleaseHdc($dc); $g.Dispose(); $bmp.Save("$Out\$($k.Substring(5)).png"); $bmp.Dispose()
    continue
  }
  if ($k -eq "front") { [K8]::Front($h); Start-Sleep -Milliseconds 300; continue }
  # wheel:N (or wheel:N@fx@fy): N notches (minus = down) with the cursor at that spot (default the
  # middle). Only while the game is in front, as the keys.
  if ($k.StartsWith("wheel:")) {
    if ([K8]::GetForegroundWindow() -ne $h) { "ABORTED: game not in front before $k"; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; exit 2 }
    $w = $k.Substring(6).Split("@"); $fx = 0.5; $fy = 0.5
    if ($w.Count -ge 3) { $fx = [double]::Parse($w[1], [Globalization.CultureInfo]::InvariantCulture); $fy = [double]::Parse($w[2], [Globalization.CultureInfo]::InvariantCulture) }
    [K8]::Wheel($h, $fx, $fy, [int]$w[0]); Start-Sleep -Milliseconds 300; continue
  }
  # A key only goes to the game while it's in front. Another window in front means someone is
  # using the PC (keys went into Discord once): stop at once, never take the focus back.
  $fg = [K8]::GetForegroundWindow()
  if ($fg -ne $h) {
    $fpid = [uint32]0; [K8]::GetWindowThreadProcessId($fg, [ref]$fpid) | Out-Null
    $fname = (Get-Process -Id $fpid -ErrorAction SilentlyContinue).ProcessName
    "ABORTED: $fname was in front before key $k"
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    exit 2
  }
  [K8]::Key([byte][Convert]::ToInt32($k, 16)); Start-Sleep -Milliseconds 350
}
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
"done $Out"
