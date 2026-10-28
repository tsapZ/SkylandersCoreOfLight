<#
.SYNOPSIS
Draws the pictures at the top of the READMEs: banner.png (the Core of Light from the icon, with
the six games around it, and the name) and download.png (the download button). Our own drawings
in the launcher's colours, no art from the discs; the text is Noto Sans from fonts/. The PNGs are
in the repositories; run this again only to change them.

Without -Game it draws Core of Light's, into this repository's .github/images. With -Game (ssa,
giants, swapforce, trapteam, superchargers, imaginators) it draws that game's banner (its name,
its year and its card colour from the launcher, its dot of the six lit) into -OutDir, the game
repository's .github/images.

.USAGE
    tools/make_banner.ps1 [-Game <key>] [-OutDir <folder>]
#>
param(
    [string]$Game = "",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "drawing.ps1")
if ($Game -and -not $games.Contains($Game)) { throw "unknown game '$Game' (one of: $($games.Keys -join ', '))" }
if (-not $OutDir) {
    if ($Game) { throw "-OutDir is needed with -Game (the game repository's .github\images)" }
    $OutDir = Join-Path $PSScriptRoot "..\.github\images"
}

# --- The banner -------------------------------------------------------------------------------
# $lit: which of the six dots is the game's (0-5), -1 for Core of Light's (all six alike).
function Draw-Banner([string]$kicker, [string]$title, [string[]]$lines, $accent, [int]$lit) {
    $W = 1280; $H = 400
    $bmp, $g = New-Canvas $W $H
    $card = Get-RoundedRect 0 0 $W $H 28
    $g.SetClip($card)
    # The night sky, for a game a little towards its colour.
    $top = @(8, 14, 48); $bottom = @(22, 34, 92)
    if ($accent) { $top = @((Get-Mix $top $accent 0.10).R, (Get-Mix $top $accent 0.10).G, (Get-Mix $top $accent 0.10).B) }
    $sky = New-Object System.Drawing.Drawing2D.LinearGradientBrush (Get-Point 0 0), (Get-Point $W $H), (Get-Color 255 $top[0] $top[1] $top[2]), (Get-Color 255 $bottom[0] $bottom[1] $bottom[2])
    $g.FillPath($sky, $card)

    # A few faint stars, always the same ones.
    $rand = New-Object System.Random 2011
    for ($i = 0; $i -lt 90; $i++) {
        $sx = $rand.NextDouble() * $W; $sy = $rand.NextDouble() * $H
        $sr = 0.6 + $rand.NextDouble() * 1.4
        $g.FillEllipse((New-Object System.Drawing.SolidBrush (Get-Color (40 + $rand.Next(90)) 220 228 255)), [single]($sx - $sr), [single]($sy - $sr), [single](2 * $sr), [single](2 * $sr))
    }

    # The core, as on the icon but without its disc: a wide glow, eight rays, the bright middle.
    $cx = 240; $cy = 200
    Add-Glow $g $cx $cy 320 (Get-Color 70 90 120 230) (Get-Color 0 90 120 230)
    Add-Glow $g $cx $cy 200 (Get-Color 80 255 200 90) (Get-Color 0 255 200 90)
    $r = 168
    $rayBrush = New-Object System.Drawing.SolidBrush (Get-Color 235 255 214 110)
    for ($i = 0; $i -lt 8; $i++) {
        $angle = $i * [math]::PI / 4
        $long = if ($i % 2 -eq 0) { $r * 0.86 } else { $r * 0.62 }
        $half = if ($i % 2 -eq 0) { $r * 0.13 } else { $r * 0.10 }
        $dx = [math]::Cos($angle); $dy = [math]::Sin($angle)
        $pts = @((Get-Point ($cx + $dx * $long) ($cy + $dy * $long)), (Get-Point ($cx - $dy * $half) ($cy + $dx * $half)), (Get-Point ($cx + $dy * $half) ($cy - $dx * $half)))
        $g.FillPolygon($rayBrush, [System.Drawing.PointF[]]$pts)
    }
    $glowR = $r * 0.55
    $glow = New-Object System.Drawing.Drawing2D.GraphicsPath
    $glow.AddEllipse([single]($cx - $glowR), [single]($cy - $glowR), [single](2 * $glowR), [single](2 * $glowR))
    $glowBrush = New-Object System.Drawing.Drawing2D.PathGradientBrush $glow
    $blend = New-Object System.Drawing.Drawing2D.ColorBlend 4
    $blend.Colors = @((Get-Color 0 255 190 60), (Get-Color 170 255 200 80), (Get-Color 255 255 236 170), (Get-Color 255 255 255 255))
    $blend.Positions = @([single]0.0, [single]0.45, [single]0.8, [single]1.0)
    $glowBrush.InterpolationColors = $blend
    $g.FillPath($glowBrush, $glow)
    $coreR = $r * 0.2
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (Get-Color 255 255 255 250)), [single]($cx - $coreR), [single]($cy - $coreR), [single](2 * $coreR), [single](2 * $coreR))
    $g.DrawEllipse((New-Object System.Drawing.Pen $gold, 3), [single]($cx - $coreR), [single]($cy - $coreR), [single](2 * $coreR), [single](2 * $coreR))

    # The six games around it, on a thin ring between the rays, the first at the top right and on
    # clockwise in release order; a game's own dot is bigger and in its colour.
    $orbit = 168
    $g.DrawEllipse((New-Object System.Drawing.Pen (Get-Color 70 255 204 64), 2), [single]($cx - $orbit), [single]($cy - $orbit), [single](2 * $orbit), [single](2 * $orbit))
    for ($i = 0; $i -lt 6; $i++) {
        $angle = (-90 + 30 + 60 * $i) * [math]::PI / 180
        $px = $cx + [math]::Cos($angle) * $orbit; $py = $cy + [math]::Sin($angle) * $orbit
        if ($lit -eq $i) {
            Add-Glow $g $px $py 40 (Get-Color 170 $accent[0] $accent[1] $accent[2]) (Get-Color 0 $accent[0] $accent[1] $accent[2])
            $light = Get-Mix $accent @(255, 255, 255) 0.35
            $g.FillEllipse((New-Object System.Drawing.SolidBrush $light), [single]($px - 14), [single]($py - 14), 28, 28)
            $g.DrawEllipse((New-Object System.Drawing.Pen (Get-Color 255 255 255 255), 3), [single]($px - 14), [single]($py - 14), 28, 28)
        } elseif ($lit -ge 0) {
            $g.FillEllipse((New-Object System.Drawing.SolidBrush (Get-Color 150 255 226 150)), [single]($px - 6), [single]($py - 6), 12, 12)
        } else {
            Add-Glow $g $px $py 22 (Get-Color 120 255 214 110) (Get-Color 0 255 214 110)
            $g.FillEllipse((New-Object System.Drawing.SolidBrush (Get-Color 255 255 226 150)), [single]($px - 8), [single]($py - 8), 16, 16)
            $g.DrawEllipse((New-Object System.Drawing.Pen $gold, 2), [single]($px - 8), [single]($py - 8), 16, 16)
        }
    }

    # The name and what it is; a long name gets smaller to fit.
    $tx = 500
    $kickerColor = if ($accent) { Get-Mix $accent @(255, 255, 255) 0.25 } else { $gold }
    Add-Text $g $kicker $tx 96 26 $bold $kickerColor 7
    $size = 92
    $fit = (1280 - 60 - $tx) / (Get-TextWidth $g $title $size $bold)
    if ($fit -lt 1) { $size = [math]::Floor($size * $fit) }
    Add-Text $g $title ($tx - 4) (128 + (92 - $size) * 0.6) $size $bold (Get-Color 255 255 255 255)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        Add-Text $g $lines[$i] $tx (262 + 40 * $i) 30 $regular (Get-Color 255 196 206 240)
    }

    $g.ResetClip()
    $edge = if ($accent) { Get-Color 120 $accent[0] $accent[1] $accent[2] } else { Get-Color 90 255 204 64 }
    $g.DrawPath((New-Object System.Drawing.Pen $edge, 2), (Get-RoundedRect 1 1 ($W - 2) ($H - 2) 27))
    $g.Dispose()
    Save-Png $bmp $OutDir "banner.png"
}

if ($Game) {
    $info = $games[$Game]
    $index = [array]::IndexOf(@($games.Keys), $Game)
    Draw-Banner "SKYLANDERS  $($info.Year)" $info.Name @("The Xbox 360 game, recompiled to run", "natively on Windows. Bring your own disc.") $info.Color $index
} else {
    Draw-Banner "SKYLANDERS" "Core of Light" @("One launcher for the six recompiled", "Xbox 360 games, from your own discs.") $null -1
}

# --- The download button (drawn at twice the size it's shown at) ------------------------------
$W = 600; $H = 112
$bmp, $g = New-Canvas $W $H
$pill = Get-RoundedRect 2 2 ($W - 4) ($H - 4) (($H - 4) / 2)
$fill = New-Object System.Drawing.Drawing2D.LinearGradientBrush (Get-Point 0 0), (Get-Point 0 $H), (Get-Color 255 255 222 128), (Get-Color 255 245 182 46)
$g.FillPath($fill, $pill)
$g.DrawPath((New-Object System.Drawing.Pen (Get-Color 255 176 120 16), 3), $pill)
$navy = Get-Color 255 10 18 60
# An arrow down onto a line.
$ax = 70; $ay = 56
$arrow = @((Get-Point ($ax - 9) ($ay - 26)), (Get-Point ($ax + 9) ($ay - 26)), (Get-Point ($ax + 9) ($ay - 2)), (Get-Point ($ax + 22) ($ay - 2)),
           (Get-Point $ax ($ay + 20)), (Get-Point ($ax - 22) ($ay - 2)), (Get-Point ($ax - 9) ($ay - 2)))
$g.FillPolygon((New-Object System.Drawing.SolidBrush $navy), [System.Drawing.PointF[]]$arrow)
$g.FillRectangle((New-Object System.Drawing.SolidBrush $navy), [single]($ax - 24), [single]($ay + 26), 48, 7)
Add-Text $g "Download for Windows" 116 30 40 $bold $navy
$g.Dispose()
Save-Png $bmp $OutDir "download.png"
