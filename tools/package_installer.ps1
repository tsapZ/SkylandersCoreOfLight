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
Then runs Inno Setup's ISCC with the version from CMakeLists.txt (COREOFLIGHT_VERSION) and packs
installer\Output\CoreOfLightPortable-<version>.zip (#46) from the same staged files.
Set COREOFLIGHT_SIGNING_THUMBPRINT to sign with a certificate in the current user's Personal store.
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

$iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if (-not (Test-Path $iscc)) { $iscc = Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe" }
if (-not (Test-Path $iscc)) { throw "ISCC.exe not found: install Inno Setup 6 (winget install JRSoftware.InnoSetup)" }
$compilerArgs = @("/DMyAppVersion=$version")
$thumbprint = $env:COREOFLIGHT_SIGNING_THUMBPRINT
if ($thumbprint) {
  if ($thumbprint -notmatch '\A[0-9a-f]{40}\z') { throw "Expected a 40-digit COREOFLIGHT_SIGNING_THUMBPRINT" }
  $signTool = (Get-Command signtool.exe -CommandType Application -ErrorAction Stop).Source
  $compilerArgs += '/DSignRelease'
  $compilerArgs += ('/sCoreOfLight=$q{0}$q sign /sha1 {1} /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $f' -f
    $signTool.Replace('$', '$$'), $thumbprint)
}

if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force (Join-Path $staging "games") | Out-Null
Copy-Item $exe $staging
# The launcher's fallback font (Noto Sans, SIL OFL) for where Windows' Segoe UI isn't there (Wine / Proton).
Copy-Item -Recurse (Join-Path $repo "fonts") (Join-Path $staging "fonts")

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

$output = Join-Path $repo "installer\Output"
$setupPath = Join-Path $output "CoreOfLightSetup-$version.exe"
& $iscc @compilerArgs (Join-Path $repo "installer\CoreOfLight.iss") | Select-Object -Last 3
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }
if ($thumbprint) {
  foreach ($file in $setupPath, (Join-Path $staging "coreoflight.exe")) {
    & $signTool verify /pa /tw $file
    if ($LASTEXITCODE -ne 0) { throw "Signature verification failed for $file" }
  }
}
Get-Item $setupPath | Select-Object Name, Length

# The portable launcher (#46, src/portable.h): the same files without the games' setups, with
# portable.txt next to the exe, as installer\Output\CoreOfLightPortable-<version>.zip. Entry names
# with forward slashes (Compress-Archive of Windows PowerShell 5 writes backslashes).
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zipPath = Join-Path $output "CoreOfLightPortable-$version.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath }
$zip = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
  foreach ($f in Get-ChildItem $staging -Recurse -File) {
    $name = $f.FullName.Substring($staging.Length + 1).Replace('\', '/')
    if ($name.StartsWith("games/")) { continue }
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, $name,
      [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
  }
  $writer = New-Object System.IO.StreamWriter($zip.CreateEntry("portable.txt").Open())
  $writer.Write("This copy of Skylanders: Core of Light runs from this folder and updates itself from the`r`n" +
                "release's portable ZIP instead of the setup. Keep this file next to coreoflight.exe.`r`n" +
                "Your figures, saves and settings are in Documents\Skylanders, as with the setup.`r`n")
  $writer.Dispose()
} finally {
  $zip.Dispose()
}
Get-Item $zipPath | Select-Object Name, Length
