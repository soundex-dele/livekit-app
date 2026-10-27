#include "features/remote_control/ui.hpp"
#include <imgui.h>
#include <av/locale.hpp>
namespace remote_app {
Action View::draw(const Snapshot& state){
  Action action=Action::None;auto* viewport=ImGui::GetMainViewport();ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("远程协助",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
  ImGui::TextUnformatted("远程协助");ImGui::Separator();
  ImGui::TextWrapped("由你批准后才会共享主屏，允许对方操作键鼠及发送文字。");
  ImGui::Spacing();ImGui::BeginDisabled(!state.finished);ImGui::InputText("服务地址",url,sizeof(url));if(ImGui::Button("生成连接码"))action=Action::Start;ImGui::EndDisabled();
  ImGui::Text("状态：%s",av::ui::state_label(state.state));
  if(!state.code.empty()&&!state.finished){
    ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("连接码：");ImGui::SameLine();
    auto code=state.code;
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000000000").x+ImGui::GetStyle().FramePadding.x*2+16);
    ImGui::InputText("##connection-code",code.data(),code.size()+1,ImGuiInputTextFlags_ReadOnly|ImGuiInputTextFlags_AutoSelectAll);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("选中文字后按 Ctrl+C 复制。");
    ImGui::SameLine();
    if(ImGui::Button("复制")){
      ImGui::SetClipboardText(state.code.c_str());copied_code=state.code;copied_until=ImGui::GetTime()+2;
    }
    if(copied_code==state.code&&ImGui::GetTime()<copied_until){ImGui::SameLine();ImGui::TextUnformatted("已复制");}
    ImGui::TextUnformatted("连接码 5 分钟内有效，请发给协助者。");
  }
  if(state.state=="pending"){
    ImGui::Separator();ImGui::TextWrapped("请求者：%s",state.name.c_str());ImGui::TextWrapped("此名称由对方填写，未经账号认证。");
    ImGui::TextWrapped("批准后对方可查看主屏、操作键鼠，并向当前输入框发送文字；不共享剪贴板和文件。断开连接会结束授权。");
    if(ImGui::Button("允许控制"))action=Action::Approve;ImGui::SameLine();if(ImGui::Button("拒绝"))action=Action::Reject;
  }
  if(state.state=="approved"){ImGui::TextColored(ImVec4(1,.65f,.2f,1),"对方正在控制你的电脑");ImGui::TextWrapped("控制者：%s（未认证名称）",state.name.c_str());}
  if(!state.finished){ImGui::Spacing();ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.65f,.12f,.15f,1));if(ImGui::Button("立即停止控制",ImVec2(-1,48)))action=Action::Stop;ImGui::PopStyleColor();}
  if(hotkey.available())ImGui::Text("紧急停止快捷键：%s",hotkey.label);
  else {
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.65f,.2f,1));
    ImGui::TextWrapped("全局快捷键不可用（系统错误 %lu），请点击停止按钮或关闭窗口。",hotkey.error);
    ImGui::PopStyleColor();
  }
  av::ui::error_message(state.error);
  ImGui::End();return action;
}
}
