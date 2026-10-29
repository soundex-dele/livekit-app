#pragma once
#include <livekit/auth.hpp>

namespace av {
livekit::sdk::Json request(const std::string& base, const std::string& path,
                           const std::string& credential, const livekit::sdk::Json* body = nullptr);
}
