#include <livekit/studio.hpp>
#include "internal.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <cmath>
using namespace livekit::sdk;

void check(bool v, const char* m) {
  if (!v)
    throw std::runtime_error(m);
}

int main(int argc, char** argv) {
  try {
    StudioSource red;
    red.id = "red";
    red.color = {1, 0, 0, 1};
    auto pixels = std::make_shared<StudioImage>();
    pixels->width = pixels->height = 2;
    pixels->rgba = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
    auto cpu = studio_composite_cpu(8, 8, {{red, pixels}});
    check(cpu.rgba[0] == 255 && cpu.rgba[1] == 0, "red composition");
    auto translucent = std::make_shared<StudioImage>(*pixels);
    for (size_t i = 3; i < translucent->rgba.size(); i += 4)
      translucent->rgba[i] = 128;
    auto alpha = studio_composite_cpu(8, 8, {{red, translucent}});
    check(alpha.rgba[0] >= 127 && alpha.rgba[0] <= 129, "alpha blend");
    StudioSource hidden = red;
    hidden.visible = false;
    check(studio_composite_cpu(8, 8, {{hidden, pixels}}).rgba[0] == 0, "hidden layer");
    StudioSource keyed = red;
    keyed.chroma.enabled = true;
    keyed.chroma.color = {1, 0, 0};
    auto black = studio_composite_cpu(8, 8, {{keyed, pixels}});
    check(black.rgba[0] == 0, "chroma key");
    studio_detail::Compositor gpu(8, 8, true);
    auto gpuAlpha = gpu.render({{red, translucent}});
    check(gpuAlpha.rgba[3] == 255 && std::abs(int(gpuAlpha.rgba[0]) - 128) < 2,
          "opaque program canvas preserves alpha when reused for PK");
    auto result = gpu.render({{red, pixels}});
    check(result.rgba[0] > 250 && result.rgba[1] < 3, "software compositor");
    result = gpu.render({{keyed, pixels}});
    check(result.rgba[0] < 3, "software chroma key");
    auto edge = std::make_shared<StudioImage>(*pixels);
    for (size_t i = 0; i < edge->rgba.size(); i += 4)
      edge->rgba[i] = 179;
    keyed.chroma.spill = 0;
    keyed.chroma.similarity = .2f;
    keyed.chroma.smoothness = .2f;
    auto soft = studio_composite_cpu(8, 8, {{keyed, edge}});
    check(soft.rgba[0] > 70 && soft.rgba[0] < 110, "key edge feather");
    auto softGpu = gpu.render({{keyed, edge}});
    check(std::abs(int(softGpu.rgba[0]) - int(soft.rgba[0])) < 3, "GPU and CPU key edge agree");
    red.transform = {.25f, .25f, .5f, .5f};
    auto crop = studio_composite_cpu(8, 8, {{red, pixels}});
    red.transform.rotation = 90;
    auto rotated = studio_composite_cpu(8, 8, {{red, pixels}});
    check(rotated.rgba[(3 * 8 + 3) * 4] == 255, "rotation center");
    check(crop.rgba[0] == 0 && crop.rgba[(3 * 8 + 3) * 4] == 255, "transform bounds");
    std::vector<float> a{.2f, -.2f}, b{.8f, -.8f};
    studio_mix(a, b, .5f);
    check(std::abs(a[0] - .6f) < .001f, "mix gain");
    std::filesystem::path dir = argc > 1 ? argv[1] : "fixtures";
    std::filesystem::create_directories(dir);
    StudioScene scene;
    scene.sources = {red};
    StudioAudioSettings audio;
    StudioOptions options;
    options.software_gpu = true;
    auto project = dir / "project.json";
    Studio::save_project(project, {scene}, 0, options, audio);
    size_t selected = 9;
    auto loaded = Studio::load_project(project, selected, options, audio);
    check(selected == 0 && loaded[0].sources[0].id == "red", "project roundtrip");
    scene.name = "updated";
    Studio::save_project(project, {scene}, 0, options, audio);
    loaded = Studio::load_project(project, selected, options, audio);
    check(loaded[0].name == "updated", "atomic replacement of an existing project");
    auto filename =
        dir /
        ("record-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".mp4");
    {
      Studio studio(options);
      studio.set_scene(scene);
      studio.start_recording(filename);
      std::this_thread::sleep_for(std::chrono::seconds(3));
      auto status = studio.snapshot();
      check(status.frames > 10, "frames produced");
      check(status.error.empty(), status.error.c_str());
      studio.stop_recording();
      for (int i = 0; i < 100 && studio.snapshot().recording; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      check(studio.snapshot().error.empty(), studio.snapshot().error.c_str());
    }
    {
      Studio recovery(options);
      StudioSource missing;
      missing.id = "missing";
      missing.kind = StudioSourceKind::image;
      missing.input = (dir / "does-not-exist.png").string();
      StudioScene broken;
      broken.sources = {missing};
      recovery.set_scene(broken);
      for (int i = 0; i < 100 && recovery.snapshot().source_errors.empty(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      check(!recovery.snapshot().source_errors.empty(), "missing source is reported");
      recovery.set_scene(scene);
      for (int i = 0; i < 100 && !recovery.snapshot().source_errors.empty(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      check(recovery.snapshot().source_errors.empty(), "source replacement recovers");
      recovery.start_recording(filename);
      for (int i = 0; i < 100 && recovery.snapshot().error.empty(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      check(!recovery.snapshot().error.empty() && !recovery.snapshot().recording,
            "existing recording is protected");
      check(recovery.snapshot().frames > 0, "recording failure preserves preview");
    }
    check(std::filesystem::exists(filename) && std::filesystem::file_size(filename) > 1024,
          "MP4 recording");
    std::cout << "studio engine passed; recording=" << filename.string() << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
