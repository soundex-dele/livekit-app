#pragma once
#include "domain/snapshot.hpp"
#include <imgui.h>
namespace screen_cast {
enum class Action {None,Start,Stop,Refresh};
struct View {
 char url[512]="http://127.0.0.1:8080",code[16]="";
 std::vector<livekit::sdk::ScreenCaptureDevice> displays;
 int selected=0;
 Action draw(const Snapshot&,ImTextureID texture=0);
};
}
