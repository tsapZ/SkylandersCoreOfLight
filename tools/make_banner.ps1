<#
.SYNOPSIS
Draws the pictures at the top of README.md: .github/images/banner.png (the Core of Light from the
icon, with six games around it, and the name) and .github/images/download.png (the download
button). Our own drawings in the launcher's colours, no art from the discs; the text is Noto Sans
from fonts/. The PNGs are in the repository; run this again only to change them.

.USAGE
    tools/make_banner.ps1 [-OutDir .github/images]
#>
param(
    [string]$OutDir = (Join-Path $PSScriptRoot "..\.github\images")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Get-Color([int]$a, [int]$r, [int]$g, [int]$b) { [System.Drawing.Color]::FromArgb($a, $r, $g, $b) }
function Get-Point([double]$x, [double]$y) { New-Object System.Drawing.PointF ([single]$x), ([single]$y) }

# One collection per file: with both in one, GDI+ drew the regular text from the bold file.
function Get-FontFamily([string]$file) {
    $fonts = New-Object System.Drawing.Text.PrivateFontCollection
    $fonts.AddFontFile((Resolve-Path (Join-Path $PSScriptRoot "..\fonts\$file")).Path)
    return $fonts.Families[0]
}
$bold = Get-FontFamily "NotoSans-Bold.ttf"
$regular = Get-FontFamily "NotoSans-Regular.ttf"

function New-Canvas([int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    return $bmp, $g
}

function Get-RoundedRect([double]$x, [double]$y, [double]$w, [double]$h, [double]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc([single]$x, [single]$y, [single]$d, [single]$d, 180, 90)
    $p.AddArc([single]($x + $w - $d), [single]$y, [single]$d, [single]$d, 270, 90)
    $p.AddArc([single]($x + $w - $d), [single]($y + $h - $d), [single]$d, [single]$d, 0, 90)
    $p.AddArc([single]$x, [single]($y + $h - $d), [single]$d, [single]$d, 90, 90)
    $p.CloseFigure()
    return $p
}

# A soft round glow: $color at the middle, fading out to the edge.
function Add-Glow($g, [double]$cx, [double]$cy, [double]$r, $inner, $outer) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddEllipse([single]($cx - $r), [single]($cy - $r), [single](2 * $r), [single](2 * $r))
    $brush = New-Object System.Drawing.Drawing2D.PathGradientBrush $path
    $brush.CenterColor = $inner
    $brush.SurroundColors = @($outer)
    $g.FillPath($brush, $path)
}

# Text as a path (smoother than DrawString), its top left corner at x, y; $tracking spreads the letters.
function Add-Text($g, [string]$text, [double]$x, [double]$y, [double]$size, $family, $color, [double]$tracking = 0) {
    $brush = New-Object System.Drawing.SolidBrush $color
    $format = [System.Drawing.StringFormat]::GenericTypographic
    if ($tracking -eq 0) {
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $path.AddString($text, $family, 0, [single]$size, (Get-Point $x $y), $format)
        $g.FillPath($brush, $path)
        return
    }
    foreach ($ch in $text.ToCharArray()) {
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $path.AddString([string]$ch, $family, 0, [single]$size, (Get-Point $x $y), $format)
        $g.FillPath($brush, $path)
        $font = New-Object System.Drawing.Font $family, ([single]$size), ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
        $x += $g.MeasureString([string]$ch, $font, (Get-Point 0 0), $format).Width + $tracking
    }
}

function Save-Png($bmp, [string]$name) {
    $dir = New-Item -ItemType Directory -Force $OutDir
    $file = Join-Path $dir.FullName $name
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "wrote $file"
}

$gold = Get-Color 255 255 204 64

# --- The banner -------------------------------------------------------------------------------
$W = 1280; $H = 400
$bmp, $g = New-Canvas $W $H
$card = Get-RoundedRect 0 0 $W $H 28
$g.SetClip($card)
$sky = New-Object System.Drawing.Drawing2D.LinearGradientBrush (Get-Point 0 0), (Get-Point $W $H), (Get-Color 255 8 14 48), (Get-Color 255 22 34 92)
$g.FillPath($sky, $card)

# A few faint stars, always the same ones.
$rand = New-Object System.Random 2011
for ($i = 0; $i -lt 90; $i++) {
    $sx = $rand.NextDouble() * $W; $sy = $rand.NextDouble() * $H
    $sr = 0.6 + $rand.NextDouble() * 1.4
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (Get-Color (40 + $rand.Next(90)) 220 228 255)), [single]($sx - $sr), [single]($sy - $sr), [single](2 * $sr), [single](2 * $sr))
}

# The core, as on the icon but without its disc: a wide warm glow, eight rays, the bright middle.
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

# The six games around it, on a thin ring between the rays.
$orbit = 168
$g.DrawEllipse((New-Object System.Drawing.Pen (Get-Color 70 255 204 64), 2), [single]($cx - $orbit), [single]($cy - $orbit), [single](2 * $orbit), [single](2 * $orbit))
for ($i = 0; $i -lt 6; $i++) {
    $angle = (-90 + 30 + 60 * $i) * [math]::PI / 180
    $px = $cx + [math]::Cos($angle) * $orbit; $py = $cy + [math]::Sin($angle) * $orbit
    Add-Glow $g $px $py 22 (Get-Color 120 255 214 110) (Get-Color 0 255 214 110)
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (Get-Color 255 255 226 150)), [single]($px - 8), [single]($py - 8), 16, 16)
    $g.DrawEllipse((New-Object System.Drawing.Pen $gold, 2), [single]($px - 8), [single]($py - 8), 16, 16)
}

# The name and what it is.
$tx = 500
Add-Text $g "SKYLANDERS" $tx 96 26 $bold $gold 7
Add-Text $g "Core of Light" ($tx - 4) 128 92 $bold (Get-Color 255 255 255 255)
Add-Text $g "One launcher for the six recompiled" $tx 262 30 $regular (Get-Color 255 196 206 240)
Add-Text $g "Xbox 360 games, from your own discs." $tx 302 30 $regular (Get-Color 255 196 206 240)

$g.ResetClip()
$g.DrawPath((New-Object System.Drawing.Pen (Get-Color 90 255 204 64), 2), (Get-RoundedRect 1 1 ($W - 2) ($H - 2) 27))
$g.Dispose()
Save-Png $bmp "banner.png"

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
Save-Png $bmp "download.png"
