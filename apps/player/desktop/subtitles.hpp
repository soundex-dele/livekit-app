#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace player_demo {
struct SubtitleCue { int64_t start_us, end_us; std::string text; };
class Subtitles {
 public:
  static Subtitles parse(std::string_view text);
  static bool accepts(const std::string& path);
  void load(const std::string& path); // A failed replacement preserves the current subtitles.
  void clear() { *this = Subtitles{}; }
  std::string text_at(int64_t position_us) const;
  size_t size() const { return cues_.size(); }
  const std::string& path() const { return path_; }
  bool enabled = true;
  int delay_ms = 0; // Positive delay displays subtitles later.
 private:
  std::vector<SubtitleCue> cues_;
  std::vector<int64_t> end_prefix_;
  std::string path_;
};
}
