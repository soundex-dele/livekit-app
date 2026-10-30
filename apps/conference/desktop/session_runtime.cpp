#include "rtc_result.hpp"
using livekit::sdk::app_support::require_result;
#include "rtc_event_mailbox.hpp"
#include "session.hpp"
#include <livekit/rtc/engine.hpp>
#include <algorithm>
#include <random>

namespace demo {
namespace {
using namespace rtc;
using namespace std::chrono_literals;

struct Track {
  TrackDescriptor descriptor;
  bool publication = false, ready = false;
  std::shared_ptr<engine::MediaTrack> media;

  std::string sid() const {
    return publication ? media->snapshot().transport.server_sid : descriptor.remote_sid;
  }

  void check() const {
    if (media)
      require_result(media->check());
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
}  // namespace

Session::Session() = default;

Session::~Session() {
  stop();
  if (worker_.joinable())
    worker_.join();
}

void Session::start(SessionOptions options) {
  if (std::count_if(options.media.begin(), options.media.end(),
                    [](const auto& m) { return m.video; }) > max_test_videos)
    throw std::invalid_argument("Demo supports up to 8 test videos");
  {
    std::lock_guard lock(mutex_);
    if (!data_.finished)
      throw std::logic_error("Session already active");
  }
  if (worker_.joinable())
    worker_.join();
  {
    std::lock_guard lock(mutex_);
    data_ = {};
    video_command_.reset();
    data_.state = State::Connecting;
    data_.finished = false;
    data_.room = options.room;
    chat_command_.reset();
    chat_in_flight_ = false;
    chat_sequence_ = 0;
    std::random_device random;
    chat_epoch_ = std::to_string(random()) + "-" + std::to_string(random());
  }
  try {
    worker_ = std::jthread([this, options = std::move(options)](std::stop_token stop) mutable {
      std::string error;
      const auto redact = [&](std::string message) {
        const auto& token = options.connection.token;
        if (!token.empty())
          for (size_t pos; (pos = message.find(token)) != std::string::npos;)
            message.replace(pos, token.size(), "[redacted]");
        return message;
      };
      try {
        std::map<TrackId, std::shared_ptr<Track>> tracks;
        auto engine =
            require_result(engine::create_livekit_engine(livekit::sdk::rtc::AudioOutputOptions{}));
        livekit::sdk::app_support::RtcEventMailbox engine_events(*engine);
        // Only the supervisor owns engine control. Codec callbacks only replace an owned frame.
        const auto await = [&](auto& result) { return require_result(std::move(result)); };
        options.connection.subscribe = false;
        options.connection.subscribe_tracks.clear();
        auto joining = engine->join(options.connection);
        await(joining);
        if (const auto joined = engine->room().snapshot().session;
            !joined || (!options.room.empty() && joined->room.name != options.room))
          throw std::runtime_error("Joined room does not match the requested room");
        int video_number = 0;
        uint64_t track_number = 0;
        const auto publish = [&](const sdk::MediaSpec& spec, bool microphone = false,
                                 PublicationSource role = PublicationSource::unknown) {
          auto track = std::make_shared<Track>();
          track->publication = true;
          auto& d = track->descriptor;
          d.id = "local-" + std::to_string(++track_number);
          d.name = spec.video ? "测试视频 " + std::to_string(++video_number)
                              : (microphone ? "麦克风" : "测试音调");
          d.source = role != PublicationSource::unknown         ? role
                     : !spec.video                              ? PublicationSource::microphone
                     : spec.capture == sdk::CaptureKind::screen ? PublicationSource::screen
                                                                : PublicationSource::camera;
          if (spec.capture == sdk::CaptureKind::camera)
            d.name = "摄像头";
          if (d.source == PublicationSource::screen)
            d.name = "屏幕共享";
          d.kind = spec.video ? MediaKind::video : MediaKind::audio;
          d.format.codec = spec.video ? Codec::vp8 : Codec::opus;
          d.format.clock_rate = spec.video ? 90000 : 48000;
          d.format.channels = spec.video ? 0 : 1;
          d.format.width = spec.video ? spec.width : 0;
          d.format.height = spec.video ? spec.height : 0;
          d.stream_group = d.id;
          d.follow_subscriber_demand = spec.video && spec.dynacast;
          engine::MediaInput input;
          if (microphone)
            input.microphone = sdk::MicrophoneOptions{};
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
          engine::MediaPublishSpec config;
          config.track = d;
          config.input = input;
          if (spec.local_preview && spec.video && !input.encoded_source)
            config.preview.video = [this, id = d.id](const auto& frame) {
              std::lock_guard lock(mutex_);
              if (data_.state != State::Disconnecting)
                data_.frames[id] = frame.pixels;
            };
          auto operation = require_result(engine->publish_media(std::move(config)));
          track->media = std::move(operation.track);
          track->ready = true;
          tracks.emplace(d.id, track);
          return track;
        };
        for (const auto& media : options.media) {
          auto track = publish(media);
          track->ready = true;
        }
        if (options.microphone) {
          sdk::MediaSpec spec;
          spec.video = false;
          auto track = publish(spec, true);
          track->ready = true;
        }
        const auto subscribe = [&](TrackDescriptor descriptor) {
          if (tracks.contains(descriptor.id))
            return;
          auto track = std::make_shared<Track>();
          track->descriptor = descriptor;
          engine::MediaSubscribeSpec config;
          config.track = descriptor;
          config.output.recording = options.record;
          if (descriptor.kind == MediaKind::video)
            config.output.video = [this, sid = descriptor.remote_sid](const auto& frame) {
              std::lock_guard lock(mutex_);
              if (data_.state != State::Disconnecting) {
                data_.frames[sid] = frame.pixels;
                ++data_.received_frames;
              }
            };
          else {
            config.output.playout = options.audio_playout;
            config.output.audio = [this](const auto& frame) {
              std::lock_guard lock(mutex_);
              if (data_.state == State::Disconnecting || !frame.pcm)
                return;
              ++data_.received_audio_frames;
              if (std::any_of(frame.pcm->samples.begin(), frame.pcm->samples.end(),
                              [](int sample) { return sample > 100 || sample < -100; }))
                ++data_.non_silent_audio_frames;
            };
          }
          auto operation = require_result(engine->subscribe_media(std::move(config)));
          track->media = std::move(operation.track);
          track->ready = true;
          tracks.emplace(descriptor.id, std::move(track));
        };
        auto services = require_result(engine->room().services());
        services->set_event_handler([this, room = engine->room()](const DataEvent& event) {
          const auto* message = std::get_if<ReceivedData>(&event.value);
          if (!message || message->topic != chat_topic) { return; }
          const auto state = room.snapshot().session;
          if (!state) { return; }
          const auto sender = std::find_if(state->remote.begin(), state->remote.end(),
              [&](const auto& row) { return row.second.identity == event.sender_identity; });
          if (sender == state->remote.end()) { return; }
          std::lock_guard lock(mutex_);
          if (data_.state != State::Disconnecting) {
            data_.chat.receive(message->bytes,
                {event.sender_identity, message->topic, event.reliable},
                sender->second.name, state->local.identity);
          }
        });
        std::future<void> chat_send;
        std::string sending_id;
        while (!stop.stop_requested()) {
          while (auto event = engine_events.take_event(0ms)) {
            if (event->kind == RoomEventKind::remote_track_added && event->descriptor)
              subscribe(*event->descriptor);
            else if (event->kind == RoomEventKind::track_removed) {
              if (auto found = tracks.find(event->track_id); found != tracks.end()) {
                auto removing = found->second->publication ? engine->unpublish(found->first)
                                                           : engine->unsubscribe(found->first);
                await(removing);
                {
                  std::lock_guard lock(mutex_);
                  data_.frames.erase(found->first);
                  data_.frames.erase(found->second->sid());
                }
                tracks.erase(found);
              }
            } else if (event->kind == RoomEventKind::remote_track_removed) {
              auto found = tracks.find(event->track_id);
              if (found != tracks.end() && !found->second->publication) {
                auto removing = engine->unsubscribe(found->first);
                await(removing);
                {
                  std::lock_guard lock(mutex_);
                  data_.frames.erase(found->second->sid());
                }
                tracks.erase(found);
              }
            } else if (event->kind == RoomEventKind::reconnecting) {
              std::lock_guard lock(mutex_);
              data_.frames.clear();
              video_command_.reset();
              data_.changing_video = false;
              chat_command_.reset();
              data_.chat.interrupt();
              data_.sending_chat = chat_in_flight_;
              if (data_.state != State::Disconnecting)
                data_.state = State::Reconnecting;
            } else if (event->kind == RoomEventKind::closed || event->kind == RoomEventKind::failed)
              throw std::runtime_error(event->message.empty()
                                           ? "Disconnected by server. Join again to reconnect."
                                           : event->message);
          }

          while (auto failure = engine_events.take_failure()) {
            auto it = tracks.find(failure->id);
            if (it != tracks.end())
              throw std::runtime_error("Track " + failure->id + ": " + failure->message);
          }

          if (chat_send.valid() && chat_send.wait_for(0ms) == std::future_status::ready) {
            bool confirmed = true;
            try {
              chat_send.get();
            } catch (...) {
              confirmed = false;
            }
            std::lock_guard lock(mutex_);
            data_.chat.complete(sending_id, confirmed);
            data_.sending_chat = false;
            chat_in_flight_ = false;
            if (!confirmed)
              data_.chat_error = "发送结果未确认，消息可能已送出，不会自动重发。";
          }
          std::optional<ChatMessage> chat;
          if (!chat_send.valid()) {
            std::lock_guard lock(mutex_);
            chat.swap(chat_command_);
            if (chat)
              chat_in_flight_ = true;
          }
          if (chat) {
            sending_id = chat->id;
            try {
              DataSendOptions send;
              send.topic = chat_topic;
              send.operation.timeout = 10s;
              send.operation.stop = stop;
              chat_send = services->send_data(encode_chat(chat->id, chat->text), send);
            } catch (...) {
              std::lock_guard lock(mutex_);
              data_.chat.complete(sending_id, false);
              data_.sending_chat = false;
              chat_in_flight_ = false;
              data_.chat_error = "暂时无法发送，请检查连接与发言权限。";
            }
          }
          std::optional<VideoCommand> command;
          {
            std::lock_guard lock(mutex_);
            command.swap(video_command_);
          }
          if (command) {
            std::string failure;
            TrackId adding;
            try {
              if (command->remove_sid.empty()) {
                sdk::MediaSpec spec;
                spec.demo = !command->microphone;
                spec.video = !command->microphone;
                auto track = publish(command->source.value_or(spec), command->microphone,
                                     command->publication_source);
                adding = track->descriptor.id;
                track->ready = true;
              } else {
                const auto found =
                    std::find_if(tracks.begin(), tracks.end(), [&](const auto& item) {
                      return item.second->publication && item.second->sid() == command->remove_sid;
                    });
                if (found == tracks.end())
                  throw std::runtime_error("Publication no longer exists");
                if (command->muted) {
                  auto muting = engine->set_muted(found->first, *command->muted);
                  await(muting);
                } else {
                  auto removing = engine->unpublish(found->first);
                  await(removing);
                  tracks.erase(found);
                }
              }
            } catch (const std::exception& e) {
              failure = redact(e.what());
              if (!adding.empty()) {
                try {
                  require_result(engine->unpublish(adding));
                } catch (...) {
                }
                tracks.erase(adding);
              }
            }
            std::lock_guard lock(mutex_);
            data_.changing_video = false;
            data_.video_error = std::move(failure);
          }
          const auto room = engine->room().snapshot();
          if (room.session) {
            std::vector<Participant> remote;
            for (const auto& [id, value] : room.session->remote)
              remote.push_back(participant(value));
            std::lock_guard lock(mutex_);
            auto local = participant(room.session->local);
            for (auto& info : local.tracks)
              for (const auto& [id, track] : tracks)
                if (track->publication && track->sid() == info.sid)
                  info.local_id = id;
            for (auto& remote_participant : remote)
              for (auto& info : remote_participant.tracks)
                for (const auto& [id, track] : tracks)
                  if (!track->publication && track->sid() == info.sid && track->ready &&
                      track->media->snapshot().transport.state == TrackState::ready)
                    info.subscribed = true;
            data_.update(std::move(local), remote);
            data_.room = room.session->room.name;
            if (data_.state != State::Disconnecting && room.state == ConnectionState::connected)
              data_.state = State::Connected;
          }
          std::this_thread::sleep_for(5ms);
        }
        require_result(engine->close());
        for (const auto& [id, track] : tracks)
          track->check();
      } catch (const std::exception& e) {
        error = redact(e.what());
      } catch (...) {
        error = "Unknown session failure";
      }
      std::lock_guard lock(mutex_);
      if (stop.stop_requested())
        error.clear();
      data_ = {};
      data_.state = error.empty() ? State::Idle : State::Error;
      data_.error = std::move(error);
      video_command_.reset();
      chat_command_.reset();
      chat_in_flight_ = false;
    });
  } catch (...) {
    std::lock_guard lock(mutex_);
    data_.finished = true;
    data_.state = State::Error;
    throw;
  }
}

void Session::stop() {
  {
    std::lock_guard lock(mutex_);
    if (data_.finished)
      return;
    data_.state = State::Disconnecting;
    data_.chat = {};
    chat_command_.reset();
    data_.sending_chat = false;
  }
  worker_.request_stop();
}
}  // namespace demo
