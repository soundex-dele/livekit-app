#pragma once
#include "session.hpp"
#include "emergency_hotkey.hpp"
namespace remote_app {
enum class Action {None,Start,Approve,Reject,Stop};
struct View {
  char url[512]="http://127.0.0.1:8080";
  EmergencyHotkey hotkey;
  std::string copied_code;
  double copied_until=0;
  Action draw(const Snapshot&);
};
}
