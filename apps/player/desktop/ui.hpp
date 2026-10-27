#pragma once
#include "session.hpp"
#include "imgui.h"
#include <array>

namespace player_demo {
void apply_style(float scale);
void load_font(float scale);
std::string format_time(int64_t microseconds);
struct View {
  void draw(Session& session, SDL_Window* window);
  void browse(Session& session, SDL_Window* window);
  void toggle_fullscreen(SDL_Window* window);
  void browse_subtitles(Session& session, SDL_Window* window);
  void show_subtitle_settings() { subtitle_popup_ = true; }
  bool fullscreen = false;
 private:
  float scale_ = 1;
  bool seek_editing_ = false, path_popup_ = false;
  float seek_seconds_ = 0;
  std::array<char, 4096> path_input_{};
  bool subtitle_popup_ = false;
  std::array<char, 4096> subtitle_input_{};
};
}
