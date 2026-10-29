#pragma once

namespace remote_app {
struct EmergencyHotkey {
  const char* label = nullptr;
  unsigned long error = 0;

  bool available() const {
    return label != nullptr;
  }
};

// F12 is reserved for the Windows debugger. Keep selection independent of the
// native registration call so conflict/failure tests never register desktop keys.
template <class Register>
EmergencyHotkey register_emergency_hotkey(Register&& attempt) {
  struct Candidate {
    unsigned key;
    const char* label;
  };

  constexpr Candidate candidates[] = {
      {0x79, "Ctrl + Alt + F10"},  // VK_F10
      {0x78, "Ctrl + Alt + F9"},   // VK_F9
  };
  EmergencyHotkey result;
  for (const auto& candidate : candidates) {
    if (attempt(candidate.key, result.error)) {
      result.label = candidate.label;
      result.error = 0;
      break;
    }
  }
  return result;
}
}  // namespace remote_app
