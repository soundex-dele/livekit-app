#include <av/http.hpp>
#include <iostream>
#include <stdexcept>
using livekit::sdk::Json;

void check(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}

int main(int argc, char** argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("test URL required");
    std::string base = argv[1];
    auto result = av::request(base, "/get", "test-token");
    check(result.at("ok") == true, "GET JSON");
    Json body{{"text", "你好，直播助手"}};
    check(av::request(base, "/post", "test-token", &body) == body, "UTF-8 POST body");
    for (auto path : {"/redirect", "/error", "/oversize", "/slow"}) {
      bool rejected = false;
      try {
        av::request(base, path, "test-token");
      } catch (const std::exception&) {
        rejected = true;
      }
      check(rejected, "unsafe or failed request accepted");
    }
    auto stats = av::request(base, "/stats", "");
    check(stats.at("redirected") == 0, "redirect leaked credentials");
    check(stats.at("headers_ok") == true, "request headers");
    std::cout << "HTTP transport passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
