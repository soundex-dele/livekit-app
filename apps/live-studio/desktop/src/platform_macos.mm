#include "platform.hpp"
#import <AppKit/AppKit.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace studio_app::platform {
std::filesystem::path default_settings() {
  @autoreleasepool {
    auto paths =
        NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    if (!paths.count)
      throw std::runtime_error("无法定位 Application Support 目录");
    return std::filesystem::path(paths.firstObject.UTF8String) / "AVPlatform" / "LiveStudio";
  }
}

std::string browse(bool save) {
  @autoreleasepool {
    NSSavePanel* panel = save ? [NSSavePanel savePanel] : [NSOpenPanel openPanel];
    if (save) {
      panel.allowedContentTypes = @[ UTTypeMPEG4Movie ];
      panel.nameFieldStringValue = @"直播录制.mp4";
    } else {
      NSOpenPanel* open = (NSOpenPanel*)panel;
      open.canChooseDirectories = NO;
      open.allowsMultipleSelection = NO;
      open.allowedContentTypes = @[ UTTypeImage, UTTypeMovie ];
    }
    if ([panel runModal] != NSModalResponseOK)
      return {};
    return panel.URL.path.UTF8String;
  }
}

static NSMutableDictionary* query(const std::filesystem::path& path) {
  auto account = std::filesystem::absolute(path).lexically_normal().string();
  return [@{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : @"org.avplatform.live-studio",
    (__bridge id)kSecAttrAccount : [NSString stringWithUTF8String:account.c_str()]
  } mutableCopy];
}

static void require(OSStatus status) {
  if (status != errSecSuccess)
    throw std::runtime_error("钥匙串访问失败：" + std::to_string(status));
}

nlohmann::json load_account(const std::filesystem::path& path) {
  @autoreleasepool {
    auto item = query(path);
    item[(__bridge id)kSecReturnData] = @YES;
    item[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
    // Reading at startup must never display a modal Keychain prompt.
    auto context = [LAContext new];
    context.interactionNotAllowed = YES;
    item[(__bridge id)kSecUseAuthenticationContext] = context;
    CFTypeRef result = nullptr;
    auto status = SecItemCopyMatching((__bridge CFDictionaryRef)item, &result);
    if (status == errSecItemNotFound)
      return nullptr;
    require(status);
    NSData* bytes = CFBridgingRelease(result);
    if (bytes.length > 16384)
      throw std::runtime_error("账号配置过大");
    auto* data = static_cast<const char*>(bytes.bytes);
    return nlohmann::json::parse(data, data + bytes.length);
  }
}

void save_account(const std::filesystem::path& path, const nlohmann::json& account) {
  @autoreleasepool {
    auto item = query(path);
    if (account.value("token", "").empty()) {
      auto status = SecItemDelete((__bridge CFDictionaryRef)item);
      if (status != errSecItemNotFound)
        require(status);
      return;
    }
    auto text = account.dump();
    NSData* bytes = [NSData dataWithBytes:text.data() length:text.size()];
    auto updates = @{(__bridge id)kSecValueData : bytes};
    auto status = SecItemUpdate((__bridge CFDictionaryRef)item, (__bridge CFDictionaryRef)updates);
    if (status == errSecItemNotFound) {
      item[(__bridge id)kSecValueData] = bytes;
      item[(__bridge id)kSecAttrAccessible] =
          (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
      status = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);
    }
    require(status);
  }
}
}  // namespace studio_app::platform
