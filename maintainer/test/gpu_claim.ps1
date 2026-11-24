# gpu_claim.ps1 (dot-sourced): the graphics card shared with another program on the maintainer's PC (a picture generator
# that needs nearly all of its memory; a game next to it slows both down many times). Two files in one folder:
#
#   the claim: who has the card now, one line: "Skylanders (Claude Code, <who>): <what>, seit HH:MM, voraussichtlich
#              bis HH:MM (pid N)"
#   the wait:  who wants it next: "Skylanders (Claude Code, <who>): wartet seit HH:MM (pid N)"
#
# Paths: $env:SKYLANDERS_GPU_CLAIM_FILE / $env:SKYLANDERS_GPU_WAIT_FILE, or the first and second line of
# %LOCALAPPDATA%\SkylandersDev\gpu_claim_file.txt; without a claim file (or when its folder isn't there) nothing here
# does anything. Lines starting with "Skylanders" are ours; one whose process is gone is cleared.
#
# The rules (owner 2026-11-24, agreed with the other program):
#   1. Before any use of the card, short game tests too: wait while another program's claim is there; when another
#      program's wait file is there, it goes first.
#   2. Starting: write the claim, remove our own wait file. Afterwards remove the claim, on an abort too.
#   3. Waiting: write the wait file with our name (the other program then ends its block and starts no new one).
#   4. Holding the card while the other program's wait file appears: finish the running test, start no new one, give
#      the card back.
#
# Enter-GpuClaim waits by these rules, then writes a claim for this run and returns the file ("" when one of ours
# already holds the card: a chain, whose Wait-Turn (debug\kit\chain_turn.ps1) is the gate between its tests, or the
# watch below while no other program waits). Exit-GpuClaim removes the claim this process wrote. The watch
# (debug\kit\gpu_watch.ps1) writes a claim for any game of ours that runs without one.

function Get-GpuSettingLine([int]$Index) {
  $setting = Join-Path $env:LOCALAPPDATA "SkylandersDev\gpu_claim_file.txt"
  if (-not (Test-Path $setting)) { return "" }
  $lines = @(Get-Content $setting -ErrorAction SilentlyContinue)
  if ($lines.Count -gt $Index -and $lines[$Index]) { return $lines[$Index].Trim() }
  return ""
}

function Get-GpuClaimFile() {
  if ($env:SKYLANDERS_GPU_CLAIM_FILE) { return $env:SKYLANDERS_GPU_CLAIM_FILE }
  return Get-GpuSettingLine 0
}

function Get-GpuWaitFile() {
  if ($env:SKYLANDERS_GPU_WAIT_FILE) { return $env:SKYLANDERS_GPU_WAIT_FILE }
  return Get-GpuSettingLine 1
}

# What one of the two files says: Exists, Text, Ours (a Skylanders line), Pid, Watch (written by gpu_watch.ps1). A line
# of ours whose process is gone is removed and reads as not there.
function Read-GpuNote([string]$File) {
  $note = [pscustomobject]@{ Exists = $false; Text = ""; Ours = $false; Pid = 0; Watch = $false }
  if (-not $File -or -not (Test-Path $File)) { return $note }
  $text = "?"
  try { $text = (Get-Content -Raw $File -ErrorAction Stop).Trim() } catch {}
  if (-not $text) {
    # Empty: one of the two sides is writing it this moment; count it as there.
    $text = "?"
  }
  $note.Exists = $true
  $note.Text = $text
  if ($text -match "^Skylanders") {
    $note.Ours = $true
    $note.Watch = $text -match "^Skylanders \(Claude Code, Wache\)"
    if ($text -match "\(pid (\d+)\)") {
      $note.Pid = [int]$Matches[1]
      if (-not (Get-Process -Id $note.Pid -ErrorAction SilentlyContinue)) {
        Remove-Item $File -Force -ErrorAction SilentlyContinue
        $note.Exists = $false
      }
    }
  }
  return $note
}

function Write-GpuNoteNew([string]$File, [string]$Line) {
  # CreateNew: fails when the file is there (the other program made it in between).
  $stream = [IO.File]::Open($File, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
  $bytes = [Text.Encoding]::UTF8.GetBytes($Line + "`r`n")
  $stream.Write($bytes, 0, $bytes.Length)
  $stream.Dispose()
}

function New-GpuClaimLine([string]$Who, [string]$What, [int]$Minutes) {
  $now = Get-Date
  "Skylanders (Claude Code, $Who): $What, seit $($now.ToString('HH:mm')), voraussichtlich bis " +
    "$($now.AddMinutes([math]::Max($Minutes, 1)).ToString('HH:mm')) (pid $PID)"
}

# Our wait file goes when this process wrote it (or it is ours and its process is gone: Read-GpuNote).
function Clear-GpuWait() {
  $wait = Get-GpuWaitFile
  $w = Read-GpuNote $wait
  if ($w.Exists -and $w.Ours -and $w.Pid -eq $PID) { Remove-Item $wait -Force -ErrorAction SilentlyContinue }
}

# Whether the other program has the card or waits for it (rules 1 and 4); its line, or "".
function Get-GpuOtherSide() {
  $b = Read-GpuNote (Get-GpuClaimFile)
  if ($b.Exists -and -not $b.Ours) { return "has it: $($b.Text)" }
  $w = Read-GpuNote (Get-GpuWaitFile)
  if ($w.Exists -and -not $w.Ours) { return "waits: $($w.Text)" }
  return ""
}

# -Minutes: about how long the run uses the card (written into the claim).
function Enter-GpuClaim([string]$Name, [int]$Minutes = 15) {
  $file = Get-GpuClaimFile
  if (-not $file -or -not (Test-Path (Split-Path $file))) { return "" }
  $wait = Get-GpuWaitFile
  $said = $false
  try {
    while ($true) {
      $b = Read-GpuNote $file
      $w = Read-GpuNote $wait
      $otherWaits = $w.Exists -and -not $w.Ours
      if ($b.Exists -and $b.Ours -and ($b.Pid -eq $PID -or -not $b.Watch -or -not $otherWaits)) {
        # Ours already: this process, a chain (its Wait-Turn gates between tests), or the watch with nobody waiting.
        return ""
      }
      if (-not $b.Exists -and -not $otherWaits) {
        try {
          Write-GpuNoteNew $file (New-GpuClaimLine "Spieltest" $Name $Minutes)
          return $file
        } catch {
          Start-Sleep -Seconds 2
          continue
        }
      }
      # Waiting (rule 3): our wait file, unless the other program's is there (then it goes first anyway).
      if ($wait -and -not $w.Exists) {
        try { Write-GpuNoteNew $wait "Skylanders (Claude Code, Spieltest): wartet seit $(Get-Date -Format 'HH:mm') (pid $PID)" } catch {}
      }
      if (-not $said) {
        $why = if ($b.Exists) { $b.Text } else { $w.Text }
        Write-Host "waiting for the graphics card: $why"
        $said = $true
      }
      Start-Sleep -Seconds 15
    }
  } finally {
    Clear-GpuWait
  }
}

function Exit-GpuClaim([string]$File) {
  if (-not $File -or -not (Test-Path $File)) { return }
  $text = ""
  try { $text = Get-Content -Raw $File -ErrorAction Stop } catch {}
  if ($text -match "\(pid $PID\)") { Remove-Item $File -Force -ErrorAction SilentlyContinue }
}
