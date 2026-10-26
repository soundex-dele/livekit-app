#pragma once
#include "session.hpp"
namespace remote_app {
enum class Action {None,Start,Approve,Reject,Stop};
struct View {char url[512]="http://127.0.0.1:8080";Action draw(const Snapshot&);};
}
