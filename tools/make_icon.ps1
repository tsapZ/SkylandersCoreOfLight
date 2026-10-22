<#
.SYNOPSIS
Draws the launcher's icon, src/coreoflight.ico: our own picture of the Core of Light (no art from
the discs), a glowing core with eight rays in a dark blue disc with a golden edge, in the
launcher's colours. The .ico (256, 64, 48, 32, 16 px PNGs) is in the repository; run this again
only to change the drawing.

.USAGE
    tools/make_icon.ps1 [-Out src/coreoflight.ico] [-Preview <png>]
#>
param(
    [string]$Out = (Join-Path $PSScriptRoot "..\src\coreoflight.ico"),
    [string]$Preview = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Get-Color([int]$a, [int]$r, [int]$g, [int]$b) { [System.Drawing.Color]::FromArgb($a, $r, $g, $b) }

function Get-CorePng([int]$side) {
    $bmp = New-Object System.Drawing.Bitmap $side, $side, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $c = $side / 2.0
    $edge = [math]::Max(1.0, $side / 24.0)
    $r = $c - $edge / 2 - 0.5

    # The disc: dark blue, a little lighter towards the middle, with a golden edge.
    $disc = New-Object System.Drawing.Drawing2D.GraphicsPath
    $disc.AddEllipse([single]($c - $r), [single]($c - $r), [single](2 * $r), [single](2 * $r))
    $fill = New-Object System.Drawing.Drawing2D.PathGradientBrush $disc
    $fill.CenterColor = Get-Color 255 40 60 140
    $fill.SurroundColors = @(Get-Color 255 8 14 48)
    $g.FillPath($fill, $disc)

    # Eight rays, the four long ones upright and across, tapering to a point.
    $gold = Get-Color 255 255 204 64
    $rayBrush = New-Object System.Drawing.SolidBrush (Get-Color 235 255 214 110)
    for ($i = 0; $i -lt 8; $i++) {
        $angle = $i * [math]::PI / 4
        $long = if ($i % 2 -eq 0) { $r * 0.86 } else { $r * 0.62 }
        $half = if ($i % 2 -eq 0) { $r * 0.13 } else { $r * 0.10 }
        $dx = [math]::Cos($angle); $dy = [math]::Sin($angle)
        $pts = @(
            (New-Object System.Drawing.PointF ([single]($c + $dx * $long)), ([single]($c + $dy * $long))),
            (New-Object System.Drawing.PointF ([single]($c - $dy * $half)), ([single]($c + $dx * $half))),
            (New-Object System.Drawing.PointF ([single]($c + $dy * $half)), ([single]($c - $dx * $half)))
        )
        $g.FillPolygon($rayBrush, [System.Drawing.PointF[]]$pts)
    }

    # The glow: white in the middle, gold, then fading out.
    $glowR = $r * 0.55
    $glow = New-Object System.Drawing.Drawing2D.GraphicsPath
    $glow.AddEllipse([single]($c - $glowR), [single]($c - $glowR), [single](2 * $glowR), [single](2 * $glowR))
    $glowBrush = New-Object System.Drawing.Drawing2D.PathGradientBrush $glow
    $blend = New-Object System.Drawing.Drawing2D.ColorBlend 4
    $blend.Colors = @((Get-Color 0 255 190 60), (Get-Color 170 255 200 80), (Get-Color 255 255 236 170), (Get-Color 255 255 255 255))
    $blend.Positions = @([single]0.0, [single]0.45, [single]0.8, [single]1.0)
    $glowBrush.InterpolationColors = $blend
    $g.FillPath($glowBrush, $glow)

    # The core: a small white disc with a golden rim.
    $coreR = $r * 0.2
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (Get-Color 255 255 255 250)), [single]($c - $coreR), [single]($c - $coreR), [single](2 * $coreR), [single](2 * $coreR))
    $rim = New-Object System.Drawing.Pen $gold, ([single][math]::Max(1.0, $side / 64.0))
    $g.DrawEllipse($rim, [single]($c - $coreR), [single]($c - $coreR), [single](2 * $coreR), [single](2 * $coreR))

    $edgePen = New-Object System.Drawing.Pen $gold, ([single]$edge)
    $g.DrawEllipse($edgePen, [single]($c - $r), [single]($c - $r), [single](2 * $r), [single](2 * $r))
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    if ($Preview -and $side -eq 256) { $bmp.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png) }
    $bmp.Dispose()
    return , $ms.ToArray()
}

$sizes = @(256, 64, 48, 32, 16)
$images = @()
foreach ($s in $sizes) { $images += , (Get-CorePng $s) }
$ico = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $ico
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $side = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $w.Write([byte]$side); $w.Write([byte]$side); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$images[$i].Length); $w.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($img in $images) { $w.Write($img) }
$w.Flush()
[System.IO.File]::WriteAllBytes((Resolve-Path -LiteralPath (Split-Path -Parent $Out)).Path + "\" + (Split-Path -Leaf $Out), $ico.ToArray())
Write-Output "wrote $Out"
