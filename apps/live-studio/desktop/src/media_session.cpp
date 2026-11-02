#include "rtc_blocking_bridge.hpp"
#include "rtc_event_mailbox.hpp"
#include "livekit/rtc/livekit.hpp"
#include "media_session.hpp"
#include <deque>

namespace studio_app {
namespace sdk = livekit::sdk;
namespace rtc = sdk::rtc;
using namespace std::chrono_literals;

namespace {
sdk::Result<void> operation_result(sdk::ErrorCode code) {
  if (code == sdk::ErrorCode::ok)
    return sdk::Result<void>::success();
  return sdk::Result<void>::failure(
      sdk::make_failure(sdk::ErrorDomain::media, code, "Studio media operation failed"));
}

sdk::Result<void> operation_result(sdk::Result<void> result) {
  return result;
}

struct Mailbox {
  std::mutex mutex;
  std::shared_ptr<const sdk::VideoFrame> video;
  std::deque<rtc::engine::AudioSample> audio;
};
}  // namespace

MediaSession::MediaSession(rtc::LiveKitRoomOptions options, std::shared_ptr<sdk::Studio> studio,
                           bool interaction, const std::atomic<bool>& closing, Statistics stats) {
  std::promise<sdk::Result<void>> started;
  auto ready = started.get_future();
  worker_ =
      std::jthread([this, options = std::move(options), studio = std::move(studio), interaction,
                    &closing, stats = std::move(stats), started = std::move(started)]() mutable {
        run(std::move(options), std::move(studio), interaction, closing, std::move(stats),
            std::move(started));
      });
  (void)ready.get();
}

MediaSession::~MediaSession() {
  stop_ = true;
  if (worker_.joinable())
    worker_.join();
}

std::string MediaSession::error() const {
  std::lock_guard lock(mutex_);
  return error_;
}

void MediaSession::run(rtc::LiveKitRoomOptions options, std::shared_ptr<sdk::Studio> studio,
                       bool interaction, const std::atomic<bool>& closing, Statistics stats,
                       std::promise<sdk::Result<void>> started) {
  bool announced = false;
  std::unique_ptr<livekit::sdk::app_support::BlockingRtc> engine;
  std::map<rtc::TrackId, std::shared_ptr<Mailbox>> mailboxes;
  auto cancelled = [&] { return stop_.load() || closing.load(); };
  auto failure = [](std::string message) {
    return sdk::Result<void>::failure(sdk::make_failure(
        sdk::ErrorDomain::media, sdk::ErrorCode::processing_failed, std::move(message)));
  };
#define STUDIO_MEDIA_TRY(expr)                                    \
  do {                                                            \
    auto result = operation_result(expr);                         \
    if (!result)                                                  \
      return sdk::Result<void>::failure(result.failure_record()); \
  } while (false)
#define STUDIO_MEDIA_ASSIGN(name, expr)                                \
  auto name##_result = (expr);                                         \
  if (!name##_result)                                                  \
    return sdk::Result<void>::failure(name##_result.failure_record()); \
  auto name = std::move(*name##_result)
  auto run_session = [&]() -> sdk::Result<void> {
    STUDIO_MEDIA_ASSIGN(created, livekit::sdk::app_support::create_blocking_engine(
                                     livekit::sdk::rtc::AudioOutputOptions{}));
    engine = std::move(created);
    livekit::sdk::app_support::RtcEventMailbox engine_events(*engine);
    auto joining = engine->join(options);
    STUDIO_MEDIA_TRY(joining);
    const auto canvas = studio->options();
    rtc::TrackDescriptor video;
    video.id = "video";
    video.name = "Studio video";
    video.stream_group = "studio";
    video.format.codec = rtc::Codec::vp8;
    video.format.width = canvas.width;
    video.format.height = canvas.height;
    rtc::engine::MediaInput vi;
    vi.fps = canvas.fps;
    vi.video_source = interaction ? studio->local_video_output() : studio->video_output();
    rtc::engine::MediaPublishSpec video_config;
    video_config.track = video;
    video_config.input = vi;
    STUDIO_MEDIA_ASSIGN(video_operation, engine->publish_video(std::move(video_config)));
    rtc::TrackDescriptor audio;
    audio.id = "audio";
    audio.name = "Studio audio";
    audio.stream_group = "studio";
    audio.kind = rtc::MediaKind::audio;
    audio.format.codec = rtc::Codec::opus;
    audio.format.clock_rate = 48000;
    audio.format.channels = 2;
    rtc::engine::MediaInput ai;
    ai.audio_source = interaction ? studio->local_audio_output() : studio->audio_output();
    rtc::engine::MediaPublishSpec audio_config;
    audio_config.track = audio;
    audio_config.input = ai;
    STUDIO_MEDIA_ASSIGN(audio_operation, engine->publish_audio(std::move(audio_config)));
    connected_ = true;
    started.set_value(sdk::Result<void>::success());
    announced = true;
    bool ended = false;
    while (!cancelled() && !ended) {
      STUDIO_MEDIA_ASSIGN(error, engine_events.take_failure());
      if (error)
        return error->failure ? sdk::Result<void>::failure(error->failure)
                              : failure(error->message);
      for (;;) {
        STUDIO_MEDIA_ASSIGN(event, engine_events.take_event(0ms));
        if (!event)
          break;
        if (event->kind == rtc::RoomEventKind::closed) {
          ended = true;
          break;
        }
        if (event->kind == rtc::RoomEventKind::failed ||
            event->kind == rtc::RoomEventKind::reconnecting)
          return failure("Studio media connection ended");
        if (event->kind == rtc::RoomEventKind::statistics && event->statistics && stats)
          stats(*event->statistics);
        if (event->kind == rtc::RoomEventKind::remote_track_added && interaction &&
            event->descriptor) {
          const auto d = *event->descriptor;
          if (mailboxes.contains(d.id))
            continue;
          auto box = std::make_shared<Mailbox>();
          mailboxes.emplace(d.id, box);
          rtc::engine::MediaSubscribeSpec config;
          config.track = d;
          if (d.kind == rtc::MediaKind::video)
            config.output.video = [box](const auto& frame) {
              std::lock_guard lock(box->mutex);
              box->video = frame.pixels;
              return sdk::Result<void>::success();
            };
          else
            config.output.audio = [box](const auto& frame) {
              std::lock_guard lock(box->mutex);
              if (box->audio.size() == 10)
                box->audio.pop_front();
              box->audio.push_back(frame);
              return sdk::Result<void>::success();
            };
          STUDIO_MEDIA_ASSIGN(operation, engine->subscribe_media(std::move(config)));
        }
        if (event->kind == rtc::RoomEventKind::remote_track_removed ||
            event->kind == rtc::RoomEventKind::track_removed) {
          if (event->track_id == "video" || event->track_id == "audio") {
            ended = true;
            break;
          }
          if (mailboxes.contains(event->track_id)) {
            STUDIO_MEDIA_TRY(engine->unsubscribe(event->track_id));
            mailboxes.erase(event->track_id);
            studio->remove_remote(event->track_id);
          }
        }
      }
      if (ended)
        break;
      subscriptions_ = mailboxes.size();
      for (const auto& [id, box] : mailboxes) {
        std::shared_ptr<const sdk::VideoFrame> frame;
        std::deque<rtc::engine::AudioSample> blocks;
        {
          std::lock_guard lock(box->mutex);
          frame.swap(box->video);
          blocks.swap(box->audio);
        }
        if (frame) {
          STUDIO_MEDIA_TRY(studio->submit_remote_video(id, *frame));
          ++received_video_;
        }
        for (const auto& block : blocks) {
          if (!block.pcm || rtc::SteadyClock::now() - block.captured > 250ms)
            continue;
          sdk::AudioFrame pcm;
          pcm.sample_rate = block.pcm->sample_rate;
          pcm.channels = block.pcm->channels;
          pcm.samples_per_channel = block.pcm->samples_per_channel();
          pcm.samples = block.pcm->samples;
          pcm.float_samples = block.pcm->float_samples;
          pcm.format = block.pcm->format == rtc::PcmFormat::s16 ? sdk::AudioSampleFormat::pcm16
                                                                : sdk::AudioSampleFormat::float32;
          STUDIO_MEDIA_TRY(studio->submit_remote_audio(id, pcm));
          ++received_audio_;
        }
      }
      std::this_thread::sleep_for(5ms);
    }
    return sdk::Result<void>::success();
  };
  sdk::Result<void> result = sdk::Result<void>::success();
  try {
    result = run_session();
  } catch (const std::exception& error) {
    result = failure(error.what());
  }
  if (!result) {
    std::string message = result.error().message;
    if (!options.token.empty())
      for (size_t pos; (pos = message.find(options.token)) != std::string::npos;)
        message.replace(pos, options.token.size(), "[redacted]");
    if (!cancelled()) {
      std::lock_guard lock(mutex_);
      error_ = message;
    }
    if (!announced)
      started.set_value(std::move(result));
  }
  if (engine) {
    auto closed = operation_result(engine->close());
    if (!closed) {
      std::lock_guard lock(mutex_);
      if (error_.empty())
        error_ = closed.error().message;
    }
  }
#undef STUDIO_MEDIA_TRY
#undef STUDIO_MEDIA_ASSIGN
  for (const auto& [id, box] : mailboxes)
    studio->remove_remote(id);
  connected_ = false;
  finished_ = true;
}
}  // namespace studio_app
