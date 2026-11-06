#include <av/http.hpp>
#include <iostream>
using livekit::sdk::Json;

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "test URL required\n";
    return 1;
  }
  const std::string base = argv[1];
  auto result = av::request_result(base, "/get", "test-token");
  if (!result || !result->value("ok", false)) {
    return 2;
  }
  Json body{{"text", "你好，直播助手"}};
  auto posted = av::request_result(base, "/post", "test-token", &body, {"org-one", ""});
  if (!posted || *posted != body) {
    return 3;
  }
  auto guest = av::request_result(base, "/guest", "", nullptr, {"", "visitor-secret"});
  if (!guest || !guest->value("ok", false)) {
    return 4;
  }
  auto login = av::request_result(base, "/api/v1/auth/login", "", &body);
  if (!login || login->value("session_token", "") != "cookie-test-token") {
    return 5;
  }
  auto media = av::media_credentials(base, "test-grant", "test-token", {"org-one", ""});
  if (!media || media->value("media_token", "") != "rtc-test-token") {
    return 6;
  }
  for (auto path : {"/redirect", "/error", "/oversize", "/slow"}) {
    if (av::request_result(base, path, "test-token")) {
      return 7;
    }
  }
  if (av::request_result(base, "/get", "bad\r\ncredential")) {
    return 8;
  }
  auto stats = av::request_result(base, "/stats", "");
  if (!stats || stats->value("redirected", -1) != 0 || !stats->value("headers_ok", false) ||
      stats->value("grant_attempts", 0) != 2) {
    return 9;
  }
  std::cout << "HTTP transport, Foundation auth and RTC grant retry passed\n";
  return 0;
}
