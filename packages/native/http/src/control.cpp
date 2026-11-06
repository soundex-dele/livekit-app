#include "av/http.hpp"

#include <chrono>
#include <exception>
#include <thread>

namespace av {
namespace sdk = livekit::sdk;

sdk::Result<sdk::Json> request_result(const std::string& base, const std::string& path,
                                      const std::string& account, const sdk::Json* body,
                                      const RequestOptions& options) {
  for (const auto* value : {&account, &options.organization, &options.resource_credential}) {
    if (value->find_first_of("\r\n") != std::string::npos) {
      return sdk::Result<sdk::Json>::failure(sdk::make_failure(
          sdk::ErrorDomain::lifecycle, sdk::ErrorCode::invalid_argument, "Invalid credential"));
    }
  }
  try {
    return sdk::Result<sdk::Json>::success(request(base, path, account, body, options));
  } catch (const std::exception& error) {
    return sdk::Result<sdk::Json>::failure(sdk::make_failure(
        sdk::ErrorDomain::lifecycle, sdk::ErrorCode::request_failed, error.what()));
  }
}

sdk::Result<sdk::Json> media_credentials(const std::string& base, const std::string& grant,
                                         const std::string& account, const RequestOptions& options,
                                         const std::atomic<bool>* stop) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
  const auto body = sdk::Json::object();
  for (;;) {
    if (stop && stop->load()) {
      return sdk::Result<sdk::Json>::failure(sdk::make_failure(
          sdk::ErrorDomain::lifecycle, sdk::ErrorCode::session_closed, "Connection cancelled"));
    }
    auto result = request_result(base, "/api/v1/av/rtc-grants/" + grant + "/credentials", account,
                                 &body, options);
    if (result || result.error().message.find("media_preparing") == std::string::npos ||
        std::chrono::steady_clock::now() >= deadline) {
      return result;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

sdk::Result<std::string> select_organization(const std::string& base, const std::string& account,
                                             const std::string& preferred) {
  auto result = request_result(base, "/api/v1/organizations", account);
  if (!result) {
    return sdk::Result<std::string>::failure(result.failure_record());
  }
  const auto organizations = result->value("items", sdk::Json::array());
  for (const auto& organization : organizations) {
    const auto id = organization.value("id", std::string{});
    if ((!preferred.empty() && id == preferred) ||
        (preferred.empty() && organizations.size() == 1)) {
      return sdk::Result<std::string>::success(id);
    }
  }
  return sdk::Result<std::string>::failure(
      sdk::make_failure(sdk::ErrorDomain::lifecycle, sdk::ErrorCode::invalid_argument,
                        organizations.empty() ? "账号尚未加入组织，请在网页中创建或加入组织"
                                              : "请在账号设置中填写要使用的组织 ID"));
}
}  // namespace av
