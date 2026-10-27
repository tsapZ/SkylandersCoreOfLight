# smoke.ps1 -Repo <Repo> -Route <name in routes\ or a file> [-Shots "..."] [-Save] [-Extra "<game args>"]
# One scripted run of the built game (out\build\win-amd64-release) on its own user data: the route's
# "# save:" line (or -Save) copies the real saves first, its "# shots:" line (or -Shots) times the
# window shots. Prints one verdict line; the run (shots, log, the expanded route) stays in
# %LOCALAPPDATA%\SkylandersDev\runs\smoke\<Repo>.
param([Parameter(Mandatory = $true)][string]$Repo, [Parameter(Mandatory = $true)][string]$Route,
      [string]$Shots = "", [switch]$Save, [string]$Extra = "")
. (Join-Path $PSScriptRoot "testkit.ps1")
$head = Get-RouteHeader $Route
if (-not $Shots) { $Shots = $head.Shots }
$t = New-TestRun $Repo "smoke" $Repo -Save:($Save -or $head.Save)
$script = Expand-Route $Route $t.Run
$log = Invoke-Game $Repo $script $t.UserData $t.Run $Shots $Extra
Get-Verdict $Repo $log
