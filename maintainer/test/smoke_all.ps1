# smoke_all.ps1 [-Repos <Repo> ...] [-Menus] [-Vulkan]: smoke.ps1 for each game with its usual route, one
# after the other (about 15 minutes for all six; -Menus opens and shoots every shared menu at the end of
# each route, a few minutes more), then the verdicts again in one block. Run it after anything that
# touches all games: a submodule bump, shared code, a release build.
# -Vulkan runs each route a second time with Graphics interface Vulkan and keeps its pictures in
# runs\smoke\<Repo>_vulkan next to the D3D12 ones in runs\smoke\<Repo>_d3d12: look at them side by side.
# A Vulkan fault often shows only in the picture (the false colours of Core of Light #76 left no log
# line), so the verdict alone doesn't cover it.
param([string[]]$Repos = @(), [switch]$Menus, [switch]$Vulkan)
# powershell -File hands "A,B" over as one string: split it here.
$Repos = @($Repos | ForEach-Object { $_ -split "," } | Where-Object { $_ })
. (Join-Path $PSScriptRoot "testkit.ps1")
$routes = [ordered]@{
  SSARecomp = "ssa_save"; GiantsRecomp = "giants_save"; SwapForceRecomp = "sf_save"
  TrapTeamRecomp = "tt_save"; SuperChargersRecomp = "sc_save"; ImaginatorsRecomp = "im_save"
}
function Keep-SmokePictures([string]$from, [string]$to) {
  # smoke.ps1 reuses runs\smoke\<Repo> for every run: copy the pictures and the log aside.
  if (Test-Path $to) { Remove-Item -Recurse -Force $to }
  New-Item -ItemType Directory -Force $to | Out-Null
  Get-ChildItem $from -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in ".png", ".log" } |
    ForEach-Object { Copy-Item $_.FullName $to -Force }
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
  if ($Vulkan) {
    $runs = Join-Path $env:LOCALAPPDATA "SkylandersDev\runs\smoke"
    Keep-SmokePictures (Join-Path $runs $repo) (Join-Path $runs "${repo}_d3d12")
    "### $repo on Vulkan $(Get-Date -Format HH:mm:ss)"
    $v = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "smoke.ps1") @smokeArgs `
      -Extra "--gpu_backend=vulkan"
    $v
    $verdicts += (($v -join "`n") + " (Vulkan)")
    Keep-SmokePictures (Join-Path $runs $repo) (Join-Path $runs "${repo}_vulkan")
  }
}
"### all done $(Get-Date -Format HH:mm:ss)"
$verdicts
