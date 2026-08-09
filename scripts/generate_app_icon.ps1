[CmdletBinding()]
param(
    [string]$OutputDir = "resources",
    [string]$PngName = "app_icon.png",
    [string]$IcoName = "app_icon.ico"
)

$ErrorActionPreference = "Stop"

try {
    Add-Type -AssemblyName System.Drawing
} catch {
    Add-Type -AssemblyName System.Drawing.Common
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outputPath = Join-Path $repoRoot $OutputDir
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

function New-RoundedRectanglePath {
    param(
        [float]$X,
        [float]$Y,
        [float]$Width,
        [float]$Height,
        [float]$Radius
    )

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $diameter = $Radius * 2.0
    $path.AddArc($X, $Y, $diameter, $diameter, 180, 90)
    $path.AddArc($X + $Width - $diameter, $Y, $diameter, $diameter, 270, 90)
    $path.AddArc($X + $Width - $diameter, $Y + $Height - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($X, $Y + $Height - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function Fill-Circle {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Brush]$Brush,
        [float]$CenterX,
        [float]$CenterY,
        [float]$Diameter
    )

    $radius = $Diameter / 2.0
    $Graphics.FillEllipse($Brush, $CenterX - $radius, $CenterY - $radius, $Diameter, $Diameter)
}

function New-IconBitmap {
    param([int]$Size)

    $bitmap = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)

    try {
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $graphics.Clear([System.Drawing.Color]::Transparent)

        $scale = $Size / 256.0

        $shadowPath = New-RoundedRectanglePath -X (26.0 * $scale) -Y (28.0 * $scale) -Width (204.0 * $scale) -Height (204.0 * $scale) -Radius (46.0 * $scale)
        try {
            $shadowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(32, 30, 48, 63))
            $graphics.FillPath($shadowBrush, $shadowPath)
        } finally {
            if ($shadowBrush) { $shadowBrush.Dispose() }
            $shadowPath.Dispose()
        }

        $backgroundPath = New-RoundedRectanglePath -X (18.0 * $scale) -Y (16.0 * $scale) -Width (204.0 * $scale) -Height (204.0 * $scale) -Radius (46.0 * $scale)
        try {
            $bgRect = [System.Drawing.RectangleF]::new((18.0 * $scale), (16.0 * $scale), (204.0 * $scale), (204.0 * $scale))
            $bgBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
                $bgRect,
                [System.Drawing.Color]::FromArgb(255, 217, 123, 63),
                [System.Drawing.Color]::FromArgb(255, 241, 178, 97),
                45.0)
            $graphics.FillPath($bgBrush, $backgroundPath)

            $ringPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(90, 255, 255, 255), (3.0 * $scale))
            $graphics.DrawPath($ringPen, $backgroundPath)
        } finally {
            if ($ringPen) { $ringPen.Dispose() }
            if ($bgBrush) { $bgBrush.Dispose() }
            $backgroundPath.Dispose()
        }

        $cardPath = New-RoundedRectanglePath -X (48.0 * $scale) -Y (52.0 * $scale) -Width (132.0 * $scale) -Height (132.0 * $scale) -Radius (22.0 * $scale)
        try {
            $cardBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(245, 255, 253, 249))
            $cardPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 226, 212, 196), (2.4 * $scale))
            $graphics.FillPath($cardBrush, $cardPath)
            $graphics.DrawPath($cardPen, $cardPath)
        } finally {
            if ($cardPen) { $cardPen.Dispose() }
            if ($cardBrush) { $cardBrush.Dispose() }
            $cardPath.Dispose()
        }

        $headerPath = New-RoundedRectanglePath -X (48.0 * $scale) -Y (52.0 * $scale) -Width (132.0 * $scale) -Height (28.0 * $scale) -Radius (22.0 * $scale)
        try {
            $headerBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 241, 230, 214))
            $graphics.FillPath($headerBrush, $headerPath)
        } finally {
            if ($headerBrush) { $headerBrush.Dispose() }
            $headerPath.Dispose()
        }

        $gridPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 228, 214, 196), (2.0 * $scale))
        try {
            foreach ($x in 81, 114, 147) {
                $graphics.DrawLine($gridPen, $x * $scale, 82.0 * $scale, $x * $scale, 175.0 * $scale)
            }
            foreach ($y in 106, 130, 154) {
                $graphics.DrawLine($gridPen, 57.0 * $scale, $y * $scale, 171.0 * $scale, $y * $scale)
            }
        } finally {
            $gridPen.Dispose()
        }

        $routePen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 38, 122, 145), (14.0 * $scale))
        $routePen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $routePen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $routePen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
        try {
            $points = [System.Drawing.PointF[]]@(
                ([System.Drawing.PointF]::new((72.0 * $scale), (102.0 * $scale))),
                ([System.Drawing.PointF]::new((116.0 * $scale), (102.0 * $scale))),
                ([System.Drawing.PointF]::new((116.0 * $scale), (139.0 * $scale))),
                ([System.Drawing.PointF]::new((156.0 * $scale), (139.0 * $scale))),
                ([System.Drawing.PointF]::new((156.0 * $scale), (171.0 * $scale)))
            )
            $graphics.DrawLines($routePen, $points)

            $nodeBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 38, 122, 145))
            try {
                Fill-Circle -Graphics $graphics -Brush $nodeBrush -CenterX (72.0 * $scale) -CenterY (102.0 * $scale) -Diameter (17.0 * $scale)
                Fill-Circle -Graphics $graphics -Brush $nodeBrush -CenterX (116.0 * $scale) -CenterY (102.0 * $scale) -Diameter (17.0 * $scale)
                Fill-Circle -Graphics $graphics -Brush $nodeBrush -CenterX (116.0 * $scale) -CenterY (139.0 * $scale) -Diameter (17.0 * $scale)
                Fill-Circle -Graphics $graphics -Brush $nodeBrush -CenterX (156.0 * $scale) -CenterY (139.0 * $scale) -Diameter (17.0 * $scale)
            } finally {
                $nodeBrush.Dispose()
            }

            $arrowBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 38, 122, 145))
            try {
                $arrowPoints = [System.Drawing.PointF[]]@(
                    ([System.Drawing.PointF]::new((156.0 * $scale), (181.0 * $scale))),
                    ([System.Drawing.PointF]::new((144.0 * $scale), (164.0 * $scale))),
                    ([System.Drawing.PointF]::new((168.0 * $scale), (164.0 * $scale)))
                )
                $graphics.FillPolygon($arrowBrush, $arrowPoints)
            } finally {
                $arrowBrush.Dispose()
            }
        } finally {
            $routePen.Dispose()
        }

        $badgeRect = [System.Drawing.RectangleF]::new((148.0 * $scale), (146.0 * $scale), (60.0 * $scale), (60.0 * $scale))
        $badgeBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
            $badgeRect,
            [System.Drawing.Color]::FromArgb(255, 93, 166, 130),
            [System.Drawing.Color]::FromArgb(255, 63, 135, 102),
            60.0)
        try {
            $graphics.FillEllipse($badgeBrush, $badgeRect)
            $badgePen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(140, 255, 255, 255), (2.0 * $scale))
            try {
                $graphics.DrawEllipse($badgePen, $badgeRect)
            } finally {
                $badgePen.Dispose()
            }
        } finally {
            $badgeBrush.Dispose()
        }

        $checkPen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, (8.0 * $scale))
        $checkPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $checkPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        try {
            $graphics.DrawLines(
                $checkPen,
                [System.Drawing.PointF[]]@(
                    ([System.Drawing.PointF]::new((164.0 * $scale), (176.0 * $scale))),
                    ([System.Drawing.PointF]::new((176.0 * $scale), (188.0 * $scale))),
                    ([System.Drawing.PointF]::new((194.0 * $scale), (166.0 * $scale)))
                ))
        } finally {
            $checkPen.Dispose()
        }

        $sparkBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(80, 255, 255, 255))
        try {
            Fill-Circle -Graphics $graphics -Brush $sparkBrush -CenterX (74.0 * $scale) -CenterY (42.0 * $scale) -Diameter (18.0 * $scale)
            Fill-Circle -Graphics $graphics -Brush $sparkBrush -CenterX (199.0 * $scale) -CenterY (70.0 * $scale) -Diameter (12.0 * $scale)
        } finally {
            $sparkBrush.Dispose()
        }
    } finally {
        $graphics.Dispose()
    }

    return $bitmap
}

function Convert-BitmapToPngBytes {
    param([System.Drawing.Bitmap]$Bitmap)

    $stream = New-Object System.IO.MemoryStream
    try {
        $Bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return $stream.ToArray()
    } finally {
        $stream.Dispose()
    }
}

function Write-IcoFile {
    param(
        [hashtable]$PngBytesBySize,
        [string]$Path
    )

    $sizes = @($PngBytesBySize.Keys | Sort-Object)
    $fileStream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    $writer = New-Object System.IO.BinaryWriter($fileStream)
    try {
        $writer.Write([UInt16]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]$sizes.Count)

        $offset = 6 + (16 * $sizes.Count)
        foreach ($size in $sizes) {
            $bytes = [byte[]]$PngBytesBySize[$size]
            $writer.Write([byte]($(if ($size -ge 256) { 0 } else { $size })))
            $writer.Write([byte]($(if ($size -ge 256) { 0 } else { $size })))
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]32)
            $writer.Write([UInt32]$bytes.Length)
            $writer.Write([UInt32]$offset)
            $offset += $bytes.Length
        }

        foreach ($size in $sizes) {
            $writer.Write([byte[]]$PngBytesBySize[$size])
        }
    } finally {
        $writer.Dispose()
        $fileStream.Dispose()
    }
}

$pngPath = Join-Path $outputPath $PngName
$icoPath = Join-Path $outputPath $IcoName
$sizes = @(16, 24, 32, 48, 64, 128, 256)
$pngBytesBySize = @{}

foreach ($size in $sizes) {
    $bitmap = New-IconBitmap -Size $size
    try {
        $pngBytes = Convert-BitmapToPngBytes -Bitmap $bitmap
        $pngBytesBySize[$size] = $pngBytes
        if ($size -eq 256) {
            [System.IO.File]::WriteAllBytes($pngPath, $pngBytes)
        }
    } finally {
        $bitmap.Dispose()
    }
}

Write-IcoFile -PngBytesBySize $pngBytesBySize -Path $icoPath

Write-Host "Generated PNG: $pngPath"
Write-Host "Generated ICO: $icoPath"
