<#
.SYNOPSIS
Draws status.png, the picture at the top of TESTING.md: the six games as cards in their colours,
each with what the last test runs covered (levels, figures, the end credits) and a drawn tick, cross
or dash. The numbers come from a JSON file (the developer kit's status_page.py writes it next to
the page); the style is the banners' (drawing.ps1).

.USAGE
    tools/make_status_card.ps1 -Data <status.json> [-OutDir <folder>]
#>
param(
    [Parameter(Mandatory = $true)][string]$Data,
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "drawing.ps1")
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot "..\.github\images" }
$status = Get-Content $Data -Raw | ConvertFrom-Json
# The page's game keys to the drawing's.
$keys = @{ ssa = "ssa"; giants = "giants"; sf = "swapforce"; tt = "trapteam"; sc = "superchargers"; im = "imaginators" }

# A mark in a circle: "ok" a tick, "bad" a cross, anything else a dash.
function Add-Mark($g, [double]$cx, [double]$cy, [string]$kind) {
    $r = 13
    $fill = switch ($kind) { "ok" { Get-Color 255 70 175 90 } "bad" { Get-Color 255 215 60 60 } default { Get-Color 255 80 88 120 } }
    $g.FillEllipse((New-Object System.Drawing.SolidBrush $fill), [single]($cx - $r), [single]($cy - $r), [single](2 * $r), [single](2 * $r))
    $pen = New-Object System.Drawing.Pen (Get-Color 255 255 255 255), 3.2
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    switch ($kind) {
        "ok" { $g.DrawLines($pen, [System.Drawing.PointF[]]@((Get-Point ($cx - 6) ($cy + 0.5)), (Get-Point ($cx - 1.5) ($cy + 5)), (Get-Point ($cx + 6.5) ($cy - 5)))) }
        "bad" { $g.DrawLine($pen, [single]($cx - 5), [single]($cy - 5), [single]($cx + 5), [single]($cy + 5)); $g.DrawLine($pen, [single]($cx + 5), [single]($cy - 5), [single]($cx - 5), [single]($cy + 5)) }
        default { $g.DrawLine($pen, [single]($cx - 5), [single]$cy, [single]($cx + 5), [single]$cy) }
    }
}

# A line of a card: its mark, its name and its result.
function Add-Line($g, [double]$x, [double]$y, [double]$w, [string]$name, [string]$value, [string]$kind) {
    Add-Mark $g ($x + 13) ($y + 14) $kind
    Add-Text $g $name ($x + 36) $y 22 $regular (Get-Color 255 196 206 240)
    $vw = Get-TextWidth $g $value 22 $bold
    $color = if ($kind -eq "none") { Get-Color 255 120 130 165 } else { Get-Color 255 255 255 255 }
    Add-Text $g $value ($x + $w - $vw) $y 22 $bold $color
}

function Get-Count($pair) {
    $ok = [int]$pair[0]; $n = [int]$pair[1]
    if ($n -eq 0) { return "not yet", "none" }
    return ("{0} of {1}" -f $ok, $n), $(if ($ok -eq $n) { "ok" } else { "bad" })
}

$W = 1280; $H = 560
$bmp, $g = New-Canvas $W $H
$frame = Get-RoundedRect 0 0 $W $H 28
$g.SetClip($frame)
$sky = New-Object System.Drawing.Drawing2D.LinearGradientBrush (Get-Point 0 0), (Get-Point $W $H), (Get-Color 255 8 14 48), (Get-Color 255 22 34 92)
$g.FillPath($sky, $frame)
$rand = New-Object System.Random 2011
for ($i = 0; $i -lt 70; $i++) {
    $sx = $rand.NextDouble() * $W; $sy = $rand.NextDouble() * $H
    $sr = 0.6 + $rand.NextDouble() * 1.4
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (Get-Color (30 + $rand.Next(80)) 220 228 255)), [single]($sx - $sr), [single]($sy - $sr), [single](2 * $sr), [single](2 * $sr))
}

Add-Text $g "SKYLANDERS" 48 38 22 $bold $gold 6
Add-Text $g "Test status" 46 66 56 $bold (Get-Color 255 255 255 255)
$sub = "The last runs of the test kit, $($status.date)"
Add-Text $g $sub ($W - 48 - (Get-TextWidth $g $sub 22 $regular)) 92 22 $regular (Get-Color 255 196 206 240)

# Six cards, three to a row, in release order.
$cols = 3; $gap = 24; $left = 48; $top = 160
$cw = ($W - 2 * $left - ($cols - 1) * $gap) / $cols; $ch = 176
for ($i = 0; $i -lt $status.games.Count; $i++) {
    $game = $status.games[$i]
    $info = $games[$keys[$game.key]]
    $accent = $info.Color
    $x = $left + ($i % $cols) * ($cw + $gap); $y = $top + [math]::Floor($i / $cols) * ($ch + $gap)
    $card = Get-RoundedRect $x $y $cw $ch 18
    $fill = New-Object System.Drawing.Drawing2D.LinearGradientBrush (Get-Point $x $y), (Get-Point ($x + $cw) ($y + $ch)), (Get-Mix @(14, 20, 58) $accent 0.22), (Get-Color 255 12 18 52)
    $g.FillPath($fill, $card)
    $g.DrawPath((New-Object System.Drawing.Pen (Get-Color 170 $accent[0] $accent[1] $accent[2]), 2), $card)
    # The game's colour as a band along the top, with its name.
    $g.SetClip($card)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush (Get-Color 255 $accent[0] $accent[1] $accent[2])), [single]$x, [single]$y, [single]$cw, 6)
    $g.SetClip($frame)
    Add-Text $g $info.Name ($x + 20) ($y + 18) 28 $bold (Get-Color 255 255 255 255)
    $tested = if ($game.tested -and $game.tested -ne "-") { $game.tested } else { "" }
    if ($tested) {
      # Right of the name; a long name (Spyro's Adventure) leaves room for the version without the
      # date, or only in a smaller size.
      $nameRight = $x + 20 + (Get-TextWidth $g $info.Name 28 $bold) + 12
      $short = $tested -replace '\s*\(.*\)$', ''
      foreach ($try in @(@($tested, 16), @($short, 16), @($short, 13))) {
        $text, $size = $try
        $tx = $x + $cw - 20 - (Get-TextWidth $g $text $size $regular)
        if ($tx -ge $nameRight) { break }
      }
      Add-Text $g $text $tx ($y + 28 + (16 - $size) / 2) $size $regular (Get-Color 255 150 160 200)
    }
    $levels, $lk = Get-Count $game.levels
    $figures, $fk = Get-Count $game.figures
    $credits, $ck = if ($null -eq $game.credits) { "not yet", "none" } elseif ($game.credits) { "to the end", "ok" } else { "stopped", "bad" }
    Add-Line $g ($x + 20) ($y + 68) ($cw - 40) "Levels" $levels $lk
    Add-Line $g ($x + 20) ($y + 102) ($cw - 40) "Figures" $figures $fk
    Add-Line $g ($x + 20) ($y + 136) ($cw - 40) "End credits" $credits $ck
}

$g.ResetClip()
$g.DrawPath((New-Object System.Drawing.Pen (Get-Color 90 255 204 64), 2), (Get-RoundedRect 1 1 ($W - 2) ($H - 2) 27))
$g.Dispose()
Save-Png $bmp $OutDir "status.png"
