#include "permission.hpp"
#import <CoreGraphics/CoreGraphics.h>
namespace screen_cast {
bool requestScreenPermission() {
  return CGPreflightScreenCaptureAccess() || CGRequestScreenCaptureAccess();
}
} // namespace screen_cast
