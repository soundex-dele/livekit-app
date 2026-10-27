#pragma once
#include <imgui.h>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace av::ui {
// Call before the first frame, or after destroying the renderer font texture.
// Keep glyph storage alive for the lifetime of the ImGui font atlas.
inline void load_chinese_font(float pixels = 18) {
  std::vector<std::filesystem::path> candidates;
  if (const auto* custom = std::getenv("AV_UI_FONT")) candidates.emplace_back(custom);
#ifdef _WIN32
  if (const auto* windows = std::getenv("WINDIR")) {
    candidates.emplace_back(std::filesystem::path(windows) / "Fonts/msyh.ttc");
    candidates.emplace_back(std::filesystem::path(windows) / "Fonts/simhei.ttf");
  }
#elif defined(__APPLE__)
  candidates.emplace_back("/System/Library/Fonts/PingFang.ttc");
  candidates.emplace_back("/System/Library/Fonts/Supplemental/Songti.ttc");
#else
  candidates.emplace_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
  candidates.emplace_back("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc");
#endif
  auto& io = ImGui::GetIO();
  static ImVector<ImWchar> ranges;
  if (ranges.empty()) {
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
    builder.AddText("←→×…"); builder.BuildRanges(&ranges);
  }
  for (const auto& path : candidates) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) continue;
    ImFontConfig config; config.OversampleH = config.OversampleV = 1;
    const auto utf8 = path.u8string();
    if (auto* font = io.Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(utf8.c_str()), pixels, &config, ranges.Data)) {
      io.FontDefault = font; return;
    }
  }
  throw std::runtime_error("未找到中文字体，请安装中文字体或设置 AV_UI_FONT 为字体文件路径。");
}
inline const char* state_label(std::string_view state) {
  if (state == "idle") return "未连接";
  if (state == "creating") return "正在生成连接码";
  if (state == "waiting") return "等待连接";
  if (state == "pending") return "等待本机批准";
  if (state == "approved") return "正在远程控制";
  if (state == "connecting") return "正在连接";
  if (state == "paired") return "已配对，等待画面";
  if (state == "streaming") return "正在传屏";
  if (state == "stopping") return "正在停止";
  if (state == "ended" || state == "revoked") return "已结束";
  if (state == "expired") return "已过期";
  if (state == "rejected") return "已拒绝";
  return "正在更新状态";
}
inline const char* error_label(std::string_view detail) {
  if (detail.find("file.source") != detail.npos) return "无法打开文件，请检查路径、文件是否存在以及读取权限。";
  if (detail.find("demuxer") != detail.npos || detail.find("decoder") != detail.npos) return "无法读取媒体内容，文件可能损坏或格式不受支持。";
  if (detail.find("code_unavailable") != detail.npos) return "连接码无效、已使用或已过期，请重新生成。";
  if (detail.find("rate_limited") != detail.npos) return "操作过于频繁，请稍后重试。";
  if (detail.find("media_unavailable") != detail.npos) return "媒体服务暂不可用，请检查服务后重试。";
  if (detail.find("heartbeat_timeout") != detail.npos) return "连接超时，请检查网络后重新连接。";
  if (detail.find("control_connect_timeout") != detail.npos) return "控制通道未能及时建立，请检查网络后重新申请。";
  if (detail.find("Display") != detail.npos || detail.find("display") != detail.npos) return "显示器状态已改变，请刷新列表并重新选择。";
  if (detail.find("401") != detail.npos || detail.find("403") != detail.npos || detail.find("unauthorized") != detail.npos) return "连接凭证无效或权限不足，请重新获取授权。";
  return "操作未完成，请检查连接或输入后重试。";
}
inline void error_message(const std::string& detail) {
  if (detail.empty()) return;
  const bool localized = static_cast<unsigned char>(detail.front()) >= 0x80;
  ImGui::TextWrapped("%s", localized ? detail.c_str() : error_label(detail));
  if (ImGui::TreeNode("错误详情")) { ImGui::TextWrapped("%s", detail.c_str()); ImGui::TreePop(); }
}
}
