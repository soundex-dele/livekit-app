#pragma once
#include "session.hpp"
#include "video_texture.hpp"
#include <array>

namespace demo {
enum class Action { None, Join, Leave, AddVideo, AddSource, RemoveVideo, Mute, SendChat };
class View {
 public:
  std::array<char, 1024> url{};
  std::array<char, 256> room{};
  std::array<char, 16384> token{};
  bool audio = true, tone = false;
  bool microphone = false, camera = false;
  int camera_index = 0, screen_index = 0;
  std::vector<sdk::VideoCaptureDevice> cameras;
  std::vector<sdk::ScreenCaptureDevice> screens;
  sdk::MediaSpec source;
  bool mute_value = false;
  int video_count = 0;
  std::string remove_video_sid;
  std::string error;
  std::array<char, 8001> chat_text{};
  bool show_chat = true;
  View();
  Action draw(SDL_Renderer* renderer, const Snapshot& snapshot);
  sdk::RoomOptions options() const;
  sdk::MediaSpec camera_spec() const;
  sdk::MediaSpec screen_spec() const;
 private:
  struct Cached {
    VideoTexture texture;
    std::shared_ptr<const sdk::VideoFrame> last;
  };
  std::map<std::string, Cached> textures_;
  uint64_t chat_revision_ = 0;
  Action draw_chat(const Snapshot& snapshot, float scale);
};
void apply_style(float scale, float pixel_density = 1.0f);
void render_draw_data(SDL_Renderer* renderer);
}
