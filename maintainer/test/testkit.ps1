# testkit.ps1: what the test scripts share (dot-source it). A test run never touches the real saves
# or figures: the game gets its own user data folder (with -Save a copy of the real saves) and the
# route's figures come from a copy of Documents\Skylanders\Figures.
#
# Layout: <Tools>\<Repo>\<Repo> for each game (found from this file's place, SKYLANDERS_TOOLS
# overrides it); runs go to SKYLANDERS_WORK, by default %LOCALAPPDATA%\SkylandersDev\runs.

$script:Tools = if ($env:SKYLANDERS_TOOLS) { $env:SKYLANDERS_TOOLS } else { (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path }
$script:Work = if ($env:SKYLANDERS_WORK) { $env:SKYLANDERS_WORK } else { Join-Path $env:LOCALAPPDATA "SkylandersDev" }
$script:Skylanders = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Skylanders"
$script:Games = [ordered]@{
  SSARecomp           = @{ Name = "Spyro's Adventure"; Exe = "ssarecompiled" }
  GiantsRecomp        = @{ Name = "Giants"; Exe = "giantsrecompiled" }
  SwapForceRecomp     = @{ Name = "Swap Force"; Exe = "swapforcerecompiled" }
  TrapTeamRecomp      = @{ Name = "Trap Team"; Exe = "trapteamrecompiled" }
  SuperChargersRecomp = @{ Name = "SuperChargers"; Exe = "superchargersrecompiled" }
  ImaginatorsRecomp   = @{ Name = "Imaginators"; Exe = "imaginatorsrecompiled" }
}
# The log lines that mean a crash, a hang or a broken start.
$script:TroublePattern = "invalid or unregistered|Unhandled|Crash: |exception C0|Fatal|assert|Could not set"

function Get-RepoDir([string]$Repo) {
  if (-not $script:Games.Contains($Repo)) { throw "unknown game repository '$Repo'" }
  Join-Path $script:Tools "$Repo\$Repo"
}

# A fresh run folder and user data folder for one test; -Save copies the real saves (not the shader
# cache or the save backups), so the run starts where the owner is.
function New-TestRun([string]$Repo, [string]$Tool, [string]$Tag, [switch]$Save, [switch]$KeepCache) {
  $run = Join-Path $script:Work "runs\$Tool\$Tag"
  $ud = Join-Path $script:Work "userdata\$Tool`_$Repo"
  $cache = Join-Path $script:Work "userdata\cache_$Repo"
  if ($KeepCache -and (Test-Path "$ud\cache")) {
    if (Test-Path $cache) { Remove-Item -Recurse -Force $cache }
    Move-Item "$ud\cache" $cache
  }
  foreach ($d in $run, $ud) {
    if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    New-Item -ItemType Directory -Force $d | Out-Null
  }
  if ($KeepCache -and (Test-Path $cache)) { Move-Item $cache "$ud\cache" }
  if ($Save) {
    $real = Join-Path $script:Skylanders $script:Games[$Repo].Name
    if (-not (Test-Path $real)) { throw "no saves at $real" }
    Get-ChildItem $real -Directory | Where-Object { $_.Name -notin "cache", "save-backup" } |
      ForEach-Object { Copy-Item -Recurse $_.FullName (Join-Path $ud $_.Name) }
  }
  @{ Run = $run; UserData = $ud }
}

function Get-RoutePath([string]$Route) {
  $path = if (Test-Path $Route) { $Route } else { Join-Path $PSScriptRoot "routes\$Route.txt" }
  if (-not (Test-Path $path)) { throw "no route '$Route'" }
  $path
}

# The route (an input script, see thirdparty/skylanders-portal/src/dev/input_script.h in a game)
# with {figures} pointing at a fresh copy of the figure folder, written into the run folder. -Then
# appends a second route (e.g. menus) in place of the first one's quit.
function Expand-Route([string]$Route, [string]$Run, [string]$Then = "") {
  $path = Get-RoutePath $Route
  $figures = Join-Path $Run "figures"
  $real = Join-Path $script:Skylanders "Figures"
  if (Test-Path $real) { Copy-Item -Recurse $real $figures } else { New-Item -ItemType Directory -Force $figures | Out-Null }
  # Comment lines are left out: the game's script reader splits at every ';', comments too.
  $lines = @(Get-Content $path | Where-Object { $_ -notmatch '^\s*#' })
  if ($Then) {
    $lines = @($lines | Where-Object { $_.Trim() -ne "quit" }) + @(Get-Content (Get-RoutePath $Then) | Where-Object { $_ -notmatch '^\s*#' })
  }
  $lines = $lines | ForEach-Object { $_ -replace '\{figures\}', $figures }
  $out = Join-Path $Run "route.txt"
  [IO.File]::WriteAllLines($out, [string[]]$lines)
  $out
}

# A route's header lines: "# save: yes", "# shots: ..." (comma-separated there),
# "# expect: <log text>, ..." (log lines the run must have, e.g. a menu that opened) and
# "# reject: <log text>, ..." (log lines it must not have, e.g. a fixed bug's message).
function Get-RouteHeader([string]$Route, [string]$Then = "") {
  $head = @{ Save = $false; Shots = ""; Expect = @(); Reject = @() }
  foreach ($r in @($Route, $Then) | Where-Object { $_ }) {
    foreach ($line in Get-Content (Get-RoutePath $r)) {
      if ($line -match '^#\s*save:\s*yes') { $head.Save = $true }
      if ($line -match '^#\s*shots:\s*(.*)$') { $head.Shots = $Matches[1] -replace ',', ';' }
      if ($line -match '^#\s*expect:\s*(.*)$') { $head.Expect += @($Matches[1].Split(",") | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
      if ($line -match '^#\s*reject:\s*(.*)$') { $head.Reject += @($Matches[1].Split(",") | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
    }
  }
  $head
}

# Window shots at the route's own "log shot <name>" marks, one second after each: the times come
# from adding up the commands before it (wait, tap 0.12 s, hold / stick their ms, fps its seconds).
# Empty without marks.
function Get-ScriptShots([string]$Script) {
  $t = 0.0; $last = 0.0; $steps = @()
  foreach ($cmd in ((Get-Content $Script) -join ";").Split(";")) {
    $w = $cmd.Trim() -split '\s+'
    switch ($w[0]) {
      "wait" { $t += [double]::Parse($w[1], [Globalization.CultureInfo]::InvariantCulture) }
      "tap" { $t += 0.12 }
      "key" { $t += 0.12 }
      "hold" { $t += [double]$w[2] / 1000 }
      "stick" { $t += [double]$w[3] / 1000 }
      "fps" { $t += [double]$w[1] }
      "log" {
        if ($w.Count -ge 3 -and $w[1] -eq "shot") {
          $at = $t + 1.0
          $steps += ("wait {0}; shot {1}" -f ([Math]::Round($at - $last, 2)).ToString([Globalization.CultureInfo]::InvariantCulture), $w[2])
          $last = $at
        }
      }
    }
  }
  $steps -join "; "
}

# The longest a route can take: its waits, holds and taps, and each waitfor's limit.
function Get-ScriptLength([string]$Script) {
  $t = 0.0
  foreach ($cmd in ((Get-Content $Script) -join ";").Split(";")) {
    $w = $cmd.Trim() -split '\s+'
    switch ($w[0]) {
      "wait" { $t += [double]::Parse($w[1], [Globalization.CultureInfo]::InvariantCulture) }
      "waitfor" { if ($w.Count -ge 3) { $t += [double]::Parse($w[2], [Globalization.CultureInfo]::InvariantCulture) } }
      "tap" { $t += 0.12 }
      "key" { $t += 0.12 }
      "hold" { $t += [double]$w[2] / 1000 }
      "stick" { $t += [double]$w[3] / 1000 }
      "fps" { $t += [double]$w[1] }
    }
  }
  $t
}

# A window picture of the running game (as run_scripted.ps1's "shot": 640 wide, overlays included).
function Save-GameWindow([System.Diagnostics.Process]$Process, [string]$Path) {
  if (-not ("TestkitWindow" -as [type])) {
    Add-Type -AssemblyName System.Drawing
    Add-Type @"
using System; using System.Runtime.InteropServices;
public static class TestkitWindow {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
  }
  $Process.Refresh()
  $hwnd = $Process.MainWindowHandle
  if ($hwnd -eq [IntPtr]::Zero) { return }
  $rect = New-Object TestkitWindow+RECT
  [TestkitWindow]::GetClientRect($hwnd, [ref]$rect) | Out-Null
  $bmp = New-Object System.Drawing.Bitmap ([math]::Max(1, $rect.R - $rect.L)), ([math]::Max(1, $rect.B - $rect.T))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [TestkitWindow]::PrintWindow($hwnd, $hdc, 3) | Out-Null
  $g.ReleaseHdc($hdc); $g.Dispose()
  $small = New-Object System.Drawing.Bitmap $bmp, 640, ([int](640 * $bmp.Height / $bmp.Width))
  $small.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
  $small.Dispose(); $bmp.Dispose()
}

# The game started through its tools\run_scripted.ps1 (window shots on a timeline), muted (only the
# samples are zeroed: SDL and the sound driver run as usual), then its newest log copied to the run.
# -Build: the build folder's name after win-amd64- (release, or devkit for the developer kit's build).
function Invoke-Game([string]$Repo, [string]$Script, [string]$UserData, [string]$Run, [string]$Shots,
                     [string]$Extra = "", [int]$QuitTimeout = 180, [string]$Build = "release",
                     [switch]$ShotsFromLog) {
  # The game of a run just before can still be closing (saving, its threads ending): up to 60 s for it
  # to go before calling it another run (2026-11-12: three runs of a chain were lost to this).
  for ($i = 0; $i -lt 120 -and (Get-Process *recompiled* -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 500 }
  if (Get-Process *recompiled* -ErrorAction SilentlyContinue) { throw "a game is already running" }
  $repoDir = Get-RepoDir $Repo
  $logDir = Join-Path $repoDir "out\build\win-amd64-$Build\logs"
  $before = Get-Date
  $gameArgs = "--user_data_root=$UserData --portal_mode=software --fullscreen=false --resolution=1920x1080 " +
          "--audio_mute=true --skip_intro=true --input_script=$Script $Extra"
  # RULES.md: a setting with an empty value can make the game drop its command line and run on the real data.
  if ($gameArgs -match '(^|\s)--\w+=(\s|$)') { throw "a setting with an empty value: $($Matches[0])" }
  # The shots run alongside the route; once they are done the game keeps going until the route's
  # quit, at most $QuitTimeout more seconds (then it is stopped and the verdict says NO QUIT).
  # SDL keeps the display on while a game runs (ES_DISPLAY_REQUIRED), which switches monitors back
  # on that were turned off for a night of test runs; the games inherit this hint.
  $env:SDL_VIDEO_ALLOW_SCREENSAVER = "1"
  Push-Location $repoDir
  # The shared runner in the game's skylanders-portal; an older checkout still has its own copy.
  $runner = ".\thirdparty\skylanders-portal\tools\run_scripted.ps1"
  if (-not (Test-Path $runner)) { $runner = ".\tools\run_scripted.ps1" }
  # -ShotsFromLog: no timeline; each picture is taken a second after the game logs the route's
  # "log shot <name>" line, so a waitfor before it (any length) can't put it off (2026-11-12).
  $timeline = if ($ShotsFromLog) { "" } else { $Shots }
  try { & $runner -OutDir $Run -GameArgs $gameArgs -Shots $timeline -Config $Build -KeepRunning 2>&1 | Out-File -Encoding utf8 (Join-Path $Run "run.txt") }
  finally { Pop-Location }
  $exe = $script:Games[$Repo].Exe
  $limit = 2 * $QuitTimeout
  if ($ShotsFromLog) { $limit += 2 * [int](Get-ScriptLength $Script) }
  $reader = $null
  try {
    for ($i = 0; $i -lt $limit -and (Get-Process $exe -ErrorAction SilentlyContinue); $i++) {
      Start-Sleep -Milliseconds 500
      if (-not $ShotsFromLog) { continue }
      if (-not $reader) {
        $file = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $before } |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $file) { continue }
        $stream = [IO.File]::Open($file.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $reader = New-Object IO.StreamReader($stream)
      }
      while ($null -ne ($line = $reader.ReadLine())) {
        if ($line -match 'Input script: shot (\S+)') {
          Start-Sleep -Milliseconds 1000
          $game = Get-Process $exe -ErrorAction SilentlyContinue | Select-Object -First 1
          if ($game) { Save-GameWindow $game (Join-Path $Run "$($Matches[1]).png") }
        }
      }
    }
  } finally {
    if ($reader) { $reader.Dispose() }
  }
  Get-Process $exe -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false
  Start-Sleep -Milliseconds 500
  $log = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $before } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $log) { return $null }
  $copy = Join-Path $Run "game.log"
  Copy-Item $log.FullName $copy
  $copy
}

# One line per run: crash lines, whether the script reached its quit, the FPS samples' range, any
# expected log line (Get-RouteHeader's Expect) that never came and any rejected one (Reject) that
# did.
function Get-Verdict([string]$Repo, [string]$Log, [string[]]$Expect = @(), [string[]]$Reject = @()) {
  if (-not $Log) { return "${Repo}: NO LOG" }
  $lines = Get-Content $Log
  $trouble = @($lines | Select-String $script:TroublePattern)
  $quit = @($lines | Select-String "Input script: quit").Count
  $missing = @($Expect | Where-Object { -not ($lines | Select-String -SimpleMatch $_ -Quiet) })
  $rejected = @($Reject | Where-Object { $lines | Select-String -SimpleMatch $_ -Quiet })
  $fps = @($lines | Select-String "guest FPS" | ForEach-Object { [double]($_.Line -replace '^.*Input script: ([\d.]+) guest FPS.*$', '$1') })
  $range = if ($fps.Count) { "fps {0:N0}-{1:N0}" -f ($fps | Measure-Object -Minimum).Minimum, ($fps | Measure-Object -Maximum).Maximum } else { "no fps samples" }
  $state = if ($trouble.Count) { "TROUBLE" } elseif ($rejected.Count) { "REJECTED" } elseif ($missing.Count) { "MISSING" } elseif ($quit) { "ok" } else { "NO QUIT" }
  $text = "{0}: {1} ({2} log lines, {3} trouble lines, {4})" -f $Repo, $state, $lines.Count, $trouble.Count, $range
  foreach ($t in $trouble | Select-Object -First 3) { $text += "`n    " + $t.Line.Substring(0, [Math]::Min(200, $t.Line.Length)) }
  foreach ($r in $rejected) { $text += "`n    in the log, but must not be: $r" }
  foreach ($m in $missing) { $text += "`n    never in the log: $m" }
  $text
}
