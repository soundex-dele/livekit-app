#pragma once
#include <livekit/media.hpp>
#include <memory>

namespace screen_cast {
struct Snapshot {
  std::string state = "idle", error;
  bool finished = true;
  std::shared_ptr<const livekit::sdk::VideoFrame> preview;
};
}  // namespace screen_cast
