param(
    [Parameter(Mandatory = $true)][string]$FFmpegRoot,
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$BuildDirectory = '',
    [string]$InstallDirectory = '',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$appRoot = Split-Path $PSScriptRoot -Parent
$ffmpegPrefix = (Resolve-Path -LiteralPath $FFmpegRoot).Path
if ((Split-Path $ffmpegPrefix -Leaf) -eq 'bin') { $ffmpegPrefix = Split-Path $ffmpegPrefix -Parent }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $appRoot 'out/player-windows-qt' }
$dependencyDirectory = Join-Path $appRoot 'out/player-deps'
New-Item -ItemType Directory -Force -Path $dependencyDirectory | Out-Null

function Get-VerifiedArchive([string]$Url, [string]$Path, [string]$Sha256) {
    if (-not (Test-Path -LiteralPath $Path)) { Invoke-WebRequest $Url -OutFile $Path }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Sha256) {
        throw "Dependency checksum mismatch: $Path"
    }
}

$ffmpegInclude = Join-Path $ffmpegPrefix 'include'
if (-not (Test-Path -LiteralPath (Join-Path $ffmpegInclude 'libavformat/avformat.h'))) {
    # This particular 7.1 distribution contains DLLs/import libs but omits the
    # public headers. Use the exact upstream revision reported by its executable.
    $version = & (Join-Path $ffmpegPrefix 'bin/ffmpeg.exe') -version
    if ($LASTEXITCODE -ne 0 -or $version[0] -notmatch 'b08d796') {
        throw 'Missing FFmpeg headers. Supply the matching development package; automatic recovery supports b08d796 only.'
    }
    $revision = 'b08d7969c550a804a59511c7b83f2dd8cc0499b8'
    $archive = Join-Path $dependencyDirectory 'ffmpeg-b08d796.tar.gz'
    Get-VerifiedArchive "https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/$revision" $archive `
        '02FA6D9827DA3B6786E4DF821218CC036DB2B4481E7F48267C2DCDA695633AFC'
    $ffmpegInclude = Join-Path $dependencyDirectory "FFmpeg-$revision"
    if (-not (Test-Path -LiteralPath (Join-Path $ffmpegInclude 'libavformat/avformat.h'))) {
        & tar -xf $archive -C $dependencyDirectory
        if ($LASTEXITCODE -ne 0) { throw 'FFmpeg header extraction failed' }
    }
    # Public generated configuration for this MSVC little-endian x64 build.
    @'
#ifndef AVUTIL_AVCONFIG_H
#define AVUTIL_AVCONFIG_H
#define AV_HAVE_BIGENDIAN 0
#define AV_HAVE_FAST_UNALIGNED 1
#endif
'@ | Set-Content -LiteralPath (Join-Path $ffmpegInclude 'libavutil/avconfig.h') -Encoding ascii
}
$qtPrefix = (Resolve-Path -LiteralPath $QtRoot).Path
if (-not (Test-Path -LiteralPath (Join-Path $qtPrefix 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw 'QtRoot must point to a Qt 6.8+ MSVC x64 installation with Widgets, Multimedia, Svg and Test.'
}
if (-not $InstallDirectory) { $InstallDirectory = Join-Path $BuildDirectory 'dist' }
# Clear discovery caches so changing FFmpeg versions cannot silently reuse old
# headers, import libraries or the fixture generator from another installation.
& cmake -S (Join-Path $appRoot 'apps/player/desktop') -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 `
    -U 'LIVEKIT_FFMPEG_*' -U 'LIVEKIT_PLAYER_FFMPEG_EXECUTABLE' -U 'Qt6*' `
    -DBUILD_TESTING=ON `
    "-DLIVEKIT_FFMPEG_ROOT=$ffmpegPrefix" "-DLIVEKIT_FFMPEG_INCLUDE=$ffmpegInclude" `
    "-DCMAKE_PREFIX_PATH=$qtPrefix" `
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL'
if ($LASTEXITCODE -ne 0) { throw 'Player configure failed' }
& cmake --build $BuildDirectory --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Player build failed' }
& ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Player tests failed' }

& cmake --install $BuildDirectory --config $Configuration --prefix $InstallDirectory
if ($LASTEXITCODE -ne 0) { throw 'Player deployment failed' }
$smokeTest = Start-Process -FilePath (Join-Path $InstallDirectory 'bin/livekit_local_player.exe') -ArgumentList @('--hidden', '--frames', '5') -Wait -PassThru
if ($smokeTest.ExitCode -ne 0) { throw 'Deployed player smoke test failed' }
Write-Host "Player deployment: $InstallDirectory"
