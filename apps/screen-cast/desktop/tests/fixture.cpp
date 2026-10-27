#include "services/session.hpp"
#include <iostream>
// Synthetic frames only. Never enumerates or captures the user's displays.
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  screen_cast::Session session(true);
  session.start(argv[1], argv[2],
                {"fixture", "Synthetic display", 0, 0, 1280, 720, true});
  const auto started = std::chrono::steady_clock::now();
  const auto stopAfter =
      std::chrono::seconds(argc > 3 ? std::stoi(argv[3]) : 120);
  bool preview = false, stopping = false;
  while (std::chrono::steady_clock::now() - started <
         std::chrono::seconds(120)) {
    auto s = session.snapshot();
    if (s.preview && !preview) {
      std::cout << "PREVIEW " << s.preview->width << "x" << s.preview->height
                << std::endl;
      preview = true;
    }
    if (s.finished) {
      if (!s.error.empty())
        std::cerr << s.error << std::endl;
      return s.error.empty() ? 0 : 1;
    }
    if (!stopping && std::chrono::steady_clock::now() - started >= stopAfter) {
      stopping = true;
      std::cout << "STOP_REQUESTED" << std::endl;
      session.stop();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  session.stop();
  return 3;
}
