# Original Arena HDR artwork. Regenerate assets/arena.ico with Windows PowerShell.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$destination = Join-Path (Split-Path $PSScriptRoot -Parent) 'assets/arena.ico'
$images = @()
foreach ($size in @(16, 32, 48, 256)) {
    $bitmap = [System.Drawing.Bitmap]::new($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform(($size / 256.0), ($size / 256.0))
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc(0, 0, 80, 80, 180, 90)
    $path.AddArc(176, 0, 80, 80, 270, 90)
    $path.AddArc(176, 176, 80, 80, 0, 90)
    $path.AddArc(0, 176, 80, 80, 90, 90)
    $path.CloseFigure()
    $bg = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#182638'))
    $graphics.FillPath($bg, $path)
    $left = [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml('#38C4AD'), 15)
    $right = [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml('#FF9C66'), 15)
    $screen = [System.Drawing.Pen]::new([System.Drawing.Color]::White, 12)
    $left.StartCap = $left.EndCap = $right.StartCap = $right.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $graphics.DrawArc($left, 25, 55, 76, 146, 110, 140)
    $graphics.DrawArc($right, 155, 55, 76, 146, -70, 140)
    $graphics.DrawRectangle($screen, 73, 80, 110, 82)
    $graphics.DrawLine($screen, 128, 163, 128, 184)
    $graphics.DrawLine($screen, 101, 186, 155, 186)
    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $images += ,@{ Size = $size; Bytes = $stream.ToArray() }
    foreach ($object in @($stream, $screen, $left, $right, $bg, $path, $graphics, $bitmap)) { $object.Dispose() }
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
