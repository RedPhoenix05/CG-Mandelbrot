param(
    [string]$ConfigPath = "mandelbrot.ini"
)

$settings = @{}
if (Test-Path $ConfigPath)
{
    Get-Content $ConfigPath | ForEach-Object {
        if ($_ -match '^\s*#' -or $_ -notmatch '=') { return }
        $parts = $_.Split('=', 2)
        $settings[$parts[0].Trim()] = $parts[1].Trim()
    }
}

$captureDir = if ($settings.ContainsKey('captureDirectory')) { $settings['captureDirectory'] } else { 'captures' }
$videoFps = if ($settings.ContainsKey('videoFps')) { [int]$settings['videoFps'] } else { 60 }
$outputFile = if ($settings.ContainsKey('videoOutputFile')) { $settings['videoOutputFile'] } else { 'output/mandelbrot.mp4' }
$videoCrf = if ($settings.ContainsKey('videoCrf')) { [int]$settings['videoCrf'] } else { 18 }

$inputPattern = Join-Path $captureDir 'frame_%06d.bmp'
$outputDir = Split-Path -Parent $outputFile
if (![string]::IsNullOrWhiteSpace($outputDir))
{
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
}

$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if (-not $ffmpeg)
{
    Write-Error 'ffmpeg not found in PATH.'
    exit 1
}

$arguments = @(
    '-y',
    '-framerate', $videoFps,
    '-i', $inputPattern,
    '-c:v', 'libx264',
    '-pix_fmt', 'yuv420p',
    '-crf', $videoCrf,
    $outputFile
)

& ffmpeg @arguments
if ($LASTEXITCODE -ne 0)
{
    exit $LASTEXITCODE
}

Write-Host "Video generated: $outputFile"
