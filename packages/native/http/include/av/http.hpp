#pragma once
#include <livekit/auth.hpp>
#include <livekit/result.hpp>
#include <atomic>

namespace av {
struct RequestOptions {
  std::string organization;
  std::string resource_credential;
};

livekit::sdk::Json request(const std::string& base, const std::string& path,
                           const std::string& credential, const livekit::sdk::Json* body = nullptr,
                           const RequestOptions& options = {});
// Converts the legacy native transport's exception boundary into an explicit
// result. Secrets are never placed into an error or a URL.
livekit::sdk::Result<livekit::sdk::Json> request_result(const std::string& base,
                                                        const std::string& path,
                                                        const std::string& account,
                                                        const livekit::sdk::Json* body = nullptr,
                                                        const RequestOptions& options = {});
livekit::sdk::Result<livekit::sdk::Json> media_credentials(const std::string& base,
                                                           const std::string& grant,
                                                           const std::string& account,
                                                           const RequestOptions& options = {},
                                                           const std::atomic<bool>* stop = nullptr);
livekit::sdk::Result<std::string> select_organization(const std::string& base,
                                                      const std::string& account,
                                                      const std::string& preferred = {});
}
