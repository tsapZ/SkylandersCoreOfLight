# The drawing helpers of make_banner.ps1 and make_status_card.ps1 (dot-sourced): colours, rounded
# rectangles, glows, text as paths in Noto Sans from fonts/, a PNG out. GDI+ (System.Drawing).

Add-Type -AssemblyName System.Drawing

# The six games in release order, with the launcher's card colours (launcher_ui.cpp kAccents).
$games = [ordered]@{
    ssa           = @{ Name = "Spyro's Adventure"; Year = 2011; Color = @(80, 170, 70) }
    giants        = @{ Name = "Giants"; Year = 2012; Color = @(230, 130, 40) }
    swapforce     = @{ Name = "Swap Force"; Year = 2013; Color = @(150, 90, 220) }
    trapteam      = @{ Name = "Trap Team"; Year = 2014; Color = @(40, 150, 220) }
    superchargers = @{ Name = "SuperChargers"; Year = 2015; Color = @(220, 60, 60) }
    imaginators   = @{ Name = "Imaginators"; Year = 2016; Color = @(240, 180, 40) }
}

function Get-Color([int]$a, [int]$r, [int]$g, [int]$b) { [System.Drawing.Color]::FromArgb($a, $r, $g, $b) }
function Get-Point([double]$x, [double]$y) { New-Object System.Drawing.PointF ([single]$x), ([single]$y) }
# Between two colours: t 0 is $a, 1 is $b.
function Get-Mix($a, $b, [double]$t) {
    Get-Color 255 ([int]($a[0] + ($b[0] - $a[0]) * $t)) ([int]($a[1] + ($b[1] - $a[1]) * $t)) ([int]($a[2] + ($b[2] - $a[2]) * $t))
}

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

# A soft round glow: $inner at the middle, fading out to $outer at the edge.
function Add-Glow($g, [double]$cx, [double]$cy, [double]$r, $inner, $outer) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddEllipse([single]($cx - $r), [single]($cy - $r), [single](2 * $r), [single](2 * $r))
    $brush = New-Object System.Drawing.Drawing2D.PathGradientBrush $path
    $brush.CenterColor = $inner
    $brush.SurroundColors = @($outer)
    $g.FillPath($brush, $path)
}

# The width of a text at that size, without drawing it.
function Get-TextWidth($g, [string]$text, [double]$size, $family) {
    $font = New-Object System.Drawing.Font $family, ([single]$size), ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
    return $g.MeasureString($text, $font, (Get-Point 0 0), [System.Drawing.StringFormat]::GenericTypographic).Width
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
        $x += (Get-TextWidth $g ([string]$ch) $size $family) + $tracking
    }
}

function Save-Png($bmp, [string]$dir, [string]$name) {
    $folder = New-Item -ItemType Directory -Force $dir
    $file = Join-Path $folder.FullName $name
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "wrote $file"
}

$gold = Get-Color 255 255 204 64
$goldRgb = @(255, 204, 64)
