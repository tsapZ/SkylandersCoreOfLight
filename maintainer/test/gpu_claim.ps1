# gpu_claim.ps1 (dot-sourced): the graphics card shared with another program on the maintainer's PC (a picture generator
# that needs nearly all of its memory; a game next to it slows both down many times). A claim file names who uses the
# card. Its path comes from $env:SKYLANDERS_GPU_CLAIM_FILE or the first line of
# %LOCALAPPDATA%\SkylandersDev\gpu_claim_file.txt; without either (or when its folder isn't there) nothing here does
# anything.
#
# Enter-GpuClaim waits while a claim that isn't Skylanders' is there, then writes one for this run and returns the file
# (or "" when a Skylanders chain or run already holds the card, or there is no claim file to use); Exit-GpuClaim removes
# the claim this process wrote. A Skylanders claim whose process is gone is cleared. (Owner 2026-11-24: every own run,
# short game tests too, writes the file and removes it afterwards; a morning's sweep had run without it.)

function Get-GpuClaimFile() {
  if ($env:SKYLANDERS_GPU_CLAIM_FILE) { return $env:SKYLANDERS_GPU_CLAIM_FILE }
  $setting = Join-Path $env:LOCALAPPDATA "SkylandersDev\gpu_claim_file.txt"
  if (Test-Path $setting) {
    $line = Get-Content $setting -TotalCount 1 -ErrorAction SilentlyContinue
    if ($line) { return $line.Trim() }
  }
  return ""
}

function Enter-GpuClaim([string]$Name) {
  $file = Get-GpuClaimFile
  if (-not $file -or -not (Test-Path (Split-Path $file))) { return "" }
  $said = $false
  while ($true) {
    if (Test-Path $file) {
      $text = "?"
      try { $text = Get-Content -Raw $file -ErrorAction Stop } catch {}
      if ($text -match "^Skylanders .*\(pid (\d+)\)") {
        # A Skylanders chain or run holds the card: one game at a time is its own business (chain_turn.ps1).
        if (Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue) { return "" }
        Remove-Item $file -Force -ErrorAction SilentlyContinue  # left by a Skylanders run that is gone
        continue
      }
      if (-not $said) { Write-Host "waiting for the graphics card: $($text.Trim())"; $said = $true }
      Start-Sleep -Seconds 20
      continue
    }
    $line = "Skylanders game test: $Name, since $(Get-Date -Format 'HH:mm'), end open (pid $PID)"
    try {
      $stream = [IO.File]::Open($file, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
      $bytes = [Text.Encoding]::UTF8.GetBytes($line + "`r`n")
      $stream.Write($bytes, 0, $bytes.Length)
      $stream.Dispose()
      return $file
    } catch {
      Start-Sleep -Seconds 2  # someone created it in between: look again
    }
  }
}

function Exit-GpuClaim([string]$File) {
  if (-not $File -or -not (Test-Path $File)) { return }
  $text = ""
  try { $text = Get-Content -Raw $File -ErrorAction Stop } catch {}
  if ($text -match "\(pid $PID\)") { Remove-Item $File -Force -ErrorAction SilentlyContinue }
}
