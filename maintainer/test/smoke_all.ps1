# smoke_all.ps1 [-Repos <Repo> ...] [-Menus]: smoke.ps1 for each game with its usual route, one after
# the other (about 15 minutes for all six; -Menus opens and shoots every shared menu at the end of
# each route, a few minutes more), then the verdicts again in one block. Run it after anything that
# touches all games: a submodule bump, shared code, a release build.
param([string[]]$Repos = @(), [switch]$Menus)
. (Join-Path $PSScriptRoot "testkit.ps1")
$routes = [ordered]@{
  SSARecomp = "ssa_new"; GiantsRecomp = "giants_save"; SwapForceRecomp = "sf_save"
  TrapTeamRecomp = "tt_save"; SuperChargersRecomp = "sc_save"; ImaginatorsRecomp = "im_new"
}
$verdicts = @()
foreach ($repo in $routes.Keys) {
  if ($Repos.Count -and $repo -notin $Repos) { continue }
  "### $repo $(Get-Date -Format HH:mm:ss)"
  # No empty argument: powershell -File would read it as a missing value.
  $smokeArgs = @("-Repo", $repo, "-Route", $routes[$repo])
  if ($Menus) { $smokeArgs += @("-Then", "menus") }
  $v = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "smoke.ps1") @smokeArgs
  $v
  $verdicts += ($v -join "`n")
}
"### all done $(Get-Date -Format HH:mm:ss)"
$verdicts
