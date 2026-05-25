param(
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$sourcePath = Join-Path $RepositoryRoot 'icon.png'
$icoPath = Join-Path $RepositoryRoot 'app.ico'
$faviconPath = Join-Path $RepositoryRoot 'www\favicon.png'

if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "Icon source was not found: $sourcePath"
}

$source = [System.Drawing.Image]::FromFile($sourcePath)
try {
    function New-ScaledPngBytes {
        param([int]$Size)

        $bitmap = [System.Drawing.Bitmap]::new(
            $Size,
            $Size,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::Transparent)
            $graphics.CompositingQuality =
                [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode =
                [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode =
                [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.SmoothingMode =
                [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $graphics.DrawImage($source, 0, 0, $Size, $Size)

            $stream = [System.IO.MemoryStream]::new()
            try {
                $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
                return $stream.ToArray()
            } finally {
                $stream.Dispose()
            }
        } finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }

    $faviconBytes = New-ScaledPngBytes -Size 64
    [System.IO.File]::WriteAllBytes($faviconPath, $faviconBytes)

    $sizes = @(16, 24, 32, 48, 64, 128, 256)
    $frames = foreach ($size in $sizes) {
        [PSCustomObject]@{
            Size = $size
            Bytes = (New-ScaledPngBytes -Size $size)
        }
    }

    $output = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($output)
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$frames.Count)

        $offset = 6 + (16 * $frames.Count)
        foreach ($frame in $frames) {
            $dimension = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
            $writer.Write([byte]$dimension)
            $writer.Write([byte]$dimension)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$frame.Bytes.Length)
            $writer.Write([uint32]$offset)
            $offset += $frame.Bytes.Length
        }

        foreach ($frame in $frames) {
            $writer.Write([byte[]]$frame.Bytes)
        }
        $writer.Flush()
        [System.IO.File]::WriteAllBytes($icoPath, $output.ToArray())
    } finally {
        $writer.Dispose()
        $output.Dispose()
    }
} finally {
    $source.Dispose()
}

Write-Host 'Updated app.ico and www/favicon.png from icon.png.'
