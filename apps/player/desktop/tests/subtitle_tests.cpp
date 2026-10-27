#include "subtitles.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void rejects(std::string_view text) {
  try { (void)player_demo::Subtitles::parse(text); }
  catch (const std::runtime_error&) { return; }
  throw std::runtime_error("Malformed subtitles were accepted");
}
}
int main() {
  using player_demo::Subtitles;
  try {
    const std::string srt = "\xef\xbb\xbf" "2\r\n00:00:02,000 --> 00:00:03,000\r\n第二句\r\n\r\n"
      "1\r\n00:00:01,000 --> 00:00:02,000\r\n<b>中文字幕</b>\r\nEnglish &amp; 中文\r\n\r\n"
      "3\r\n00:00:01,500 --> 00:00:02,500\r\n重叠字幕\r\n";
    auto captions = Subtitles::parse(srt);
    require(captions.size() == 3, "SRT BOM/CRLF or cue count");
    require(captions.text_at(999999).empty(), "Cue appeared before start");
    require(captions.text_at(1000000) == "中文字幕\nEnglish & 中文", "Unicode, markup or inclusive start");
    require(captions.text_at(1500000) == "中文字幕\nEnglish & 中文\n重叠字幕", "Overlapping captions lost");
    require(captions.text_at(2000000) == "重叠字幕\n第二句", "Exclusive end or stable chronological ordering");
    require(captions.text_at(3000000).empty(), "Caption remained after its end");
    require(captions.text_at(1000000) == "中文字幕\nEnglish & 中文", "Backward seek retained old cursor");
    captions.delay_ms = 500;
    require(captions.text_at(1000000).empty() && !captions.text_at(1500000).empty(), "Positive delay direction");
    captions.delay_ms = -500;
    require(!captions.text_at(500000).empty(), "Negative delay direction");
    captions.enabled = false; require(captions.text_at(1500000).empty(), "Subtitle toggle");
    auto vtt = Subtitles::parse("WEBVTT\nLanguage: zh\n\nNOTE test\nignored\n\nSTYLE\n::cue { color: red; }\n\n"
      "caption-id\n00:01.000 --> 00:02.000 align:start position:10%\n<v speaker><c.green>你好</c></v> &lt;字幕&gt;\n");
    require(vtt.size() == 1 && vtt.text_at(1000000) == "你好 <字幕>", "WebVTT identifiers, settings or plain text");
    require(Subtitles::accepts("字幕.SRT") && Subtitles::accepts("a.vtt") && !Subtitles::accepts("a.mp4"), "Drop file routing");
    rejects(""); rejects("1\n00:99:00,000 --> 00:00:02,000\nx");
    rejects("1\n00:00:02,000 --> 00:00:01,000\nx");
    rejects("1\n00:00:01,000 --> 00:00:01,000\nx");
    rejects("not subtitles"); rejects(std::string("\xc0\xaf", 2)); rejects(std::string("\xed\xa0\x80", 3));
    rejects(std::string("\xf4\x90\x80\x80", 4)); rejects(std::string("a\0b", 3));
    const auto folder = std::filesystem::current_path() / "subtitle-fixtures";
    std::filesystem::create_directories(folder);
    const auto path = folder / std::filesystem::u8path("中文字幕.srt");
    { std::ofstream file(path, std::ios::binary); file << srt; }
    const auto u8 = path.u8string(); const std::string filename(u8.begin(), u8.end());
    Subtitles loaded; loaded.load(filename); loaded.delay_ms = 250;
    bool failed = false;
    try { loaded.load((folder / "missing.srt").string()); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "Missing file accepted");
    require(loaded.size() == 3 && loaded.path() == filename && loaded.delay_ms == 250, "Failed replacement destroyed current subtitles");
    loaded.clear(); require(loaded.size() == 0 && loaded.path().empty() && loaded.delay_ms == 0, "Clear failed");
    std::cout << "PASS: SRT/VTT, UTF-8, timing, overlap, seek, delay, toggle and transactional file load\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
