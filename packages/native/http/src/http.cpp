#include "av/http.hpp"
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <stdexcept>

namespace av {
namespace {
struct Handle {
  HINTERNET value{};

  ~Handle() {
    if (value)
      WinHttpCloseHandle(value);
  }

  operator HINTERNET() const {
    return value;
  }
};

std::wstring wide(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                              nullptr, 0);
  if (!n)
    throw std::runtime_error("Invalid UTF-8 URL");
  std::wstring result(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n);
  return result;
}

void require(bool ok) {
  if (!ok)
    throw std::runtime_error("Business API transport failure (" + std::to_string(GetLastError()) +
                             ")");
}
}  // namespace

livekit::sdk::Json request(const std::string& base, const std::string& path,
                           const std::string& credential, const livekit::sdk::Json* body,
                           const RequestOptions& options) {
  auto url = wide(base + path);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  require(WinHttpCrackUrl(url.c_str(), 0, 0, &parts));
  if (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)
    throw std::runtime_error("HTTP or HTTPS service URL required");
  auto host = std::wstring(parts.lpszHostName, parts.dwHostNameLength),
       route = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.dwExtraInfoLength)
    route.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  Handle session{WinHttpOpen(L"AVPlatform/1", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0)};
  require(session.value != nullptr);
  require(WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000));
  Handle connection{WinHttpConnect(session, host.c_str(), parts.nPort, 0)};
  require(connection.value != nullptr);
  Handle req{WinHttpOpenRequest(connection, body ? L"POST" : L"GET", route.c_str(), nullptr,
                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
  require(req.value != nullptr);
  DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  require(WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)));
  std::wstring headers = L"Content-Type: application/json\r\nX-Foundation-Request: 1\r\n";
  if (!credential.empty())
    headers += L"Authorization: Bearer " + wide(credential) + L"\r\n";
  if (!options.organization.empty()) {
    headers += L"X-Organization-ID: " + wide(options.organization) + L"\r\n";
  }
  if (!options.resource_credential.empty()) {
    headers += L"X-AV-Resource-Credential: " + wide(options.resource_credential) + L"\r\n";
  }
  auto raw = body ? body->dump() : std::string();
  require(WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(headers.size()),
                             raw.empty() ? WINHTTP_NO_REQUEST_DATA : raw.data(),
                             static_cast<DWORD>(raw.size()), static_cast<DWORD>(raw.size()), 0));
  require(WinHttpReceiveResponse(req, nullptr));
  DWORD status = 0, size = sizeof(status);
  require(WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                              WINHTTP_NO_HEADER_INDEX));
  std::string result;
  char buffer[4096];
  DWORD read = 0;
  do {
    require(WinHttpReadData(req, buffer, sizeof(buffer), &read));
    result.append(buffer, read);
    if (result.size() > 65536)
      throw std::runtime_error("Business API response too large");
  } while (read);
  if (status < 200 || status >= 300) {
    std::string message = "Business API status " + std::to_string(status);
    try {
      auto error = livekit::sdk::Json::parse(result).at("error");
      message += ": " + (error.is_string() ? error.get<std::string>()
                                           : error.at("code").get<std::string>());
    } catch (...) {
    }
    throw std::runtime_error(message);
  }
  auto value = livekit::sdk::Json::parse(result);
  if (path == "/api/v1/auth/login" || path == "/api/v1/auth/register" ||
      path == "/api/v1/auth/refresh") {
    DWORD length = 0;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                        &length, WINHTTP_NO_HEADER_INDEX);
    if (length > 0 && length < 65536) {
      std::wstring cookie(length / sizeof(wchar_t), L'\0');
      if (WinHttpQueryHeaders(req, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX,
                              cookie.data(), &length, WINHTTP_NO_HEADER_INDEX)) {
        const auto start = cookie.find(L"pf_session=");
        if (start != std::wstring::npos) {
          const auto end = cookie.find(L';', start);
          const auto token = cookie.substr(start + 11, end - start - 11);
          std::string session_token;
          session_token.reserve(token.size());
          for (const auto character : token) {
            if (character > 0 && character < 128) {
              session_token.push_back(static_cast<char>(character));
            }
          }
          value["session_token"] = session_token;
        }
      }
    }
  }
  return value;
}
}  // namespace av
