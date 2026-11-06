cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(av_ui_qt LANGUAGES CXX)
  include(CTest)
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
add_library(av_ui_qt STATIC src/ui.cpp src/window_theme.cpp av_ui_assets.qrc)
set_target_properties(av_ui_qt PROPERTIES AUTORCC ON)
target_include_directories(av_ui_qt PUBLIC include)
target_link_libraries(av_ui_qt PUBLIC Qt6::Widgets)
if(WIN32)
  target_link_libraries(av_ui_qt PRIVATE dwmapi user32)
endif()
target_compile_features(av_ui_qt PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(av_ui_qt PRIVATE /utf-8)
endif()
if(BUILD_TESTING)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
  add_executable(av_ui_qt_tests tests/ui_tests.cpp)
  set_target_properties(av_ui_qt_tests PROPERTIES AUTOMOC ON)
  target_link_libraries(av_ui_qt_tests PRIVATE av_ui_qt Qt6::Test)
  if(MSVC)
    target_compile_options(av_ui_qt_tests PRIVATE /utf-8)
  endif()
  add_test(NAME shared_ui_widgets COMMAND av_ui_qt_tests)
  add_test(NAME shared_ui_widgets_hidpi COMMAND av_ui_qt_tests)
  set_tests_properties(shared_ui_widgets PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
  set_tests_properties(shared_ui_widgets_hidpi PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2" TIMEOUT 30)
  if(WIN32)
    set_tests_properties(shared_ui_widgets shared_ui_widgets_hidpi PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_executable(av_ui_window_theme_tests tests/window_theme_tests.cpp)
    set_target_properties(av_ui_window_theme_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(av_ui_window_theme_tests PRIVATE av_ui_qt Qt6::Test dwmapi user32)
    if(MSVC)
      target_compile_options(av_ui_window_theme_tests PRIVATE /utf-8)
    endif()
    add_test(NAME shared_ui_window_theme COMMAND "${CMAKE_COMMAND}"
      "-DTEST_EXECUTABLE=$<TARGET_FILE:av_ui_window_theme_tests>"
      "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/window-theme-results.txt"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_window_theme_test.cmake")
    set_tests_properties(shared_ui_window_theme PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=windows"
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
      SKIP_REGULAR_EXPRESSION "SKIP[ ]+:"
      TIMEOUT 30)
  endif()
endif()
