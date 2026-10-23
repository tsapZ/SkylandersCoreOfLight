<#
.SYNOPSIS
Builds the launcher's installer, installer\Output\CoreOfLightSetup-<version>.exe (#17, step 1).

.DESCRIPTION
Stages into installer\staging (not in the repository):
- out\build\release\coreoflight.exe (build it first),
- the Visual C++ runtime DLLs it needs, from this PC's Visual Studio (VC\Redist\MSVC\*\x64),
- with -Games, the newest <Repo>Setup-*.exe of each game from the repositories next to this one
  (<Tools>\<Repo>\<Repo>\installer\Output, the developer layout FindSetup knows too), so the
  installed launcher can set the games up without a "games" folder of its own.
Then runs Inno Setup's ISCC with the version from CMakeLists.txt (COREOFLIGHT_VERSION).
#>
param([switch]$Games)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$staging = Join-Path $repo "installer\staging"

$cmake = Get-Content -Raw (Join-Path $repo "CMakeLists.txt")
if ($cmake -notmatch 'set\(COREOFLIGHT_VERSION "([^"]+)"\)') { throw "COREOFLIGHT_VERSION not found in CMakeLists.txt" }
$version = $Matches[1]

$exe = Join-Path $repo "out\build\release\coreoflight.exe"
if (-not (Test-Path $exe)) { throw "$exe is missing: build the release first" }

if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force (Join-Path $staging "games") | Out-Null
Copy-Item $exe $staging

# The CRT DLLs of the newest MSVC redist folder of any Visual Studio 2022 edition.
$crt = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" -Directory -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending | Select-Object -First 1
if (-not $crt) { throw "No Visual C++ redist folder (VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT) found" }
foreach ($dll in "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll") {
  Copy-Item (Join-Path $crt.FullName $dll) $staging
}

if ($Games) {
  $tools = Split-Path -Parent (Split-Path -Parent $repo)
  foreach ($r in "SSARecomp", "GiantsRecomp", "SwapForceRecomp", "TrapTeamRecomp", "SuperChargersRecomp", "ImaginatorsRecomp") {
    $setup = Get-ChildItem (Join-Path $tools "$r\$r\installer\Output") -Filter "${r}Setup-*.exe" -ErrorAction SilentlyContinue |
      Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($setup) { Copy-Item $setup.FullName (Join-Path $staging "games") } else { Write-Warning "$r : no setup found" }
  }
}

$iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $iscc)) { $iscc = Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe" }
if (-not (Test-Path $iscc)) { throw "ISCC.exe not found: install Inno Setup 6 (winget install JRSoftware.InnoSetup)" }
& $iscc "/DMyAppVersion=$version" (Join-Path $repo "installer\CoreOfLight.iss") | Select-Object -Last 3
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }
Get-Item (Join-Path $repo "installer\Output\CoreOfLightSetup-$version.exe") | Select-Object Name, Length
