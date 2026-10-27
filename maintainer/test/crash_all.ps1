# crash_all.ps1 [-Minutes 20] [-Seed 1] [-Repos <Repo> ...]: crash_hunt.ps1 for each game on its
# usual route (the owner's saves where the route wants them), one after the other, then the verdicts
# again in one block. Twenty minutes each is about two and a half hours for all six; run it on a
# release candidate while the PC is free.
param([int]$Minutes = 20, [int]$Seed = 1, [string[]]$Repos = @())
. (Join-Path $PSScriptRoot "testkit.ps1")
$routes = [ordered]@{
  SSARecomp = "ssa_new"; GiantsRecomp = "giants_save"; SwapForceRecomp = "sf_save"
  TrapTeamRecomp = "tt_save"; SuperChargersRecomp = "sc_save"; ImaginatorsRecomp = "im_new"
}
$verdicts = @()
foreach ($repo in $routes.Keys) {
  if ($Repos.Count -and $repo -notin $Repos) { continue }
  "### $repo $(Get-Date -Format HH:mm:ss)"
  $v = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "crash_hunt.ps1") `
    -Repo $repo -Route $routes[$repo] -Seconds ($Minutes * 60) -Seed $Seed
  $v
  $verdicts += ($v | Select-Object -First 1)
}
"### all done $(Get-Date -Format HH:mm:ss)"
$verdicts
