#include "session.hpp"
#include <algorithm>
#include <deque>
#include <random>
#include <set>

namespace demo {
namespace {
using namespace rtc;
using namespace std::chrono_literals;

struct Events final : RoomObserver {
  std::mutex mutex;
  std::deque<RoomEvent> events;
  bool overflow = false;

  void on_room_event(const RoomEvent& event) override {
    std::lock_guard lock(mutex);
    if (events.size() >= 4096) {
      overflow = true;
      return;
    }
    events.push_back(event);
  }

  std::deque<RoomEvent> take() {
    std::lock_guard lock(mutex);
    return std::exchange(events, {});
  }
};

struct Track {
  bool publication = false;
  std::string id, remote_sid;
  std::shared_ptr<MediaTrack> media;

  std::string sid() const {
    return publication ? media->snapshot().transport.server_sid : remote_sid;
  }
};

Participant participant(const ParticipantSnapshot& value) {
  Participant result{value.sid, value.identity, value.name, value.state};
  result.speaking = value.speaking;
  result.audio_level = value.audio_level;
  for (const auto& [sid, track] : value.publications) {
    TrackInfo info;
    static_cast<PublicationInfo&>(info) = track;
    result.tracks.push_back(std::move(info));
  }
  return result;
}

sdk::Result<void> failed(std::string message) {
  return sdk::Result<void>::failure(sdk::make_failure(
      sdk::ErrorDomain::media, sdk::ErrorCode::processing_failed, std::move(message)));
}
}  // namespace

Session::Session() = default;

Session::~Session() {
  stop();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void Session::start(SessionOptions options) {
  {
    std::lock_guard lock(mutex_);
    if (!data_.finished) {
      return;
    }
    if (std::count_if(options.media.begin(), options.media.end(),
                      [](const auto& m) { return m.video; }) > max_test_videos) {
      data_.state = State::Error;
      data_.error = "Demo supports up to 8 test videos";
      return;
    }
  }
  if (worker_.joinable()) {
    worker_.join();
  }
  {
    std::lock_guard lock(mutex_);
    data_ = {};
    data_.state = State::Connecting;
    data_.finished = false;
    data_.room = options.room;
    video_command_.reset();
    chat_command_.reset();
    chat_in_flight_ = false;
    chat_sequence_ = 0;
    std::random_device random;
    chat_epoch_ = std::to_string(random()) + "-" + std::to_string(random());
  }
  worker_ = livekit::compat::jthread([this, options = std::move(options)](
                                         livekit::compat::stop_token stop) mutable {
    // MediaEngine is synchronous. The session worker keeps all blocking controls off Qt.
    auto redact = [&options](std::string message) {
      const auto& token = options.connection.token;
      if (!token.empty()) {
        for (size_t pos = 0; (pos = message.find(token, pos)) != std::string::npos; pos += 10) {
          message.replace(pos, token.size(), "[redacted]");
        }
      }
      return message;
    };
    Events events;
    MediaEngine engine;
    engine.set_observer(&events);
    livekit::compat::stop_callback cancel(stop, [&engine] { engine.close(); });
    std::map<TrackId, Track> tracks;
    int video_number = 0;
    auto publish = [&](const sdk::MediaSpec& spec, bool microphone,
                       PublicationSource role) -> sdk::Result<TrackId> {
      MediaPublishSpec config;
      config.name = spec.video   ? "测试视频 " + std::to_string(++video_number)
                    : microphone ? "麦克风"
                                 : "测试音调";
      config.options.source = role != PublicationSource::unknown ? role
                              : !spec.video                      ? PublicationSource::microphone
                              : spec.capture == sdk::CaptureKind::screen
                                  ? PublicationSource::screen
                                  : PublicationSource::camera;
      if (spec.capture == sdk::CaptureKind::camera) {
        config.name = "摄像头";
      }
      if (config.options.source == PublicationSource::screen) {
        config.name = "屏幕共享";
      }
      config.kind = spec.video ? MediaKind::video : MediaKind::audio;
      config.format = {
          spec.video ? Codec::vp8 : Codec::opus,  spec.video ? 90000u : 48000u,
          uint16_t(spec.video ? 0 : 1),           uint32_t(spec.video ? spec.width : 0),
          uint32_t(spec.video ? spec.height : 0), {}};
      config.options.follow_subscriber_demand = spec.video && spec.dynacast;
      auto& input = config.input;
      if (microphone) {
        input.microphone = sdk::MicrophoneOptions{};
      }
      input.generated = spec.demo;
      input.input = spec.input;
      input.ffmpeg = spec.ffmpeg;
      input.loop = spec.loop;
      input.fps = spec.fps;
      input.capture = spec.capture;
      input.device_index = spec.device_index;
      input.screen_id = spec.screen_id;
      input.video_source = spec.video_source;
      input.audio_source = spec.audio_source;
      input.encoded_source = spec.encoded_source;
      // The SDK chooses the local TrackId. Preview may start before publish returns.
      auto preview_key = std::make_shared<std::string>();
      auto preview_mutex = std::make_shared<std::mutex>();
      if (spec.local_preview && spec.video && !input.encoded_source) {
        config.preview.video = [this, preview_key, preview_mutex](const auto& frame) {
          std::lock_guard key_lock(*preview_mutex);
          std::lock_guard lock(mutex_);
          if (!preview_key->empty() && data_.state != State::Disconnecting) {
            data_.frames[*preview_key] = frame.pixels;
          }
          return sdk::Result<void>::success();
        };
      }
      auto result = engine.publish_media(std::move(config));
      if (!result) {
        return sdk::Result<TrackId>::failure(result.failure_record());
      }
      const auto id = result->publication.snapshot().track_id;
      {
        std::lock_guard lock(*preview_mutex);
        *preview_key = id;
      }
      tracks.emplace(id, Track{true, id, {}, result->track});
      return sdk::Result<TrackId>::success(id);
    };
    auto subscribe = [&](RemoteTrackPublication publication) -> sdk::Result<void> {
      const auto info = publication.snapshot();
      MediaSubscribeSpec config;
      config.publication = publication;
      config.output.recording = options.record;
      if (info.kind == MediaKind::video) {
        config.output.video = [this, sid = info.sid](const auto& frame) {
          std::lock_guard lock(mutex_);
          if (data_.state != State::Disconnecting) {
            data_.frames[sid] = frame.pixels;
            ++data_.received_frames;
          }
          return sdk::Result<void>::success();
        };
      } else {
        config.output.playout = options.audio_playout;
        config.output.audio = [this](const auto& frame) {
          std::lock_guard lock(mutex_);
          if (data_.state != State::Disconnecting && frame.pcm) {
            ++data_.received_audio_frames;
            if (std::any_of(frame.pcm->samples.begin(), frame.pcm->samples.end(),
                            [](int sample) { return sample > 100 || sample < -100; })) {
              ++data_.non_silent_audio_frames;
            }
          }
          return sdk::Result<void>::success();
        };
      }
      auto result = engine.subscribe_media(std::move(config));
      if (!result) {
        return sdk::Result<void>::failure(result.failure_record());
      }
      tracks.emplace(info.sid, Track{false, info.sid, info.sid, result->track});
      return sdk::Result<void>::success();
    };
    auto run = [&]() -> sdk::Result<void> {
      options.connection.auto_subscribe = false;
      if (!engine.connect(options.connection)) {
        return failed("Unable to connect to media room");
      }
      if (!options.room.empty() && engine.info().name != options.room) {
        return failed("Joined room does not match the requested room");
      }
      for (const auto& spec : options.media) {
        auto result = publish(spec, false, PublicationSource::unknown);
        if (!result) {
          return sdk::Result<void>::failure(result.failure_record());
        }
      }
      if (options.microphone) {
        sdk::MediaSpec spec;
        spec.video = false;
        auto result = publish(spec, true, PublicationSource::microphone);
        if (!result) {
          return sdk::Result<void>::failure(result.failure_record());
        }
      }
      while (!stop.stop_requested()) {
        for (const auto& event : events.take()) {
          if (event.kind == RoomEventKind::connection_changed &&
              event.connection == ConnectionState::reconnecting) {
            std::lock_guard lock(mutex_);
            data_.frames.clear();
            video_command_.reset();
            data_.changing_video = false;
            chat_command_.reset();
            data_.chat.interrupt();
            data_.sending_chat = false;
            if (data_.state != State::Disconnecting) {
              data_.state = State::Reconnecting;
            }
          }
          if (event.kind == RoomEventKind::data_received && event.data &&
              event.data->topic == chat_topic) {
            for (const auto& peer : engine.remote_participants()) {
              const auto p = peer.snapshot();
              if (p.identity != event.participant_identity) {
                continue;
              }
              const auto local = engine.local_participant().snapshot();
              std::lock_guard lock(mutex_);
              if (data_.state != State::Disconnecting) {
                data_.chat.receive(event.data->payload,
                                   {p.identity, event.data->topic, event.data->reliable}, p.name,
                                   local.identity);
              }
            }
          }
        }
        {
          std::lock_guard lock(events.mutex);
          if (events.overflow) {
            return failed("Conference event queue overflow");
          }
        }
        const auto connection = engine.connection_state();
        if (connection == ConnectionState::failed || connection == ConnectionState::closed) {
          return failed("Disconnected by server. Join again to reconnect.");
        }
        if (connection != ConnectionState::connected) {
          std::this_thread::sleep_for(5ms);
          continue;
        }
        std::set<TrackSid> remote_sids;
        for (const auto& peer : engine.remote_participants()) {
          for (const auto& publication : peer.track_publications()) {
            const auto sid = publication.snapshot().sid;
            remote_sids.insert(sid);
            if (!tracks.count(sid)) {
              auto result = subscribe(publication);
              if (!result) {
                // A concurrent removal/rejoin makes this discovery stale; retry the new roster.
                if (publication.snapshot().state == TrackState::closed ||
                    engine.connection_state() != ConnectionState::connected) {
                  continue;
                }
                return result;
              }
            }
          }
        }
        for (auto it = tracks.begin(); it != tracks.end();) {
          auto& track = it->second;
          const auto state = track.media->snapshot();
          if ((!track.publication && !remote_sids.count(track.remote_sid)) ||
              (track.publication && state.transport.state == TrackState::closed)) {
            if (track.publication) {
              engine.unpublish_track(it->first);
            } else {
              engine.unsubscribe_track(it->first);
            }
            {
              std::lock_guard lock(mutex_);
              data_.frames.erase(it->first);
              data_.frames.erase(track.sid());
            }
            it = tracks.erase(it);
          } else {
            if (state.failure) {
              return sdk::Result<void>::failure(state.failure);
            }
            ++it;
          }
        }
        std::optional<ChatMessage> chat;
        std::optional<VideoCommand> command;
        {
          std::lock_guard lock(mutex_);
          chat.swap(chat_command_);
          command.swap(video_command_);
          chat_in_flight_ = bool(chat);
        }
        if (chat) {
          const auto permissions = engine.local_participant().snapshot().permissions;
          bool accepted = !permissions || permissions->can_publish_data;
          if (accepted) {
            DataPacket packet;
            packet.payload = encode_chat(chat->id, chat->text);
            packet.topic = chat_topic;
            engine.publish_data(std::move(packet));
            accepted = engine.connection_state() == ConnectionState::connected;
          }
          std::lock_guard lock(mutex_);
          data_.chat.complete(chat->id, accepted);
          data_.sending_chat = chat_in_flight_ = false;
          if (!accepted) {
            data_.chat_error = "暂时无法发送，请检查连接与发言权限。";
          }
        }
        if (command) {
          std::string failure;
          if (command->remove_sid.empty()) {
            sdk::MediaSpec spec;
            spec.demo = !command->microphone;
            spec.video = !command->microphone;
            auto result = publish(command->source.value_or(spec), command->microphone,
                                  command->publication_source);
            if (!result) {
              failure = result.error().message;
            }
          } else {
            auto found = std::find_if(tracks.begin(), tracks.end(), [&](const auto& item) {
              return item.second.publication && item.second.sid() == command->remove_sid;
            });
            if (found == tracks.end()) {
              failure = "Publication no longer exists";
            } else if (command->muted) {
              engine.set_muted(found->first, *command->muted);
            } else {
              engine.unpublish_track(found->first);
              tracks.erase(found);
            }
          }
          std::lock_guard lock(mutex_);
          data_.changing_video = false;
          data_.video_error = redact(std::move(failure));
        }
        auto local = participant(engine.local_participant().snapshot());
        std::vector<Participant> remote;
        for (const auto& peer : engine.remote_participants()) {
          remote.push_back(participant(peer.snapshot()));
        }
        for (auto& info : local.tracks) {
          for (const auto& [id, track] : tracks) {
            if (track.publication && track.sid() == info.sid) {
              info.local_id = id;
            }
          }
        }
        for (auto& peer : remote) {
          for (auto& info : peer.tracks) {
            auto found = tracks.find(info.sid);
            info.subscribed = found != tracks.end() &&
                              found->second.media->snapshot().transport.state == TrackState::ready;
          }
        }
        {
          std::lock_guard lock(mutex_);
          data_.update(std::move(local), remote);
          data_.room = engine.info().name;
          if (data_.state != State::Disconnecting) {
            data_.state = State::Connected;
          }
        }
        std::this_thread::sleep_for(5ms);
      }
      return sdk::Result<void>::success();
    };
    auto result = run();
    engine.close();
    engine.set_observer(nullptr);
    std::string error = redact(result ? "" : result.error().message);
    std::lock_guard lock(mutex_);
    if (stop.stop_requested()) {
      error.clear();
    }
    data_ = {};
    data_.state = error.empty() ? State::Idle : State::Error;
    data_.error = std::move(error);
    video_command_.reset();
    chat_command_.reset();
    chat_in_flight_ = false;
  });
}

void Session::stop() {
  {
    std::lock_guard lock(mutex_);
    if (data_.finished) {
      return;
    }
    data_.state = State::Disconnecting;
    data_.chat = {};
    chat_command_.reset();
    data_.sending_chat = false;
  }
  worker_.request_stop();
}
}  // namespace demo
