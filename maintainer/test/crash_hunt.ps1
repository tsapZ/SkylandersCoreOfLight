# crash_hunt.ps1 -Repo <Repo> -Route <name or file> [-Seconds 600] [-Seed 1] [-Save] [-KeepCache]
# The route to a level, then that many seconds of random play (wander.py: the stick, attacks,
# jumps, RT held now and then), a window shot every minute and the threads' call chains every three
# (a hang shows where it sits). -KeepCache keeps the last run's shader cache, to tell first-time
# shader compiles from real slow spots. Prints the verdict; the run stays in
# %LOCALAPPDATA%\SkylandersDev\runs\crash\<Repo>_<Seed>.
param([Parameter(Mandatory = $true)][string]$Repo, [Parameter(Mandatory = $true)][string]$Route,
      [int]$Seconds = 600, [int]$Seed = 1, [switch]$Save, [switch]$KeepCache, [string]$Extra = "")
. (Join-Path $PSScriptRoot "testkit.ps1")
$head = Get-RouteHeader $Route
$t = New-TestRun $Repo "crash" "${Repo}_$Seed" -Save:($Save -or $head.Save) -KeepCache:$KeepCache
$prefix = Expand-Route $Route $t.Run
$script = Join-Path $t.Run "wander.txt"
python (Join-Path $PSScriptRoot "wander.py") $prefix $Seconds $Seed $script | Out-Null
$shots = ((1..[int](($Seconds + 400) / 60)) | ForEach-Object { "wait 60; shot m{0:D2}" -f $_ }) -join "; "
$log = Invoke-Game $Repo $script $t.UserData $t.Run $shots $Extra
Get-Verdict $Repo $log
if ($log) {
  "  wander end: $(@(Select-String -Path $log -Pattern 'wander end').Count), last lines:"
  Get-Content $log -Tail 4 | ForEach-Object { "    " + $_.Substring(0, [Math]::Min(180, $_.Length)) }
}
