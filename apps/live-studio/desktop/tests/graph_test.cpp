#include "result_check.hpp"
#include "engines/studio/studio_graph.hpp"
#include "media_nodes/capture/video_source.hpp"
#include "media_nodes/capture/audio_source.hpp"
#include <graphflow/types.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <thread>
#include <tuple>
#include <mutex>

using namespace livekit::sdk;
using namespace livekit::sdk::studio_detail;
using Clock = std::chrono::steady_clock;
using graphflow::GraphState;

namespace {
void check(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}

void states(const StudioGraph& graph, GraphState expected) {
  auto actual = graph.state();
  check(actual && *actual == expected, "unexpected typed studio graph lifecycle");
}

std::shared_ptr<StudioImage> solid(uint8_t r, uint8_t g, uint8_t b) {
  auto image = std::make_shared<StudioImage>();
  image->width = image->height = 2;
  for (int i = 0; i < 4; ++i)
    image->rgba.insert(image->rgba.end(), {r, g, b, 255});
  return image;
}

void color(const StudioImage& image, int x, int y, int r, int g, int b) {
  auto p = image.rgba.data() + (size_t(y) * image.width + x) * 4;
  check(std::abs(int(p[0]) - r) < 5 && std::abs(int(p[1]) - g) < 5 && std::abs(int(p[2]) - b) < 5,
        "unexpected composited pixel");
}

void routing_and_lifecycle() {
  StudioOptions options;
  options.software_gpu = true;
  StudioGraphState state(options);
  StudioSource red;
  red.id = "local";
  red.color = {1, 0, 0, 1};
  state.scene.sources = {red};
  state.remoteVideo["guest"] = solid(0, 0, 255);
  state.remoteSeen["guest"] = Clock::now();
  state.remoteAudio["guest"].assign(19200, .25f);
  state.settings.remote_volume = .5f;
  detail::I420SourceReader local_video(state.localVideo), program_video(state.programVideo);
  detail::PcmSourceReader local_audio(state.localAudio), program_audio(state.programAudio);
  checked(local_video.start());
  checked(program_video.start());
  checked(local_audio.start());
  checked(program_audio.start());
  StudioGraph graph(state);
  states(graph, GraphState::Created);
  check(!graph.step(), "expected operation to fail");
  using Edge = std::tuple<std::string, std::string, std::string>;
  const std::multiset<Edge> expected = {
      {"studio.clock", "studio.recorder", "Tick"},
      {"studio.clock", "studio.sources", "Tick"},
      {"studio.audio-clock", "studio.audio-sources", "AudioTick"},
      {"studio.audio-sources", "studio.mixer", "AudioInput"},
      {"studio.sources", "studio.compositor", "VideoLayers"},
      {"studio.mixer", "studio.audio-output", "MixedAudio"},
      {"studio.mixer", "studio.monitor", "MixedAudio"},
      {"studio.mixer", "studio.audio-recorder", "RecordingAudio"},
      {"studio.compositor", "studio.video-output", "ComposedVideo"},
      {"studio.compositor", "studio.preview", "ComposedVideo"},
      {"studio.compositor", "studio.recorder", "RecordingVideo"},
  };
  std::multiset<Edge> actual;
  for (const auto& edge : graph.connections()) {
    actual.emplace(edge.from, edge.to, edge.type);
  }
  check(actual == expected, "missing, duplicated or mislabelled Studio edge");

  checked(graph.start());
  states(graph, GraphState::Running);
  check(
      state.programAudio->stats().queued_blocks > 0 && state.localAudio->stats().queued_blocks > 0,
      "audio branches receive graph blocks");
  auto local_pcm = local_audio.read(), program_pcm = program_audio.read();
  check(local_pcm && program_pcm && !local_pcm->silence && !program_pcm->silence,
        "publication must deliver mixed blocks, not reader-generated silence");
  check(local_pcm->captured == program_pcm->captured, "program and local share audio clock");
  check(std::all_of(local_pcm->samples.begin(), local_pcm->samples.end(),
                    [](int16_t v) { return v == 0; }),
        "remote audio leaked into interaction output");
  check(std::abs(int(program_pcm->samples[0]) - 4096) < 3, "program remote gain");
  auto next_pcm = program_audio.read();
  check(next_pcm && !next_pcm->silence &&
            next_pcm->captured - program_pcm->captured == std::chrono::milliseconds(10),
        "10 ms audio timeline");
  check(state.status.frames == 0, "audio publication must not require a video tick");
  checked(graph.step());
  check(state.status.frames == 1 && state.status.preview, "preview receives graph frame");
  check(
      state.programVideo->stats().queued_frames > 0 && state.localVideo->stats().queued_frames > 0,
      "video branches receive graph frame");
  auto local = local_video.read({}), program = program_video.read({});
  check(local && program, "video publication frames");
  color(*checked(from_i420(local->frame)), 960, 360, 255, 0, 0);
  auto program_image = checked(from_i420(program->frame));
  color(*program_image, 320, 360, 255, 0, 0);
  color(*program_image, 960, 360, 0, 0, 255);
  color(*state.status.preview, 960, 360, 0, 0, 255);
  const auto nodes = graph.nodes();
  check(nodes.size() == 12, "unexpected number of Studio nodes");
  for (const auto& node : nodes) {
    const bool ingress = node.name == "studio.clock" || node.name == "studio.audio-clock";
    check(ingress ? node.published > 0 : node.received > 0,
          "media/control did not traverse a node");
  }

  // Both clock owners can observe these inputs.
  {
    std::lock_guard lock(state.mutex);
    state.scene.sources[0].color = {0, 1, 0, 1};
    state.remoteVideo.clear();
    state.remoteAudio.clear();
    state.remoteSeen.clear();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(35));
  checked(graph.step());
  color(*state.status.preview, 960, 360, 0, 255, 0);
  auto frames = state.status.frames;
  graph.stop();
  states(graph, GraphState::Stopped);
  check(!graph.step(), "expected operation to fail");
  check(state.status.frames == frames, "stopped graph produced frames");
  checked(graph.start());
  checked(graph.step());
  check(state.status.frames == frames + 1, "graph restart did not produce a frame");

  // Exercise bounded publication queues and ensure backpressure cannot block
  // preview. Readers deliberately stop consuming while the graph keeps running.
  const auto rejected_before = state.programAudio->stats().rejected_writes;
  for (int i = 0; i < 5; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    checked(graph.step());
  }
  const auto audio_stats = state.programAudio->stats();
  const auto max_blocks =
      state.programAudio->options().max_queue_duration / std::chrono::milliseconds(10);
  check(state.programVideo->stats().queued_frames <= state.programVideo->options().max_frames &&
            audio_stats.queued_blocks <= static_cast<size_t>(max_blocks),
        "unbounded publisher queue");
  check(audio_stats.rejected_writes > rejected_before, "test did not exercise PCM backpressure");
  check(state.status.frames >= frames + 6, "backpressure blocked preview");
  graph.release();
  graph.release();
  states(graph, GraphState::Closed);
  check(state.programVideo->stats().closed && state.localVideo->stats().closed &&
            state.programAudio->stats().closed && state.localAudio->stats().closed,
        "release did not close all output branches");
  check(!program_video.read({}) && !program_audio.read(), "release did not signal EOS");
  check(!graph.start(), "expected operation to fail");
}

void recording_and_failure(const std::filesystem::path& directory) {
  StudioOptions options;
  options.software_gpu = true;
  StudioGraphState state(options);
  StudioSource source;
  source.id = "color";
  source.color = {0, 1, 0, 1};
  state.scene.sources = {source};
  state.remoteAudio["guest"].assign(19200, .25f);
  state.remoteSeen["guest"] = Clock::now();
  auto path =
      directory / ("graph-" + std::to_string(Clock::now().time_since_epoch().count()) + ".mp4");
  {
    StudioGraph graph(state);
    state.recordPath = path;
    state.recordWanted = true;
    checked(graph.start());
    checked(graph.step());
    check(state.status.recording, "recording control did not reach sink");
    for (int i = 0; i < 4; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(35));
      checked(graph.step());
    }
    graph.stop();
    check(!state.status.recording && state.status.error.empty(),
          "graph stop did not drain recorder");
  }
  check(std::filesystem::exists(path) && std::filesystem::file_size(path) > 1024,
        "graph recorder did not finalize MP4");
  auto mkv = path;
  mkv.replace_extension(".mkv");
  check(std::filesystem::exists(mkv), "recovery MKV missing");
  {
    // Feed the recorded program back through Player -> Sources -> Mixer/Composite.
    // This exercises both graph-owned pipelines without capture devices or a room.
    StudioGraphState playback(options);
    StudioSource file;
    file.id = "file";
    file.kind = StudioSourceKind::video;
    file.input = path.string();
    playback.scene.sources = {file};
    StudioGraph graph(playback);
    checked(graph.start());
    bool saw_video = false, saw_audio = false;
    auto deadline = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < deadline && !(saw_video && saw_audio)) {
      checked(graph.step());
      decltype(playback.status.preview) preview;
      {
        std::lock_guard lock(playback.mutex);
        saw_audio |= playback.status.output_peak > .05f;
        preview = playback.status.preview;
      }
      if (preview) {
        auto p = (size_t(360) * preview->width + 640) * 4;
        saw_video |= preview->rgba[p + 1] > 240 && preview->rgba[p] < 10;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    graph.stop();
    check(playback.status.source_errors.empty(), "recorded file failed to open as a source");
    check(saw_video && saw_audio,
          "file media did not reach composition and mixing through graph edges");
  }
  {
    StudioGraphState collision(options);
    collision.scene = state.scene;
    collision.recordPath = path;
    collision.recordWanted = true;
    StudioGraph graph(collision);
    checked(graph.start());
    checked(graph.step());
    check(!collision.recordWanted && !collision.status.recording && !collision.status.error.empty(),
          "recorder collision must be visible and nonfatal");
    check(collision.status.frames > 0, "recorder failure stopped preview");
  }
  {
    StudioGraphState broken(options);
    broken.scene = state.scene;
    // Bypass the facade validator to reject the first independent audio slot.
    broken.settings.microphone_volume = 5;
    StudioGraph graph(broken);
    const auto first = graph.start();
    check(!first && first.failure_record(), "startup did not propagate first audio failure");
    check(graph.start().failure_record() == first.failure_record(), "lost first audio failure");
    check(!graph.step() && !broken.status.preview && broken.status.frames == 0 &&
              broken.programVideo->stats().accepted == 0 &&
              broken.programAudio->stats().accepted_samples == 0,
          "failed graph published media");
    graph.release();
    states(graph, GraphState::Closed);
  }
  std::cout << "studio graph recording=" << path.string() << "\n";
}
}  // namespace

int main(int argc, char** argv) {
  HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  try {
    check(SUCCEEDED(com), "test COM initialization");
    routing_and_lifecycle();
    auto directory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("fixtures");
    std::filesystem::create_directories(directory);
    recording_and_failure(directory);
    CoUninitialize();
    std::cout << "studio graph passed\n";
    return 0;
  } catch (const std::exception& e) {
    if (SUCCEEDED(com))
      CoUninitialize();
    std::cerr << e.what() << "\n";
    return 1;
  }
}
