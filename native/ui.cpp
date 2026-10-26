#include "ui.hpp"
#include <imgui.h>
namespace remote_app {
Action View::draw(const Snapshot& state){
  Action action=Action::None;auto* viewport=ImGui::GetMainViewport();ImGui::SetNextWindowPos(viewport->WorkPos);ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("Remote assistance",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
  ImGui::TextUnformatted("AV Platform | Remote assistance");ImGui::Separator();
  ImGui::TextWrapped("Share your primary screen and allow keyboard / mouse control only after you approve a request.");
  ImGui::Spacing();ImGui::BeginDisabled(!state.finished);ImGui::InputText("Service URL",url,sizeof(url));if(ImGui::Button("Create connection code"))action=Action::Start;ImGui::EndDisabled();
  ImGui::Text("State: %s",state.state.c_str());
  if(!state.code.empty()&&!state.finished){ImGui::Text("Connection code: %s",state.code.c_str());ImGui::TextUnformatted("Code expires after 5 minutes. Share it with your helper.");}
  if(state.state=="pending"){
    ImGui::Separator();ImGui::TextWrapped("Request from: %s",state.name.c_str());ImGui::TextWrapped("This is a self-reported name, not a verified account.");
    ImGui::TextWrapped("Scope: primary screen, keyboard and mouse. Lease: 10 seconds, renewed while connected.");
    if(ImGui::Button("Approve control"))action=Action::Approve;ImGui::SameLine();if(ImGui::Button("Reject"))action=Action::Reject;
  }
  if(state.state=="approved"){ImGui::TextColored(ImVec4(1,.65f,.2f,1),"REMOTE CONTROL ACTIVE");ImGui::TextWrapped("Controller: %s (unverified name)",state.name.c_str());}
  if(!state.finished){ImGui::Spacing();ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.65f,.12f,.15f,1));if(ImGui::Button("STOP / close connection",ImVec2(-1,48)))action=Action::Stop;ImGui::PopStyleColor();}
  ImGui::TextUnformatted("Emergency stop: Ctrl + Alt + F12");
  if(!state.error.empty())ImGui::TextWrapped("Error: %s",state.error.c_str());
  ImGui::End();return action;
}
}
