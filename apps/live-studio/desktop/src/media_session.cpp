#include "media_session.hpp"
#include <deque>

namespace studio_app {
namespace sdk = livekit::sdk;
namespace rtc = sdk::rtc;
using namespace std::chrono_literals;
namespace {
struct Mailbox {
  std::mutex mutex;
  std::shared_ptr<const sdk::VideoFrame> video;
  std::deque<rtc::engine::AudioSample> audio;
};
}
MediaSession::MediaSession(rtc::RoomOptions options, std::shared_ptr<sdk::Studio> studio,
                           bool interaction, const std::atomic<bool>& closing, Statistics stats) {
  std::promise<void> started; auto ready = started.get_future();
  worker_ = std::jthread([this, options = std::move(options), studio = std::move(studio),
                          interaction, &closing, stats = std::move(stats), started = std::move(started)]() mutable {
    run(std::move(options), std::move(studio), interaction, closing, std::move(stats), std::move(started));
  });
  ready.get();
}
MediaSession::~MediaSession() { stop_ = true; if (worker_.joinable()) worker_.join(); }
std::string MediaSession::error() const { std::lock_guard lock(mutex_); return error_; }
void MediaSession::run(rtc::RoomOptions options, std::shared_ptr<sdk::Studio> studio,
                       bool interaction, const std::atomic<bool>& closing, Statistics stats,
                       std::promise<void> started) {
  bool announced = false;
  std::unique_ptr<rtc::engine::RtcMediaEngine> engine;
  std::map<rtc::TrackId, std::shared_ptr<Mailbox>> mailboxes;
  std::map<rtc::TrackId, std::future<rtc::TrackId>> pending;
  auto cancelled = [&] { return stop_.load() || closing.load(); };
  auto await = [&](auto& future) {
    while (future.wait_for(0ms) != std::future_status::ready) {
      if (cancelled()) throw std::runtime_error("Studio media connection cancelled");
      engine->poll(); std::this_thread::sleep_for(5ms);
    }
    if (cancelled()) throw std::runtime_error("Studio media connection cancelled");
    return future.get();
  };
  try {
    engine = rtc::engine::create_livekit_media_engine();
    auto joining = engine->join(options); await(joining);
    const auto canvas = studio->options();
    rtc::TrackDescriptor video; video.id = "video"; video.name = "Studio video";
    video.stream_group = "studio"; video.format.codec = rtc::Codec::vp8;
    video.format.width = canvas.width; video.format.height = canvas.height;
    rtc::engine::MediaInput vi; vi.fps = canvas.fps;
    vi.video_source = interaction ? studio->local_video_output() : studio->video_output();
    rtc::engine::MediaPublishSpec video_config; video_config.track=video; video_config.input=vi;
    auto video_operation=engine->publish_video(std::move(video_config)); auto publishing_video=std::move(video_operation.ready);
    await(publishing_video);
    rtc::TrackDescriptor audio; audio.id = "audio"; audio.name = "Studio audio";
    audio.stream_group = "studio"; audio.kind = rtc::MediaKind::audio;
    audio.format.codec = rtc::Codec::opus; audio.format.clock_rate = 48000; audio.format.channels = 2;
    rtc::engine::MediaInput ai;
    ai.audio_source = interaction ? studio->local_audio_output() : studio->audio_output();
    rtc::engine::MediaPublishSpec audio_config; audio_config.track=audio; audio_config.input=ai;
    auto audio_operation=engine->publish_audio(std::move(audio_config)); auto publishing_audio=std::move(audio_operation.ready);
    await(publishing_audio);
    connected_ = true; started.set_value(); announced = true;
    bool ended = false;
    while (!cancelled() && !ended) {
      engine->poll();
      if (auto failure = engine->next_failure()) throw std::runtime_error(failure->message);
      while (auto event = engine->room().next_event(0ms)) {
        if (event->kind == rtc::RoomEventKind::closed) { ended = true; break; }
        if (event->kind == rtc::RoomEventKind::failed ||
            event->kind == rtc::RoomEventKind::reconnecting)
          throw std::runtime_error("Studio media connection ended");
        if (event->kind == rtc::RoomEventKind::statistics && event->statistics && stats) stats(*event->statistics);
        if (event->kind == rtc::RoomEventKind::remote_track_added && interaction && event->descriptor) {
          const auto d = *event->descriptor;
          if (mailboxes.contains(d.id)) continue;
          auto box = std::make_shared<Mailbox>(); mailboxes.emplace(d.id, box);
          rtc::engine::MediaSubscribeSpec config; config.track=d;
          if(d.kind==rtc::MediaKind::video) config.output.video=[box](const auto& frame) {
            std::lock_guard lock(box->mutex); box->video=frame.pixels;
          };
          else config.output.audio=[box](const auto& frame) {
            std::lock_guard lock(box->mutex); if(box->audio.size()==10)box->audio.pop_front(); box->audio.push_back(frame);
          };
          auto operation=engine->subscribe_media(std::move(config)); pending.emplace(d.id,std::move(operation.ready));
        }
        if (event->kind == rtc::RoomEventKind::remote_track_removed || event->kind == rtc::RoomEventKind::track_removed) {
          if (event->track_id == "video" || event->track_id == "audio") { ended = true; break; }
          if (mailboxes.contains(event->track_id)) {
            engine->unsubscribe(event->track_id).get(); pending.erase(event->track_id);
            mailboxes.erase(event->track_id); studio->remove_remote(event->track_id);
          }
        }
      }
      if (ended) break;
      for (auto& [id, operation] : pending)
        if (operation.valid() && operation.wait_for(0ms) == std::future_status::ready) operation.get();
      subscriptions_ = mailboxes.size();
      for (const auto& [id, box] : mailboxes) {
        std::shared_ptr<const sdk::VideoFrame> frame; std::deque<rtc::engine::AudioSample> blocks;
        { std::lock_guard lock(box->mutex); frame.swap(box->video); blocks.swap(box->audio); }
        if (frame) { studio->submit_remote_video(id, *frame); ++received_video_; }
        for (const auto& block : blocks) {
          if (!block.pcm || rtc::SteadyClock::now() - block.captured > 250ms) continue;
          sdk::AudioFrame pcm; pcm.sample_rate = block.pcm->sample_rate; pcm.channels = block.pcm->channels;
          pcm.samples_per_channel = block.pcm->samples_per_channel(); pcm.samples = block.pcm->samples;
          pcm.float_samples = block.pcm->float_samples;
          pcm.format = block.pcm->format == rtc::PcmFormat::s16 ? sdk::AudioSampleFormat::pcm16 : sdk::AudioSampleFormat::float32;
          studio->submit_remote_audio(id, pcm); ++received_audio_;
        }
      }
      std::this_thread::sleep_for(5ms);
    }
  } catch (const std::exception& error) {
    std::string message = error.what();
    if (!options.token.empty()) for (size_t pos; (pos = message.find(options.token)) != std::string::npos;)
      message.replace(pos, options.token.size(), "[redacted]");
    if (!cancelled()) { std::lock_guard lock(mutex_); error_ = message; }
    if (!announced) started.set_exception(std::make_exception_ptr(std::runtime_error(message)));
  }
  if (engine) try { engine->close().get(); }
  catch (const std::exception& error) {
    std::lock_guard lock(mutex_); if (error_.empty()) error_ = error.what();
  }
  for (const auto& [id, box] : mailboxes) studio->remove_remote(id);
  connected_ = false; finished_ = true;
}
}
