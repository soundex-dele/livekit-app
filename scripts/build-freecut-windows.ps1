param(
    [Parameter(Mandatory = $true)][string]$FFmpegRoot,
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$FFmpegInclude = '',
    [string]$BuildDirectory = '',
    [string]$InstallDirectory = '',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$appRoot = Split-Path $PSScriptRoot -Parent
$ffmpegPrefix = (Resolve-Path -LiteralPath $FFmpegRoot).Path
if ((Split-Path $ffmpegPrefix -Leaf) -eq 'bin') { $ffmpegPrefix = Split-Path $ffmpegPrefix -Parent }
$qtPrefix = (Resolve-Path -LiteralPath $QtRoot).Path
if (-not $FFmpegInclude) { $FFmpegInclude = Join-Path $ffmpegPrefix 'include' }
$headers = (Resolve-Path -LiteralPath $FFmpegInclude).Path
if (-not (Test-Path -LiteralPath (Join-Path $headers 'libavformat/avformat.h'))) {
    throw 'Supply matching FFmpeg development headers through -FFmpegInclude.'
}
if (-not (Test-Path -LiteralPath (Join-Path $qtPrefix 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw 'QtRoot must contain Qt 6.8+ MSVC x64 Widgets, Multimedia and Test.'
}
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $appRoot 'out/freecut-windows' }
if (-not $InstallDirectory) { $InstallDirectory = Join-Path $BuildDirectory 'dist' }
& cmake -S (Join-Path $appRoot 'apps/freecut/desktop') -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 `
    -U 'LIVEKIT_FFMPEG_*' -U 'LIVEKIT_EDITOR_FFMPEG_EXECUTABLE' -U 'Qt6*' `
    -DBUILD_TESTING=ON "-DLIVEKIT_FFMPEG_ROOT=$ffmpegPrefix" "-DLIVEKIT_FFMPEG_INCLUDE=$headers" `
    "-DCMAKE_PREFIX_PATH=$qtPrefix" '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL'
if ($LASTEXITCODE -ne 0) { throw 'FreeCut configure failed' }
& cmake --build $BuildDirectory --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'FreeCut build failed' }
$previousPath = $env:PATH
$previousFonts = $env:QT_QPA_FONTDIR
try {
    $env:PATH = (Join-Path $qtPrefix 'bin') + ';' + $env:PATH
    $env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
    & ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'FreeCut tests failed' }
    & cmake --install $BuildDirectory --config $Configuration --prefix $InstallDirectory
    if ($LASTEXITCODE -ne 0) { throw 'FreeCut deployment failed' }
    $env:PATH = $previousPath
    $deployedApp = Join-Path $InstallDirectory 'bin/freecut.exe'
    $smoke = Start-Process -FilePath $deployedApp -ArgumentList @('--hidden', '--smoke') -WindowStyle Hidden -Wait -PassThru
    if ($smoke.ExitCode -ne 0) { throw 'Deployed FreeCut offscreen smoke test failed' }
} finally {
    $env:PATH = $previousPath
    $env:QT_QPA_FONTDIR = $previousFonts
}
Write-Host "FreeCut deployment: $InstallDirectory"
