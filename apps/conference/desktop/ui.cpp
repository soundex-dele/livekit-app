#include "ui.hpp"
#include <av/locale.hpp>
#include "imgui.h"
#include "imgui_impl_sdlrenderer2.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <stdexcept>

namespace demo {
View::View() { std::snprintf(url.data(), url.size(), "ws://127.0.0.1:7880"); std::snprintf(room.data(), room.size(), "demo"); }
sdk::RoomOptions View::options() const {
  sdk::RoomOptions o; o.url = url.data(); o.room = room.data(); o.token = token.data(); o.audio_playout = audio;
  o.microphone = microphone;
  if (camera) o.media.push_back(camera_spec());
  for (int i = 0; i < std::clamp(video_count, 0, max_test_videos - (camera ? 1 : 0)); ++i) { sdk::MediaSpec m; m.demo = true; o.media.push_back(m); }
  if (tone) { sdk::MediaSpec m; m.demo = true; m.video = false; o.media.push_back(m); }
  return o;
}
sdk::MediaSpec View::camera_spec() const {
  if (camera_index < 0 || camera_index >= static_cast<int>(cameras.size())) throw std::runtime_error("请刷新并选择摄像头。");
  sdk::MediaSpec spec; spec.capture = sdk::CaptureKind::camera; spec.device_index = camera_index;
  spec.local_preview = true; return spec;
}
sdk::MediaSpec View::screen_spec() const {
  if (screen_index < 0 || screen_index >= static_cast<int>(screens.size())) throw std::runtime_error("请刷新并选择显示器。");
  const auto& display = screens[screen_index];
  const auto size = sdk::screen_capture_size(display.width, display.height);
  sdk::MediaSpec spec; spec.capture = sdk::CaptureKind::screen; spec.screen_id = display.id;
  spec.width = size.width; spec.height = size.height; spec.fps = 15; spec.local_preview = true; return spec;
}
void apply_style(float scale, float pixel_density) {
  ImGui::GetStyle() = ImGuiStyle{};
  ImGui::StyleColorsDark();
  auto& s = ImGui::GetStyle(); s.WindowPadding = {20, 20}; s.FramePadding = {10, 8};
  s.ItemSpacing = {12, 12}; s.WindowRounding = 0; s.ChildRounding = 8; s.FrameRounding = 5;
  s.Colors[ImGuiCol_WindowBg] = {0.055f, 0.068f, 0.09f, 1};
  s.Colors[ImGuiCol_ChildBg] = {0.085f, 0.10f, 0.13f, 1};
  s.Colors[ImGuiCol_Button] = {0.14f, 0.32f, 0.50f, 1};
  s.Colors[ImGuiCol_ButtonHovered] = {0.20f, 0.44f, 0.64f, 1};
  s.Colors[ImGuiCol_FrameBg] = {0.12f, 0.15f, 0.20f, 1};
  s.ScaleAllSizes(scale);
  auto& io = ImGui::GetIO();
  // Rasterize at backing-pixel resolution while retaining logical UI sizes.
  // The caller destroys the renderer's font texture before rebuilding fonts.
  pixel_density = std::clamp(pixel_density, 1.0f, 3.0f);
  io.Fonts->Clear();
  av::ui::load_chinese_font(16.0f * scale * pixel_density);
  io.FontGlobalScale = 1.0f / pixel_density;
}
void render_draw_data(SDL_Renderer* renderer) {
  // SDL's ImGui backend scales clipping, but expects the application to scale
  // vertex coordinates via the renderer (as in the upstream SDL example).
  const auto& io = ImGui::GetIO();
  if (SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y) != 0)
    throw std::runtime_error(SDL_GetError());
  ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}
Action View::draw(SDL_Renderer* renderer, const Snapshot& snapshot) {
  const float scale = ImGui::GetFontSize() / 16.0f;
  auto* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("视频会议", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
  Action action = Action::None;
  remove_video_sid.clear();
  ImGui::TextUnformatted("视频会议");
  ImGui::SameLine(); ImGui::TextDisabled("%s", state_name(snapshot.state));
  ImGui::Separator();
  // Enumerate hardware only following a user action; render tests use fixtures.
  const bool can_change = snapshot.finished || (snapshot.state == State::Connected && !snapshot.changing_video);
  ImGui::BeginDisabled(!can_change);
  if (ImGui::CollapsingHeader("设备与屏幕共享")) {
    if (ImGui::Button("刷新设备列表")) {
      error.clear();
      try { cameras = sdk::video_capture_devices(); camera_index = 0; }
      catch (const std::exception& e) { error = std::string("无法获取摄像头：") + e.what(); }
      try { screens = sdk::screen_capture_devices(); screen_index = 0; }
      catch (const std::exception& e) { if (error.empty()) error = std::string("无法获取显示器：") + e.what(); }
    }
    if (camera_index < 0 || camera_index >= static_cast<int>(cameras.size())) camera_index = 0;
    if (ImGui::BeginCombo("摄像头", cameras.empty() ? "请先刷新设备" : cameras[camera_index].name.c_str())) {
      for (int i = 0; i < static_cast<int>(cameras.size()); ++i) { ImGui::PushID(i);
        if (ImGui::Selectable(cameras[i].name.c_str(), i == camera_index)) camera_index = i; ImGui::PopID(); }
      ImGui::EndCombo();
    }
    if (snapshot.finished) {
      ImGui::BeginDisabled(cameras.empty()); ImGui::Checkbox("入会时开启摄像头", &camera); ImGui::EndDisabled();
      ImGui::Checkbox("入会时开启默认麦克风", &microphone);
      ImGui::TextWrapped("摄像头和麦克风仅在入会后启用，可在会中关闭。麦克风使用系统默认设备。");
    } else {
      ImGui::BeginDisabled(cameras.empty());
      if (ImGui::Button("开启所选摄像头")) { source = camera_spec(); action = Action::AddSource; }
      ImGui::EndDisabled();
      if (screen_index < 0 || screen_index >= static_cast<int>(screens.size())) screen_index = 0;
      if (ImGui::BeginCombo("共享显示器", screens.empty() ? "请先刷新设备" : screens[screen_index].name.c_str())) {
        for (int i = 0; i < static_cast<int>(screens.size()); ++i) { ImGui::PushID(i);
          const auto label = screens[i].name + "（" + std::to_string(screens[i].width) + "×" + std::to_string(screens[i].height) + "）";
          if (ImGui::Selectable(label.c_str(), i == screen_index)) screen_index = i; ImGui::PopID(); }
        ImGui::EndCombo();
      }
      ImGui::BeginDisabled(screens.empty() || action != Action::None);
      if (ImGui::Button("共享所选屏幕")) { source = screen_spec(); action = Action::AddSource; }
      ImGui::EndDisabled();
    }
  }
  ImGui::EndDisabled();
  if (snapshot.finished) {
    chat_text.fill(0); chat_revision_ = 0;
    textures_.clear();
    ImGui::TextUnformatted("加入会议");
    ImGui::TextWrapped("加入会议查看与会者画面，支持同时发布多路视频。");
    ImGui::SetNextItemWidth(std::min(620.0f * scale, ImGui::GetContentRegionAvail().x * .75f));
    ImGui::InputText("服务器地址", url.data(), url.size());
    ImGui::SetNextItemWidth(std::min(620.0f * scale, ImGui::GetContentRegionAvail().x * .75f));
    ImGui::InputText("会议名称", room.data(), room.size());
    ImGui::SetNextItemWidth(std::min(620.0f * scale, ImGui::GetContentRegionAvail().x * .75f));
    ImGui::InputText("入会凭证", token.data(), token.size(), ImGuiInputTextFlags_Password);
    ImGui::Checkbox("播放会议声音", &audio);
    ImGui::SetNextItemWidth(200 * scale);
    ImGui::SliderInt("测试视频路数", &video_count, 0, max_test_videos, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("发送测试音调", &tone);
    ImGui::TextDisabled("每位与会者需要独立的入会凭证。测试音调为 440 Hz。");
    ImGui::BeginDisabled(!url[0] || !room[0] || !token[0]);
    if (ImGui::Button("加入会议", {160 * scale, 0})) { error.clear(); action = Action::Join; }
    ImGui::EndDisabled();
    const auto& message = error.empty() ? snapshot.error : error;
    if (!message.empty()) { ImGui::Spacing(); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, .5f, .46f, 1)); av::ui::error_message(message); ImGui::PopStyleColor(); }
  } else {
    ImGui::Text("会议：%s", snapshot.room.c_str()); ImGui::SameLine();
    ImGui::TextDisabled("%zu 位与会者", snapshot.participants.size()); ImGui::SameLine();
    ImGui::BeginDisabled(snapshot.state == State::Disconnecting);
    if (ImGui::Button(snapshot.state == State::Connecting ? "取消连接" : "离开会议")) action = Action::Leave;
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::Checkbox("显示聊天", &show_chat);
    if (snapshot.state == State::Reconnecting) ImGui::TextWrapped("网络中断，正在尝试恢复连接。恢复前暂不可修改媒体，仍可离开会议。");
    if (snapshot.state == State::Disconnecting) ImGui::TextUnformatted("正在离开会议，请等待资源清理完成…");
    const float available = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("Members", {std::min(220 * scale, available * .28f), 0}, ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("我的音视频");
    const auto local = snapshot.participants.find(snapshot.local_sid);
    int video_count = 0;
    if (local != snapshot.participants.end()) for (const auto& t : local->second.tracks) if (t.kind == sdk::TrackKind::video) ++video_count;
    ImGui::TextDisabled("已发布 %d / %d 路", video_count, max_test_videos);
    const bool changing = snapshot.state != State::Connected || snapshot.changing_video || action != Action::None;
    ImGui::BeginDisabled(changing || video_count >= max_test_videos);
    if (ImGui::Button("添加测试视频")) action = Action::AddVideo;
    ImGui::EndDisabled();
    if (local != snapshot.participants.end()) for (const auto& t : local->second.tracks) {
      ImGui::PushID(t.sid.c_str());
      ImGui::TextWrapped("%s", t.source == sdk::TrackSource::microphone ? "麦克风" : t.name.c_str());
      ImGui::BeginDisabled(changing || action != Action::None);
      if (ImGui::SmallButton(t.muted ? "重新开启" : "暂时关闭")) { remove_video_sid = t.sid; mute_value = !t.muted; action = Action::Mute; }
      if (t.kind == sdk::TrackKind::video && ImGui::SmallButton("停止这路视频")) { remove_video_sid = t.sid; action = Action::RemoveVideo; }
      ImGui::EndDisabled(); ImGui::PopID();
    }
    if (snapshot.changing_video) ImGui::TextWrapped("正在更新视频…");
    if (!snapshot.video_error.empty()) av::ui::error_message(snapshot.video_error);
    ImGui::TextWrapped("已发布的视频对其他与会者可见。");
    ImGui::Separator();
    ImGui::TextUnformatted("与会者"); ImGui::Separator();
    for (const auto& [sid, p] : snapshot.participants) {
      ImGui::PushID(sid.c_str());
      ImGui::TextWrapped("%s%s", (p.name.empty() ? p.identity : p.name).c_str(), sid == snapshot.local_sid ? "（我）" : "");
      bool has_audio = false, muted = true;
      for (const auto& t : p.tracks) if (t.kind == sdk::TrackKind::audio) { has_audio = true; muted &= t.muted; }
      ImGui::TextDisabled("%s", !has_audio ? "未开启音频" : muted ? "已静音" : "音频已开启");
      ImGui::Separator(); ImGui::PopID();
    }
    ImGui::EndChild(); ImGui::SameLine();
    const float content_width = ImGui::GetContentRegionAvail().x;
    const bool split_chat = show_chat && content_width >= 650 * scale;
    if (!show_chat || split_chat) {
    ImGui::BeginChild("Videos", {split_chat ? content_width - 320 * scale - ImGui::GetStyle().ItemSpacing.x : 0, 0});
    auto views = tiles(snapshot);
    if (views.empty()) ImGui::TextWrapped("等待其他与会者加入并开启视频。");
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float width = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, std::min(3, static_cast<int>((width + gap) / (300 * scale + gap))));
    const float tile_width = std::max(1.0f, (width - gap * (columns - 1)) / columns);
    std::set<std::string> active;
    for (size_t i = 0; i < views.size(); ++i) {
      const auto& tile = views[i]; active.insert(tile.id);
      if (i % columns) ImGui::SameLine();
      ImGui::PushID(tile.id.c_str());
      ImGui::BeginChild("Tile", {tile_width, tile_width * 9 / 16 + 55 * scale}, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
      ImGui::TextUnformatted(tile.label.c_str());
      auto pos = ImGui::GetCursorScreenPos(); auto area = ImGui::GetContentRegionAvail();
      auto* draw = ImGui::GetWindowDrawList();
      draw->AddRectFilled(pos, {pos.x + area.x, pos.y + area.y}, IM_COL32(12, 15, 20, 255), 4 * scale);
      if (tile.frame) {
        auto& cached = textures_[tile.id];
        if (cached.last != tile.frame) { cached.texture.upload(renderer, *tile.frame); cached.last = tile.frame; }
        const auto fit = fit_video(pos.x, pos.y, area.x, area.y, cached.texture.width(), cached.texture.height());
        draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(cached.texture.get())), {fit.x, fit.y}, {fit.x + fit.w, fit.y + fit.h});
      } else {
        textures_.erase(tile.id);
        auto size = ImGui::CalcTextSize(tile.status.c_str());
        draw->AddText({pos.x + std::max(0.0f, (area.x - size.x) / 2), pos.y + area.y / 2}, IM_COL32(155, 171, 191, 255), tile.status.c_str());
      }
      ImGui::Dummy(area); ImGui::EndChild(); ImGui::PopID();
    }
    std::erase_if(textures_, [&](const auto& item) { return !active.contains(item.first); });
    ImGui::EndChild();
    }
    if (show_chat) {
      if (split_chat) ImGui::SameLine();
      const auto chat_action = draw_chat(snapshot, scale);
      if (action == Action::None) action = chat_action;
    }
    if (!error.empty()) av::ui::error_message(error);
  }
  ImGui::End(); return action;
}
Action View::draw_chat(const Snapshot& snapshot, float scale) {
  Action action = Action::None;
  ImGui::BeginChild("Chat", {0, 0}, ImGuiChildFlags_Borders);
  ImGui::TextUnformatted("会议聊天");
  ImGui::TextWrapped("发送给所有参会者。不提供已读回执。");
  const float footer = (snapshot.chat_error.empty() ? 190 : 245) * scale;
  ImGui::BeginChild("ChatHistory", {0, std::max(60 * scale, ImGui::GetContentRegionAvail().y - footer)}, ImGuiChildFlags_Borders);
  const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2 * scale;
  if (snapshot.chat.messages().empty()) ImGui::TextDisabled("还没有消息，打个招呼吧。");
  for (const auto& message : snapshot.chat.messages()) {
    ImGui::PushID(message.sender.c_str()); ImGui::PushID(message.id.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.5f, .75f, 1, 1));
    ImGui::TextWrapped("%s", message.name.c_str()); ImGui::PopStyleColor();
    ImGui::TextWrapped("%s", message.text.c_str());
    if (message.status != ChatStatus::Received) ImGui::TextDisabled("%s",
      message.status == ChatStatus::Pending ? "发送中…" : message.status == ChatStatus::Sent ? "已发送" : "发送结果未确认");
    ImGui::Separator(); ImGui::PopID(); ImGui::PopID();
  }
  if (at_bottom && chat_revision_ != snapshot.chat.revision()) ImGui::SetScrollHereY(1.0f);
  chat_revision_ = snapshot.chat.revision();
  ImGui::EndChild();
  ImGui::BeginDisabled(snapshot.state != State::Connected);
  ImGui::TextUnformatted("消息内容");
  const bool submit = ImGui::InputTextMultiline("##chat-text", chat_text.data(), chat_text.size(), {-1, 65 * scale}, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::BeginDisabled(snapshot.sending_chat || !chat_text[0]);
  if (ImGui::Button(snapshot.sending_chat ? "发送中…" : "发送", {90 * scale, 0}) || (submit && !snapshot.sending_chat && chat_text[0])) action = Action::SendChat;
  ImGui::EndDisabled(); ImGui::EndDisabled();
  ImGui::TextWrapped("Ctrl+Enter 发送，Enter 换行；最多 2000 字符。");
  if (!snapshot.chat_error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, .5f, .46f, 1));
    ImGui::TextWrapped("%s", snapshot.chat_error.c_str()); ImGui::PopStyleColor();
  }
  ImGui::TextDisabled("仅保留最近 200 条，离会后清空。");
  ImGui::EndChild(); return action;
}
}
