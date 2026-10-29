#include "platform.hpp"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

namespace remote_app {
struct Platform::Impl {
  std::function<void()> stop;
  EventHotKeyRef key = nullptr;
  EventHandlerRef handler = nullptr;
  std::string label;

  static OSStatus pressed(EventHandlerCallRef, EventRef, void* data) {
    static_cast<Impl*>(data)->stop();
    return noErr;
  }
};

Platform::Platform(std::function<void()> stop) : impl_(std::make_unique<Impl>()) {
  impl_->stop = std::move(stop);
  EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
  if (InstallApplicationEventHandler(&Impl::pressed, 1, &type, impl_.get(), &impl_->handler) !=
      noErr)
    return;
  EventHotKeyID id{'avrc', 1};
  for (auto key : {109, 101})
    if (RegisterEventHotKey(key, controlKey | optionKey, id, GetApplicationEventTarget(), 0,
                            &impl_->key) == noErr) {
      impl_->label = key == 109 ? "Control + Option + F10" : "Control + Option + F9";
      break;
    }
}

Platform::~Platform() {
  if (impl_->key)
    UnregisterEventHotKey(impl_->key);
  if (impl_->handler)
    RemoveEventHandler(impl_->handler);
}

std::string Platform::hotkey() const {
  return impl_->label;
}

void Platform::request_screen_permission() {
  CGRequestScreenCaptureAccess();
  [[NSWorkspace sharedWorkspace]
      openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple."
                                   @"preference.security?Privacy_ScreenCapture"]];
}

void Platform::request_input_permission() {
  NSDictionary* options = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt : @YES};
  AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
  [[NSWorkspace sharedWorkspace]
      openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple."
                                   @"preference.security?Privacy_Accessibility"]];
}
}  // namespace remote_app
