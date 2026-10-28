# smoke.ps1 -Repo <Repo> -Route <name in routes\ or a file> [-Then <route>] [-Shots "..."] [-Save]
#           [-Extra "<game args>"] [-Build devkit]
# One scripted run of the built game (out\build\win-amd64-release) on its own user data: the route's
# "# save:" line (or -Save) copies the real saves first. -Then appends a second route in place of the
# first one's quit (e.g. -Then menus). Window shots: -Shots, else the route's "log shot <name>" marks,
# else its "# shots:" line. Prints one verdict line, counting the "# expect:" log lines too; the run
# (shots, log, the expanded route) stays in %LOCALAPPDATA%\SkylandersDev\runs\smoke\<Repo>.
param([Parameter(Mandatory = $true)][string]$Repo, [Parameter(Mandatory = $true)][string]$Route,
      [string]$Then = "", [string]$Shots = "", [switch]$Save, [string]$Extra = "", [string]$Build = "release")
. (Join-Path $PSScriptRoot "testkit.ps1")
$head = Get-RouteHeader $Route $Then
$t = New-TestRun $Repo "smoke" $Repo -Save:($Save -or $head.Save)
$script = Expand-Route $Route $t.Run $Then
if (-not $Shots) { $Shots = Get-ScriptShots $script }
if (-not $Shots) { $Shots = $head.Shots }
$log = Invoke-Game $Repo $script $t.UserData $t.Run $Shots $Extra -Build $Build
Get-Verdict $Repo $log $head.Expect
