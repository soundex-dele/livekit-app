#include "features/ui.hpp"
#include <algorithm>
#include <av/locale.hpp>
namespace screen_cast {
Action View::draw(const Snapshot&s,ImTextureID texture){
 Action action=Action::None;ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
 ImGui::Begin("局域网传屏",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
 ImGui::TextUnformatted("局域网传屏");ImGui::Separator();
 ImGui::Text("状态: %s",av::ui::state_label(s.state));ImGui::BeginDisabled(!s.finished);
 ImGui::InputText("业务地址",url,sizeof(url));ImGui::InputText("投屏码",code,sizeof(code),ImGuiInputTextFlags_CharsDecimal);
 if(selected<0||selected>=static_cast<int>(displays.size()))selected=0;
 if(ImGui::BeginCombo("显示器",displays.empty()?"未发现显示器":displays[selected].id.c_str())){
  for(int i=0;i<static_cast<int>(displays.size());++i){auto&d=displays[i];auto label=d.id+"  "+std::to_string(d.width)+"x"+std::to_string(d.height)+(d.primary?" (主屏)":"");if(ImGui::Selectable(label.c_str(),i==selected))selected=i;}ImGui::EndCombo();
 }
 if(ImGui::Button("刷新显示器"))action=Action::Refresh;ImGui::SameLine();
 ImGui::BeginDisabled(displays.empty());if(ImGui::Button("开始传屏"))action=Action::Start;ImGui::EndDisabled();ImGui::EndDisabled();
 ImGui::SameLine();ImGui::BeginDisabled(s.finished);if(ImGui::Button("停止"))action=Action::Stop;ImGui::EndDisabled();
 ImGui::TextWrapped("最高 1080p · 15 fps · 仅画面。最小化继续共享，关闭窗口停止。");
 if(!s.error.empty()){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.4f,.4f,1));av::ui::error_message(s.error);ImGui::PopStyleColor();}
 ImGui::Separator();
 if(texture&&s.preview){auto avail=ImGui::GetContentRegionAvail();float scale=std::min(avail.x/s.preview->width,avail.y/s.preview->height);if(scale>0)ImGui::Image(texture,{s.preview->width*scale,s.preview->height*scale});}
 else ImGui::TextUnformatted("本地预览将在连接后显示");
 ImGui::End();return action;
}
}
