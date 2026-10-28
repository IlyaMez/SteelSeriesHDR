# Original vector artwork, matching assets/mark.svg. Supersample every size
# separately for a crisp taskbar and tray at common Windows display scales.
param([string]$PreviewPath)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$destination = Join-Path (Split-Path $PSScriptRoot -Parent) 'assets/arena.ico'
$images = @()
function Color([string]$hex) { [System.Drawing.ColorTranslator]::FromHtml($hex) }
function RoundedPath([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $shape = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $d = 2 * $r
    $shape.AddArc($x, $y, $d, $d, 180, 90)
    $shape.AddArc(($x + $w - $d), $y, $d, $d, 270, 90)
    $shape.AddArc(($x + $w - $d), ($y + $h - $d), $d, $d, 0, 90)
    $shape.AddArc($x, ($y + $h - $d), $d, $d, 90, 90)
    $shape.CloseFigure()
    return ,$shape
}
foreach ($size in @(16, 20, 24, 32, 40, 48, 64, 128, 256)) {
    $bitmap = [System.Drawing.Bitmap]::new(($size * 4), ($size * 4))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform(($size * 4 / 256.0), ($size * 4 / 256.0))
    $path = RoundedPath 5 5 246 246 54
    $bg = [System.Drawing.Drawing2D.LinearGradientBrush]::new([System.Drawing.Point]::new(30, 5), [System.Drawing.Point]::new(220, 251), (Color '#213c58'), (Color '#0b1525'))
    $border = [System.Drawing.Pen]::new((Color '#35516b'), 2)
    $graphics.FillPath($bg, $path)
    $graphics.DrawPath($border, $path)
    foreach ($side in @(0, 1)) {
        $x = if ($side) { 218 } else { 38 }
        $control = if ($side) { 230 } else { 26 }
        $tint = if ($side) { Color '#ffac7d' } else { Color '#40e0c4' }
        $wave = [System.Drawing.Drawing2D.GraphicsPath]::new()
        $wave.AddBezier($x, 86, $control, 114, $control, 142, $x, 170)
        foreach ($width in @(24, 10)) {
            $ink = if ($width -eq 24) { [System.Drawing.Color]::FromArgb(26, $tint) } else { $tint }
            $pen = [System.Drawing.Pen]::new($ink, $width)
            $pen.StartCap = $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            $graphics.DrawPath($pen, $wave)
            $pen.Dispose()
        }
        $wave.Dispose()
    }
    $frame = RoundedPath 60 65 136 107 17
    $white = [System.Drawing.SolidBrush]::new((Color '#eef8ff'))
    $graphics.FillPath($white, $frame)
    $screen = RoundedPath 70 75 116 87 8
    $spectrum = [System.Drawing.Drawing2D.LinearGradientBrush]::new([System.Drawing.Point]::new(70, 162), [System.Drawing.Point]::new(186, 75), (Color '#40e0c4'), (Color '#ffac7d'))
    $blend = [System.Drawing.Drawing2D.ColorBlend]::new(3)
    $blend.Colors = @((Color '#40e0c4'), (Color '#61bdf5'), (Color '#ffac7d'))
    $blend.Positions = @(0, 0.48, 1)
    $spectrum.InterpolationColors = $blend
    $graphics.FillPath($spectrum, $screen)
    if ($size -ge 32) {
        $shade = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(46, (Color '#132b46')))
        $points = [System.Drawing.Point[]]@([System.Drawing.Point]::new(78, 143), [System.Drawing.Point]::new(123, 97), [System.Drawing.Point]::new(149, 123), [System.Drawing.Point]::new(178, 94), [System.Drawing.Point]::new(178, 154), [System.Drawing.Point]::new(78, 154))
        $graphics.FillPolygon($shade, $points)
        $shade.Dispose()
    }
    $stand = [System.Drawing.Pen]::new((Color '#eef8ff'), 10)
    $stand.StartCap = $stand.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $graphics.DrawLine($stand, 128, 177, 128, 193)
    $graphics.DrawLine($stand, 103, 197, 153, 197)
    $small = [System.Drawing.Bitmap]::new($size, $size)
    $resize = [System.Drawing.Graphics]::FromImage($small)
    $resize.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $resize.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $resize.DrawImage($bitmap, 0, 0, $size, $size)
    $stream = [System.IO.MemoryStream]::new()
    $small.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    if ($PreviewPath -and $size -eq 256) { $small.Save($PreviewPath, [System.Drawing.Imaging.ImageFormat]::Png) }
    $images += ,@{ Size = $size; Bytes = $stream.ToArray() }
    foreach ($object in @($stream, $resize, $small, $stand, $spectrum, $screen, $white, $frame, $border, $bg, $path, $graphics, $bitmap)) { $object.Dispose() }
}
$output = [System.IO.File]::Create($destination)
$writer = [System.IO.BinaryWriter]::new($output)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($entry in $images) {
        $dimension = if ($entry.Size -eq 256) { 0 } else { $entry.Size }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$entry.Bytes.Length); $writer.Write([uint32]$offset)
        $offset += $entry.Bytes.Length
    }
    foreach ($entry in $images) { $writer.Write([byte[]]$entry.Bytes) }
} finally { $writer.Dispose(); $output.Dispose() }
Write-Host "Created $destination"
