#include "av/http.hpp"
#import <Foundation/Foundation.h>
#include <stdexcept>
// Never forward bearer credentials to a redirected host.
@interface AVHTTPDelegate : NSObject <NSURLSessionTaskDelegate, NSURLSessionDataDelegate> {
@public
  NSMutableData *bytes;
  bool oversized;
  dispatch_semaphore_t ready;
  NSError *failure;
  NSURLResponse *response;
}
@end
@implementation AVHTTPDelegate
- (void)URLSession:(NSURLSession *)session
                          task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response
                    newRequest:(NSURLRequest *)request
             completionHandler:(void (^)(NSURLRequest *))completion {
  completion(nil);
}
- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)task
    didReceiveData:(NSData *)data {
  if (bytes.length + data.length > 65536) {
    oversized = true;
    [task cancel];
  } else
    [bytes appendData:data];
}
- (void)URLSession:(NSURLSession *)session
                    task:(NSURLSessionTask *)task
    didCompleteWithError:(NSError *)error {
  failure = error;
  response = task.response;
  dispatch_semaphore_signal(ready);
}
@end
namespace av {
livekit::sdk::Json request(const std::string &base, const std::string &path,
                           const std::string &credential, const livekit::sdk::Json *body) {
  @autoreleasepool {
    auto url = base + path;
    NSURL *address = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if (!address ||
        (![address.scheme isEqualToString:@"http"] && ![address.scheme isEqualToString:@"https"]))
      throw std::runtime_error("HTTP or HTTPS service URL required");
    if (credential.find_first_of("\r\n") != std::string::npos)
      throw std::runtime_error("Invalid credential");
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:address];
    request.HTTPMethod = body ? @"POST" : @"GET";
    [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
    [request setValue:@"desktop" forHTTPHeaderField:@"X-AV-Client"];
    if (!credential.empty())
      [request setValue:[NSString stringWithUTF8String:("Bearer " + credential).c_str()]
          forHTTPHeaderField:@"Authorization"];
    if (body) {
      auto raw = body->dump();
      request.HTTPBody = [NSData dataWithBytes:raw.data() length:raw.size()];
    }
    auto config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    config.timeoutIntervalForRequest = 2;
    config.timeoutIntervalForResource = 5;
    auto delegate = [AVHTTPDelegate new];
    delegate->bytes = [NSMutableData data];
    auto queue = [NSOperationQueue new];
    queue.maxConcurrentOperationCount = 1;
    NSURLSession *session = [NSURLSession sessionWithConfiguration:config
                                                          delegate:delegate
                                                     delegateQueue:queue];
    delegate->ready = dispatch_semaphore_create(0);
    NSURLSessionDataTask *task = [session dataTaskWithRequest:request];
    [task resume];
    auto timeout = dispatch_semaphore_wait(delegate->ready,
                                           dispatch_time(DISPATCH_TIME_NOW, 6 * NSEC_PER_SEC));
    [session invalidateAndCancel];
    if (timeout)
      throw std::runtime_error("Business API request timeout");
    if (delegate->oversized)
      throw std::runtime_error("Business API response too large");
    auto failure = delegate->failure;
    auto result = delegate->bytes;
    auto response = delegate->response;
    if (failure)
      throw std::runtime_error("Business API transport failure: " +
                               std::string(failure.localizedDescription.UTF8String));
    if (result.length > 65536)
      throw std::runtime_error("Business API response too large");
    auto *data = static_cast<const char *>(result.bytes);
    std::string raw(data ? data : "", result.length);
    auto status = ((NSHTTPURLResponse *)response).statusCode;
    if (status < 200 || status >= 300) {
      std::string error = "Business API status " + std::to_string(status);
      try {
        auto value = livekit::sdk::Json::parse(raw).at("error");
        error += ": " + (value.is_string() ? value.get<std::string>()
                                           : value.at("code").get<std::string>());
      } catch (...) {
      }
      throw std::runtime_error(error);
    }
    return livekit::sdk::Json::parse(raw);
  }
}
} // namespace av
