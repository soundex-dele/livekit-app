#include "audio_samples_macos.hpp"
#include "internal.hpp"
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#include <cmath>
#include <iostream>
using namespace livekit::sdk;
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void audioTests() {
  studio_detail::MacAudioSamples converter;
  for (int rate : {44100, 48000})
    for (bool planar : {false, true}) {
      const int frames = rate / 100, channels = 2;
      AudioStreamBasicDescription format{};
      format.mSampleRate = rate;
      format.mFormatID = kAudioFormatLinearPCM;
      format.mFormatFlags =
          kAudioFormatFlagsNativeFloatPacked | (planar ? kAudioFormatFlagIsNonInterleaved : 0);
      format.mBitsPerChannel = 32;
      format.mChannelsPerFrame = channels;
      format.mFramesPerPacket = 1;
      format.mBytesPerFrame = format.mBytesPerPacket = sizeof(float) * (planar ? 1 : channels);
      CMAudioFormatDescriptionRef description = nullptr;
      check(CMAudioFormatDescriptionCreate(nullptr, &format, 0, nullptr, 0, nullptr, nullptr,
                                           &description) == noErr,
            "PCM description");
      std::vector<float> left(frames, .25f), right(frames, -.5f), interleaved(frames * 2);
      for (int i = 0; i < frames; ++i) {
        interleaved[i * 2] = left[i];
        interleaved[i * 2 + 1] = right[i];
      }
      struct {
        UInt32 count;
        AudioBuffer buffers[2];
      } buffers{};
      buffers.count = planar ? 2 : 1;
      buffers.buffers[0] = {UInt32(planar ? 1 : 2),
                            UInt32(frames * sizeof(float) * (planar ? 1 : 2)),
                            planar ? left.data() : interleaved.data()};
      buffers.buffers[1] = {1, UInt32(frames * sizeof(float)), right.data()};
      size_t samples = 0;
      for (int packet = 0; packet < 100; ++packet) {
        CMSampleBufferRef sample = nullptr;
        check(CMAudioSampleBufferCreateWithPacketDescriptions(
                  nullptr, nullptr, false, nullptr, nullptr, description, frames,
                  CMTimeMake(packet * frames, rate), nullptr, &sample) == noErr,
              "PCM sample");
        check(CMSampleBufferSetDataBufferFromAudioBufferList(
                  sample, nullptr, nullptr, 0, reinterpret_cast<AudioBufferList *>(&buffers)) ==
                  noErr,
              "PCM buffers");
        auto output = converter.convert(sample);
        CFRelease(sample);
        samples += output.size() / 2;
        if (output.size() > 128)
          for (size_t i = 64; i < output.size(); i += 2) {
            check(std::abs(output[i] - .25f) < .01f && std::abs(output[i + 1] + .5f) < .01f,
                  "resampling channels / values");
          }
      }
      CFRelease(description);
      check(samples >= 47900 && samples <= 48000, "continuous 48 kHz resampling clock");
    }
}
int main(int argc, char **argv) {
  @autoreleasepool {
    try {
      if (argc != 2)
        throw std::runtime_error("fixture directory required");
      audioTests();
      auto device = MTLCreateSystemDefaultDevice();
      if (!device) {
        std::cout << "No Metal device; GPU test skipped\n";
        return 77;
      }
      studio_detail::Compositor metal(64, 48, false);
      auto image = std::make_shared<StudioImage>();
      image->width = 4;
      image->height = 4;
      image->rgba.resize(64);
      for (int i = 0; i < 16; ++i) {
        image->rgba[i * 4] = uint8_t(40 + i * 10);
        image->rgba[i * 4 + 1] = uint8_t(230 - i * 10);
        image->rgba[i * 4 + 2] = 60;
        image->rgba[i * 4 + 3] = uint8_t(40 + i * 12);
      }
      StudioSource source;
      source.id = "rgba";
      for (int scenario = 0; scenario < 6; ++scenario) {
        source.transform = {.1f, .2f, .7f, .65f};
        source.transform.rotation = scenario == 1 ? 23 : 0;
        if (scenario == 2) {
          source.transform.crop_left = .25f;
          source.transform.crop_bottom = .2f;
        }
        source.chroma.enabled = scenario == 3;
        source.chroma.color = {0, 1, 0};
        source.chroma.similarity = .2f;
        source.chroma.smoothness = .7f;
        source.visible = scenario != 4;
        studio_detail::Layers layers{{source, image}};
        if (scenario == 5) {
          auto overlay = source;
          overlay.transform.x = .3f;
          layers.emplace_back(overlay, image);
        }
        auto cpu = studio_composite_cpu(64, 48, layers), gpu = metal.render(layers);
        int largest = 0;
        for (size_t i = 0; i < cpu.rgba.size(); ++i)
          largest = std::max(largest, std::abs(int(cpu.rgba[i]) - int(gpu.rgba[i])));
        check(largest <= 2, "Metal and CPU composition differ");
      }
      auto text = studio_detail::text_image("你好，直播助手", {.2f, .4f, .8f, .5f});
      size_t visible = 0;
      for (size_t i = 0; i < text->rgba.size(); i += 4)
        if (text->rgba[i + 3]) {
          ++visible;
          check(text->rgba[i] == 51 && text->rgba[i + 1] == 102 && text->rgba[i + 2] == 204 &&
                    text->rgba[i + 3] <= 128,
                "text straight alpha");
        }
      check(visible > 100, "Chinese text did not render");
      // Generate a two-row PNG to catch vertical inversion, channel order and
      // accidental double premultiplication in the CoreGraphics image decoder.
      auto dir = std::filesystem::path(argv[1]);
      std::filesystem::create_directories(dir);
      auto path = dir / "image.png";
      NSBitmapImageRep *bitmap =
          [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                  pixelsWide:2
                                                  pixelsHigh:2
                                               bitsPerSample:8
                                             samplesPerPixel:4
                                                    hasAlpha:YES
                                                    isPlanar:NO
                                              colorSpaceName:NSDeviceRGBColorSpace
                                                bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
                                                 bytesPerRow:8
                                                bitsPerPixel:32];
      const uint8_t pixels[]{255, 0, 0, 128, 255, 0, 0, 128, 0, 0, 255, 255, 0, 0, 255, 255};
      memcpy(bitmap.bitmapData, pixels, 16);
      auto png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
      check([png writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES],
            "PNG fixture write");
      auto loaded = studio_detail::load_image(path.string());
      check(loaded->width == 2 && loaded->height == 2, "image dimensions");
      check(loaded->rgba[0] > 250 && loaded->rgba[2] < 3 &&
                std::abs(int(loaded->rgba[3]) - 128) < 2 && loaded->rgba[10] > 250,
            "PNG orientation / straight alpha");
      auto i420 = studio_detail::to_i420(*loaded);
      auto roundtrip = studio_detail::from_i420(i420);
      check(roundtrip->width == 2 && roundtrip->height == 2, "I420 conversion");
      std::cout << "Metal, audio conversion, text and image backends passed\n";
      return 0;
    } catch (const std::exception &e) {
      std::cerr << e.what() << '\n';
      return 1;
    }
  }
}
