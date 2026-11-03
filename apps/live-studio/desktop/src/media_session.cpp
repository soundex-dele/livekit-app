#include "livekit/compat/thread.hpp"
#include "media_session.hpp"
#include <deque>

namespace studio_app {
namespace sdk = livekit::sdk;
namespace rtc = sdk::client;
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
  std::deque<rtc::AudioSample> audio;
};
}  // namespace

MediaSession::MediaSession(rtc::ConnectionOptions options, std::shared_ptr<sdk::Studio> studio,
                           bool interaction, const std::atomic<bool>& closing, Statistics stats) {
  std::promise<sdk::Result<void>> started;
  auto ready = started.get_future();
  worker_ = livekit::compat::jthread(
      [this, options = std::move(options), studio = std::move(studio), interaction, &closing,
       stats = std::move(stats), started = std::move(started)]() mutable {
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

void MediaSession::run(rtc::ConnectionOptions options, std::shared_ptr<sdk::Studio> studio,
                       bool interaction, const std::atomic<bool>& closing, Statistics stats,
                       std::promise<sdk::Result<void>> started) {
  bool announced = false;
  auto engine = std::make_unique<rtc::MediaEngine>();
  std::map<rtc::TrackSid, std::shared_ptr<rtc::MediaTrack>> subscriptions;
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
    options.auto_subscribe = false;
    if (!engine->connect(options)) {
      return failure("Studio media connection failed");
    }
    const auto canvas = studio->options();
    rtc::MediaPublishSpec video;
    video.name = "Studio video";
    video.options.stream_group = "studio";
    video.format.codec = rtc::Codec::vp8;
    video.format.width = canvas.width;
    video.format.height = canvas.height;
    video.input.fps = canvas.fps;
    video.input.video_source = interaction ? studio->local_video_output() : studio->video_output();
    STUDIO_MEDIA_ASSIGN(video_operation, engine->publish_media(std::move(video)));
    rtc::MediaPublishSpec audio;
    audio.name = "Studio audio";
    audio.options.stream_group = "studio";
    audio.kind = rtc::MediaKind::audio;
    audio.format.codec = rtc::Codec::opus;
    audio.format.clock_rate = 48000;
    audio.format.channels = 2;
    audio.input.audio_source = interaction ? studio->local_audio_output() : studio->audio_output();
    STUDIO_MEDIA_ASSIGN(audio_operation, engine->publish_media(std::move(audio)));
    connected_ = true;
    started.set_value(sdk::Result<void>::success());
    announced = true;
    auto next_stats = rtc::SteadyClock::now();
    while (!cancelled()) {
      if (engine->connection_state() != rtc::ConnectionState::connected) {
        return failure("Studio media connection ended");
      }
      for (const auto& track : {video_operation.track, audio_operation.track}) {
        const auto state = track->snapshot();
        if (state.failure) {
          return sdk::Result<void>::failure(state.failure);
        }
        if (state.transport.state == rtc::TrackState::closed) {
          return failure("Studio publication ended");
        }
      }
      if (stats && rtc::SteadyClock::now() >= next_stats) {
        STUDIO_MEDIA_ASSIGN(reports, engine->statistics());
        for (const auto& report : reports) {
          stats(report);
        }
        next_stats = rtc::SteadyClock::now() + 1s;
      }
      std::map<rtc::TrackSid, rtc::RemoteTrackPublication> available;
      if (interaction) {
        for (const auto& participant : engine->remote_participants()) {
          for (const auto& pub : participant.track_publications()) {
            available.emplace(pub.snapshot().sid, pub);
          }
        }
      }
      for (auto it = mailboxes.begin(); it != mailboxes.end();) {
        if (!available.count(it->first)) {
          engine->unsubscribe_track(it->first);
          studio->remove_remote(it->first);
          subscriptions.erase(it->first);
          it = mailboxes.erase(it);
        } else {
          ++it;
        }
      }
      for (const auto& [id, pub] : available) {
        if (mailboxes.count(id)) {
          continue;
        }
        auto box = std::make_shared<Mailbox>();
        rtc::MediaSubscribeSpec config;
        config.publication = pub;
        if (pub.snapshot().kind == rtc::MediaKind::video) {
          config.output.video = [box](const auto& frame) {
            std::lock_guard lock(box->mutex);
            box->video = frame.pixels;
            return sdk::Result<void>::success();
          };
        } else {
          config.output.audio = [box](const auto& frame) {
            std::lock_guard lock(box->mutex);
            if (box->audio.size() == 10) {
              box->audio.pop_front();
            }
            box->audio.push_back(frame);
            return sdk::Result<void>::success();
          };
        }
        STUDIO_MEDIA_ASSIGN(operation, engine->subscribe_media(std::move(config)));
        subscriptions.emplace(id, operation.track);
        mailboxes.emplace(id, std::move(box));
      }
      for (const auto& [id, track] : subscriptions) {
        if (auto state = track->snapshot(); state.failure) {
          return sdk::Result<void>::failure(state.failure);
        }
      }
      subscriptions_ = mailboxes.size();
      for (const auto& [id, box] : mailboxes) {
        std::shared_ptr<const sdk::VideoFrame> frame;
        std::deque<rtc::AudioSample> blocks;
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
          pcm.format = block.pcm->format == sdk::rtc::PcmFormat::s16
                           ? sdk::AudioSampleFormat::pcm16
                           : sdk::AudioSampleFormat::float32;
          STUDIO_MEDIA_TRY(studio->submit_remote_audio(id, pcm));
          ++received_audio_;
        }
      }
      std::this_thread::sleep_for(5ms);
    }
    return sdk::Result<void>::success();
  };
  auto result = run_session();
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
  engine->close();
#undef STUDIO_MEDIA_TRY
#undef STUDIO_MEDIA_ASSIGN
  for (const auto& [id, box] : mailboxes)
    studio->remove_remote(id);
  connected_ = false;
  finished_ = true;
}
}  // namespace studio_app
