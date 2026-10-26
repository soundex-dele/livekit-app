#pragma once
#include <livekit/remote_control.hpp>
namespace remote_app {
livekit::sdk::Json request(const std::string& base,const std::string& path,const std::string& credential,const livekit::sdk::Json* body=nullptr);
}
