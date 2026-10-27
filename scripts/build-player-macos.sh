#!/bin/bash
set -euo pipefail
app_root="$(cd "$(dirname "$0")/.." && pwd)"
player_qt_root="${QT_ROOT:-/usr/local}"
player_ffmpeg_root="${FFMPEG_ROOT:-}"
player_build_dir="${BUILD_DIRECTORY:-$app_root/out/player-macos-qt}"
player_install_dir="${INSTALL_DIRECTORY:-$player_build_dir/dist}"
if [[ -z "$player_ffmpeg_root" ]]; then
  echo 'Set FFMPEG_ROOT to a matching FFmpeg development prefix (for example: brew --prefix ffmpeg).' >&2
  exit 2
fi
cmake -S "$app_root/apps/player/desktop" -B "$player_build_dir" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  "-DCMAKE_PREFIX_PATH=$player_qt_root" "-DLIVEKIT_FFMPEG_ROOT=$player_ffmpeg_root"
cmake --build "$player_build_dir" --parallel "${BUILD_JOBS:-4}"
ctest --test-dir "$player_build_dir" --output-on-failure
cmake --install "$player_build_dir" --prefix "$player_install_dir"
python3 "$app_root/scripts/check-player-macos-bundle.py" "$player_install_dir/livekit_local_player.app"
# offscreen uses internal rendering and never opens a desktop window.
"$player_install_dir/livekit_local_player.app/Contents/MacOS/livekit_local_player" --hidden --frames 5
printf 'Player deployment: %s\n' "$player_install_dir/livekit_local_player.app"
