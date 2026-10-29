#pragma once
#include <string>

namespace remote_app {
struct Snapshot {
  std::string state = "idle", code, name, error, id, expires;
  bool finished = true;
};
}  // namespace remote_app
