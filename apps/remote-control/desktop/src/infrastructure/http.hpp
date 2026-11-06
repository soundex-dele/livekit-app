#pragma once
#include <av/http.hpp>

namespace remote_app {
using av::request;

inline livekit::sdk::Json grant_view(livekit::sdk::Json value) {
  const auto remaining = value.value("lease_remaining_ms", int64_t(0));
  if (value.contains("grant")) {
    value = value["grant"];
  }
  value["grant_id"] = value.value("id", std::string{});
  if (value.value("state", std::string{}) == "active") {
    value["state"] = "approved";
  }
  value["lease_remaining_ms"] = remaining;
  return value;
}
}
