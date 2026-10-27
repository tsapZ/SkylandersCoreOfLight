# smoke_all.ps1 [-Repos <Repo> ...]: smoke.ps1 for each game with its usual route, one after the
# other (about 15 minutes for all six), then the verdicts again in one block. Run it after anything
# that touches all games: a submodule bump, shared code, a release build.
param([string[]]$Repos = @())
. (Join-Path $PSScriptRoot "testkit.ps1")
$routes = [ordered]@{
  SSARecomp = "ssa_new"; GiantsRecomp = "giants_save"; SwapForceRecomp = "sf_save"
  TrapTeamRecomp = "tt_save"; SuperChargersRecomp = "sc_save"; ImaginatorsRecomp = "im_new"
}
$verdicts = @()
foreach ($repo in $routes.Keys) {
  if ($Repos.Count -and $repo -notin $Repos) { continue }
  "### $repo $(Get-Date -Format HH:mm:ss)"
  $v = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "smoke.ps1") -Repo $repo -Route $routes[$repo]
  $v
  $verdicts += ($v -join "`n")
}
"### all done $(Get-Date -Format HH:mm:ss)"
$verdicts
