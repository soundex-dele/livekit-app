cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(av_player_desktop VERSION 0.3.0 LANGUAGES C CXX)
  include(CTest)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/NativeSDK.cmake")
if(NOT TARGET LiveKit::Player)
  av_add_native_sdk(OFF ON)
endif()
# Isolate this shared SDL dependency from the RTC demo's static video-only SDL.
set(LIVEKIT_PLAYER_SDL_ROOT "" CACHE PATH "Optional separate SDL2 development prefix")
find_path(LIVEKIT_PLAYER_SDL_INCLUDE SDL.h HINTS "${LIVEKIT_PLAYER_SDL_ROOT}/include"
  "${LIVEKIT_FFMPEG_ROOT}/include/SDL2" PATH_SUFFIXES SDL2 REQUIRED)
find_library(LIVEKIT_PLAYER_SDL_LIBRARY NAMES SDL2 HINTS "${LIVEKIT_PLAYER_SDL_ROOT}/lib/x64"
  "${LIVEKIT_PLAYER_SDL_ROOT}/lib" "${LIVEKIT_FFMPEG_ROOT}/lib" REQUIRED)
include(FetchContent)
FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz
  URL_HASH SHA256=8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)
add_library(livekit_player_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(livekit_player_imgui PUBLIC "${LIVEKIT_PLAYER_SDL_INCLUDE}"
  ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(livekit_player_imgui PUBLIC "${LIVEKIT_PLAYER_SDL_LIBRARY}")
add_library(livekit_player_demo STATIC "${CMAKE_CURRENT_SOURCE_DIR}/session.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ui.cpp")
target_include_directories(livekit_player_demo PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(livekit_player_demo PUBLIC LiveKit::Player livekit_player_imgui)
if(WIN32)
  target_link_libraries(livekit_player_demo PRIVATE comdlg32)
endif()
add_executable(livekit_local_player "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
target_link_libraries(livekit_local_player PRIVATE livekit_player_demo utoolkit_logging)

# SDL may live in a separate development package rather than the FFmpeg prefix.
function(av_player_runtime target)
  livekit_player_runtime(${target})
  if(WIN32)
    find_file(AV_PLAYER_SDL_DLL SDL2.dll HINTS "${LIVEKIT_PLAYER_SDL_ROOT}/lib/x64"
      "${LIVEKIT_PLAYER_SDL_ROOT}/bin" "${LIVEKIT_FFMPEG_ROOT}/bin" REQUIRED)
    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${AV_PLAYER_SDL_DLL}" "$<TARGET_FILE_DIR:${target}>" VERBATIM)
  endif()
endfunction()
av_player_runtime(livekit_local_player)
if(BUILD_TESTING)
  find_program(LIVEKIT_PLAYER_FFMPEG_EXECUTABLE ffmpeg HINTS "${LIVEKIT_FFMPEG_ROOT}/bin" REQUIRED)
  add_test(NAME player_app_fixtures COMMAND ${CMAKE_COMMAND}
    "-DFFMPEG=${LIVEKIT_PLAYER_FFMPEG_EXECUTABLE}" "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/fixtures"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/player_fixtures.cmake")
  set_tests_properties(player_app_fixtures PROPERTIES FIXTURES_SETUP player_app_media TIMEOUT 30)
  add_test(NAME local_player_output COMMAND livekit_local_player "${CMAKE_CURRENT_BINARY_DIR}/fixtures/av.mp4" --auto-exit --hidden)
  set_tests_properties(local_player_output PROPERTIES FIXTURES_REQUIRED player_app_media TIMEOUT 15
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software")
  add_test(NAME local_player_empty COMMAND livekit_local_player --hidden --frames 5)
  set_tests_properties(local_player_empty PROPERTIES TIMEOUT 15
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software")
  add_executable(livekit_player_ui_tests tests/player_ui_tests.cpp)
  target_link_libraries(livekit_player_ui_tests PRIVATE livekit_player_demo)
  av_player_runtime(livekit_player_ui_tests)
  add_test(NAME local_player_ui COMMAND livekit_player_ui_tests "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
  set_tests_properties(local_player_ui PROPERTIES FIXTURES_REQUIRED player_app_media TIMEOUT 20
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software")
endif()
