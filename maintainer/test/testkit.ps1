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

# The route (an input script, see src/dev/input_script.h in a game) with {figures} pointing at a
# fresh copy of the figure folder, written into the run folder.
function Expand-Route([string]$Route, [string]$Run) {
  $path = if (Test-Path $Route) { $Route } else { Join-Path $PSScriptRoot "routes\$Route.txt" }
  if (-not (Test-Path $path)) { throw "no route '$Route'" }
  $figures = Join-Path $Run "figures"
  $real = Join-Path $script:Skylanders "Figures"
  if (Test-Path $real) { Copy-Item -Recurse $real $figures } else { New-Item -ItemType Directory -Force $figures | Out-Null }
  # Comment lines are left out: the game's script reader splits at every ';', comments too.
  $lines = Get-Content $path | Where-Object { $_ -notmatch '^\s*#' } | ForEach-Object { $_ -replace '\{figures\}', $figures }
  $out = Join-Path $Run "route.txt"
  [IO.File]::WriteAllLines($out, [string[]]$lines)
  $out
}

# A route's "# save:" and "# shots:" header lines (shots are comma-separated there).
function Get-RouteHeader([string]$Route) {
  $path = if (Test-Path $Route) { $Route } else { Join-Path $PSScriptRoot "routes\$Route.txt" }
  $head = @{ Save = $false; Shots = "" }
  foreach ($line in Get-Content $path) {
    if ($line -match '^#\s*save:\s*yes') { $head.Save = $true }
    if ($line -match '^#\s*shots:\s*(.*)$') { $head.Shots = $Matches[1] -replace ',', ';' }
  }
  $head
}

# The game started through its tools\run_scripted.ps1 (window shots on a timeline), muted (only the
# samples are zeroed: SDL and the sound driver run as usual), then its newest log copied to the run.
function Invoke-Game([string]$Repo, [string]$Script, [string]$UserData, [string]$Run, [string]$Shots,
                     [string]$Extra = "", [int]$QuitTimeout = 180) {
  if (Get-Process *recompiled* -ErrorAction SilentlyContinue) { throw "a game is already running" }
  $repoDir = Get-RepoDir $Repo
  $logDir = Join-Path $repoDir "out\build\win-amd64-release\logs"
  $before = Get-Date
  $gameArgs = "--user_data_root=$UserData --portal_mode=software --fullscreen=false --resolution=1920x1080 " +
          "--audio_mute=true --skip_intro=true --input_script=$Script $Extra"
  # The shots run alongside the route; once they are done the game keeps going until the route's
  # quit, at most $QuitTimeout more seconds (then it is stopped and the verdict says NO QUIT).
  Push-Location $repoDir
  # The shared runner in the game's skylanders-portal; an older checkout still has its own copy.
  $runner = ".\thirdparty\skylanders-portal\tools\run_scripted.ps1"
  if (-not (Test-Path $runner)) { $runner = ".\tools\run_scripted.ps1" }
  try { & $runner -OutDir $Run -GameArgs $gameArgs -Shots $Shots -KeepRunning 2>&1 | Out-File -Encoding utf8 (Join-Path $Run "run.txt") }
  finally { Pop-Location }
  $exe = $script:Games[$Repo].Exe
  for ($i = 0; $i -lt 2 * $QuitTimeout -and (Get-Process $exe -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 500 }
  Get-Process $exe -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false
  Start-Sleep -Milliseconds 500
  $log = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $before } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $log) { return $null }
  $copy = Join-Path $Run "game.log"
  Copy-Item $log.FullName $copy
  $copy
}

# One line per run: crash lines, whether the script reached its quit, the FPS samples' range.
function Get-Verdict([string]$Repo, [string]$Log) {
  if (-not $Log) { return "${Repo}: NO LOG" }
  $lines = Get-Content $Log
  $trouble = @($lines | Select-String $script:TroublePattern)
  $quit = @($lines | Select-String "Input script: quit").Count
  $fps = @($lines | Select-String "guest FPS" | ForEach-Object { [double]($_.Line -replace '^.*Input script: ([\d.]+) guest FPS.*$', '$1') })
  $range = if ($fps.Count) { "fps {0:N0}-{1:N0}" -f ($fps | Measure-Object -Minimum).Minimum, ($fps | Measure-Object -Maximum).Maximum } else { "no fps samples" }
  $state = if ($trouble.Count) { "TROUBLE" } elseif ($quit) { "ok" } else { "NO QUIT" }
  $text = "{0}: {1} ({2} log lines, {3} trouble lines, {4})" -f $Repo, $state, $lines.Count, $trouble.Count, $range
  foreach ($t in $trouble | Select-Object -First 3) { $text += "`n    " + $t.Line.Substring(0, [Math]::Min(200, $t.Line.Length)) }
  $text
}
