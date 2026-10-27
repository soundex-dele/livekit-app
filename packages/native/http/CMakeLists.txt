add_library(av_http STATIC src/http.cpp)
target_include_directories(av_http PUBLIC include)
target_link_libraries(av_http PUBLIC LiveKit::SDK PRIVATE winhttp)
target_compile_options(av_http PRIVATE /utf-8)
