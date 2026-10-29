#include "platform.hpp"
#include <windows.h>

#include <commdlg.h>
#include <fstream>
#include <wincrypt.h>

namespace studio_app::platform {
using Json = nlohmann::json;

std::string utf8(const std::filesystem::path& p) {
  auto s = p.u8string();
  return {s.begin(), s.end()};
}

std::filesystem::path default_settings() {
  wchar_t* env = nullptr;
  size_t n = 0;
  _wdupenv_s(&env, &n, L"LOCALAPPDATA");
  auto path = env ? std::filesystem::path(env) / L"AVPlatform" / L"LiveStudio"
                  : std::filesystem::path("studio-settings");
  free(env);
  return path;
}

std::string browse(bool save) {
  wchar_t file[2048]{};
  OPENFILENAMEW o{};
  o.lStructSize = sizeof(o);
  o.lpstrFile = file;
  o.nMaxFile = 2048;
  o.lpstrFilter = save ? L"MP4 视频\0*.mp4\0\0"
                       : L"媒体文件\0*.png;*.jpg;*.jpeg;*.bmp;*.webp;*.mp4;*.mkv;*.mov;*."
                         L"webm\0所有文件\0*.*\0\0";
  o.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  o.lpstrDefExt = save ? L"mp4" : nullptr;
  if (save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))
    return utf8(file);
  return {};
}

nlohmann::json load_account(const std::filesystem::path& settings_) {
  if (!std::filesystem::exists(settings_))
    return nullptr;
  std::ifstream f(settings_, std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(f)), {});
  if (bytes.size() > 16384)
    throw std::runtime_error("账号配置过大");
  DATA_BLOB in{DWORD(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())}, out{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
    throw std::runtime_error("无法解密账号凭证");
  try {
    auto j = Json::parse(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return j;
  } catch (...) {
    LocalFree(out.pbData);
    throw;
  }
}

void save_account(const std::filesystem::path& settings_, const Json& account) {
  if (!settings_.parent_path().empty())
    std::filesystem::create_directories(settings_.parent_path());
  auto text = account.dump();
  DATA_BLOB in{DWORD(text.size()), reinterpret_cast<BYTE*>(text.data())}, out{};
  if (!CryptProtectData(&in, L"AV Live Studio", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &out))
    throw std::runtime_error("无法加密账号凭证");
  std::ofstream f(settings_, std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<char*>(out.pbData), out.cbData);
  LocalFree(out.pbData);
  if (!f)
    throw std::runtime_error("无法保存账号凭证");
}
}  // namespace studio_app::platform
