#include "subtitles.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace player_demo {
namespace {
constexpr size_t max_file_size = 8 * 1024 * 1024;
std::string trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r");
  if (first == value.npos) return {};
  return std::string(value.substr(first, value.find_last_not_of(" \t\r") - first + 1));
}
void validate_utf8(std::string_view text) {
  for (size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i++]);
    if (lead == 0) throw std::runtime_error("字幕包含无效字符，请保存为 UTF-8 编码。");
    if (lead < 0x80) continue;
    const int extra = lead >= 0xc2 && lead <= 0xdf ? 1 : lead >= 0xe0 && lead <= 0xef ? 2 : lead >= 0xf0 && lead <= 0xf4 ? 3 : -1;
    if (extra < 0 || i + extra > text.size()) throw std::runtime_error("字幕编码无法识别，请保存为 UTF-8 编码。");
    uint32_t cp = lead & ((1u << (6 - extra)) - 1);
    for (int j = 0; j < extra; ++j) {
      const auto byte = static_cast<unsigned char>(text[i++]);
      if ((byte & 0xc0) != 0x80) throw std::runtime_error("字幕编码无法识别，请保存为 UTF-8 编码。");
      cp = (cp << 6) | (byte & 63);
    }
    if (cp < (extra == 1 ? 0x80u : extra == 2 ? 0x800u : 0x10000u) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      throw std::runtime_error("字幕编码无法识别，请保存为 UTF-8 编码。");
  }
}
int64_t timestamp(const std::string& value) {
  static const std::regex pattern(R"(^(?:(\d{2,6}):)?([0-5]\d):([0-5]\d)[,.](\d{3})$)");
  std::smatch match;
  if (!std::regex_match(value, match, pattern)) throw std::runtime_error("字幕时间格式错误，应为 00:00:01,000 或 00:01.000。");
  const int64_t hours = match[1].matched ? std::stoll(match[1]) : 0;
  return ((hours * 3600 + std::stoll(match[2]) * 60 + std::stoll(match[3])) * 1000 + std::stoll(match[4])) * 1000;
}
std::string plain_text(std::string value) {
  // SRT/VTT inline markup is displayed as plain text, never interpreted as UI.
  static const std::regex tags(R"(</?[A-Za-z][^>]*>|<\d[\d:.]*>)");
  value = std::regex_replace(value, tags, "");
  for (const auto& [entity, replacement] : std::vector<std::pair<std::string, std::string>>{
       {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}, {"&quot;", "\""}, {"&#39;", "'"}, {"&amp;", "&"}}) {
    for (size_t pos = 0; (pos = value.find(entity, pos)) != std::string::npos; pos += replacement.size())
      value.replace(pos, entity.size(), replacement);
  }
  return trim(value);
}
}
bool Subtitles::accepts(const std::string& path) {
  auto extension = std::filesystem::u8path(path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".srt" || extension == ".vtt";
}
Subtitles Subtitles::parse(std::string_view input) {
  if (input.size() > max_file_size) throw std::runtime_error("字幕文件超过 8 MB，请选择较小的字幕文件。");
  validate_utf8(input);
  if (input.starts_with("\xef\xbb\xbf")) input.remove_prefix(3);
  std::istringstream stream{std::string(input)};
  std::vector<std::vector<std::string>> blocks;
  std::vector<std::string> block;
  for (std::string line; std::getline(stream, line);) {
    line = trim(line);
    if (line.empty()) { if (!block.empty()) { blocks.push_back(std::move(block)); block.clear(); } }
    else block.push_back(std::move(line));
  }
  if (!block.empty()) blocks.push_back(std::move(block));
  Subtitles result;
  for (size_t b = 0; b < blocks.size(); ++b) {
    const auto& lines = blocks[b];
    const auto& first = lines[0];
    if (b == 0 && (first == "WEBVTT" || first.starts_with("WEBVTT ") || first.starts_with("WEBVTT\t"))) continue;
    if (first == "NOTE" || first.starts_with("NOTE ") || first.starts_with("NOTE\t") || first == "STYLE" || first == "REGION") continue;
    const size_t time_line = first.find("-->") != first.npos ? 0 : 1;
    if (time_line >= lines.size()) throw std::runtime_error("字幕缺少时间轴，请检查 SRT / WebVTT 文件。");
    const auto arrow = lines[time_line].find("-->");
    if (arrow == std::string::npos) throw std::runtime_error("字幕缺少时间轴，请检查 SRT / WebVTT 文件。");
    auto end = trim(std::string_view(lines[time_line]).substr(arrow + 3));
    end = end.substr(0, end.find_first_of(" \t")); // WebVTT cue positioning is optional.
    SubtitleCue cue{timestamp(trim(std::string_view(lines[time_line]).substr(0, arrow))), timestamp(end), {}};
    if (cue.end_us <= cue.start_us) throw std::runtime_error("字幕结束时间必须晚于开始时间。");
    for (size_t i = time_line + 1; i < lines.size(); ++i) { if (!cue.text.empty()) cue.text += '\n'; cue.text += lines[i]; }
    if (cue.text.size() > 16384) throw std::runtime_error("单条字幕过长，请检查字幕文件。");
    cue.text = plain_text(std::move(cue.text));
    if (!cue.text.empty()) result.cues_.push_back(std::move(cue));
  }
  if (result.cues_.empty()) throw std::runtime_error("未找到可显示的字幕，请选择包含时间轴的 SRT / WebVTT 文件。");
  std::stable_sort(result.cues_.begin(), result.cues_.end(), [](const auto& a, const auto& b) { return a.start_us < b.start_us; });
  int64_t maximum = 0;
  for (const auto& cue : result.cues_) { maximum = std::max(maximum, cue.end_us); result.end_prefix_.push_back(maximum); }
  return result;
}
void Subtitles::load(const std::string& path) {
  if (!accepts(path)) throw std::runtime_error("请选择 .srt 或 .vtt 字幕文件。");
  std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("无法打开字幕文件，请检查路径与读取权限。");
  const auto size = file.tellg();
  if (size < 0 || size > static_cast<std::streamoff>(max_file_size)) throw std::runtime_error("字幕文件无法读取或超过 8 MB。");
  std::string text(static_cast<size_t>(size), '\0'); file.seekg(0);
  if (!file.read(text.data(), static_cast<std::streamsize>(text.size()))) throw std::runtime_error("读取字幕失败，请重新选择文件。");
  auto next = parse(text); next.path_ = path; *this = std::move(next);
}
std::string Subtitles::text_at(int64_t position_us) const {
  if (!enabled || position_us < 0) return {};
  const auto time = position_us - static_cast<int64_t>(std::clamp(delay_ms, -600000, 600000)) * 1000;
  if (time < 0) return {};
  const auto first = std::upper_bound(end_prefix_.begin(), end_prefix_.end(), time) - end_prefix_.begin();
  std::string text;
  for (size_t i = static_cast<size_t>(first); i < cues_.size() && cues_[i].start_us <= time; ++i) if (time < cues_[i].end_us) {
    if (!text.empty()) text += '\n'; text += cues_[i].text;
  }
  return text;
}
}
