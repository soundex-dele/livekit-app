#pragma once
#include <string>
namespace remote_app {
struct Snapshot {
  std::string state="idle",code,name,error,id;
  bool finished=true;
};
}
