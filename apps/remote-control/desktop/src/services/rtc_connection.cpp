#include "services/rtc_connection.hpp"
#include <set>

namespace remote_app {
namespace {
sdk::Failure disconnected(std::string message) {
  return sdk::make_failure(sdk::ErrorDomain::transport, sdk::ErrorCode::connection_lost,
                           std::move(message));
}
}  // namespace

RtcConnection::RtcConnection(std::string url, std::string token, std::string peer,
                             bool receive_video, const std::atomic<bool>& stop)
    : peer_(std::move(peer)), stop_(stop), receive_video_(receive_video) {
  engine_ = std::make_unique<rtc::MediaEngine>();
  engine_->set_observer(this);
  rtc::ConnectionOptions options;
  options.url = std::move(url);
  options.token = std::move(token);
  options.connect_timeout = 10;
  options.operation_timeout = std::chrono::seconds(2);
  options.auto_subscribe = false;
  options.reconnect.enabled = false;
  if (!engine_->connect(std::move(options))) {
    failure_ = disconnected("Control media connection failed");
    return;
  }
  for (const auto& participant : engine_->remote_participants()) {
    const auto state = participant.snapshot();
    if (state.identity == peer_) {
      peer_sid_ = state.sid;
    }
  }
}

RtcConnection::~RtcConnection() {
  close();
}

void RtcConnection::on_room_event(const rtc::RoomEvent& event) {
  std::lock_guard lock(mutex_);
  if (event.kind == rtc::RoomEventKind::connection_changed &&
      (event.connection == rtc::ConnectionState::reconnecting ||
       event.connection == rtc::ConnectionState::failed ||
       event.connection == rtc::ConnectionState::closed)) {
    event_failure_ = disconnected("Media connection ended; request a new control grant");
  }
  // Preserve departure/replacement evidence even if a new roster arrives before poll().
  if (event.kind == rtc::RoomEventKind::participant_disconnected &&
      event.participant_identity == peer_) {
    event_failure_ = disconnected("Control peer disconnected; request a new control grant");
  }
  if (event.kind != rtc::RoomEventKind::data_received || !event.data ||
      event.participant_identity != peer_ || event.data->topic != sdk::remote_control_topic) {
    return;
  }
  if (event.data->payload.size() > 4096 || packets_.size() >= 256) {
    event_failure_ = sdk::make_failure(sdk::ErrorDomain::data, sdk::ErrorCode::resource_exhausted,
                                       "Control input queue overflow");
    return;
  }
  packets_.push_back(
      {event.data->payload, {event.participant_identity, event.data->topic, event.data->reliable}});
}

sdk::Result<void> RtcConnection::status() const {
  std::lock_guard lock(mutex_);
  auto failure = failure_ ? failure_ : event_failure_;
  return failure ? sdk::Result<void>::failure(failure) : sdk::Result<void>::success();
}

bool RtcConnection::connected() const {
  return bool(status()) && !closed_ && engine_ &&
         engine_->connection_state() == rtc::ConnectionState::connected;
}

void RtcConnection::close() {
  if (closed_) {
    return;
  }
  closed_ = true;
  if (engine_) {
    engine_->set_observer(nullptr);
    engine_->close();
  }
  std::lock_guard lock(mutex_);
  frames_.clear();
  packets_.clear();
}

bool RtcConnection::owned(const std::string& sid) const {
  for (const auto& participant : engine_->remote_participants()) {
    const auto state = participant.snapshot();
    if (state.identity == peer_ && !peer_sid_.empty() && state.sid == peer_sid_ &&
        state.publications.count(sid)) {
      return true;
    }
  }
  return false;
}

sdk::Result<void> RtcConnection::publish_screen(int width, int height, bool generated) {
  if (auto result = status(); !result) {
    return result;
  }
  rtc::MediaPublishSpec spec;
  spec.name = "Screen";
  spec.options.stream_group = "screen";
  spec.options.source = rtc::PublicationSource::screen;
  spec.format.codec = rtc::Codec::vp8;
  spec.format.width = width;
  spec.format.height = height;
  spec.input.generated = generated;
  spec.input.fps = 20;
  if (!generated) {
    spec.input.capture = sdk::CaptureKind::screen;
  }
  auto result = engine_->publish_media(std::move(spec));
  if (!result) {
    return sdk::Result<void>::failure(result.failure_record());
  }
  screen_ = result->track;
  return sdk::Result<void>::success();
}

sdk::Result<void> RtcConnection::poll() {
  if (auto result = status(); !result) {
    return result;
  }
  if (!connected()) {
    return sdk::Result<void>::failure(disconnected("Media connection ended"));
  }
  bool present = false;
  std::map<rtc::TrackSid, rtc::RemoteTrackPublication> publications;
  for (const auto& participant : engine_->remote_participants()) {
    const auto state = participant.snapshot();
    if (state.identity != peer_) {
      continue;
    }
    if (!peer_sid_.empty() && peer_sid_ != state.sid) {
      return sdk::Result<void>::failure(disconnected("Control peer connection was replaced"));
    }
    peer_sid_ = state.sid;
    present = true;
    for (const auto& pub : participant.track_publications()) {
      if (receive_video_ && pub.snapshot().kind == rtc::MediaKind::video) {
        publications.emplace(pub.snapshot().sid, pub);
      }
    }
  }
  if (!peer_sid_.empty() && !present) {
    return sdk::Result<void>::failure(disconnected("Control peer disconnected"));
  }
  if (publications.size() > 2) {
    return sdk::Result<void>::failure(disconnected("Too many remote screen tracks"));
  }
  for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
    if (!publications.count(it->first)) {
      engine_->unsubscribe_track(it->first);
      {
        std::lock_guard lock(mutex_);
        frames_.erase(it->first);
      }
      it = subscriptions_.erase(it);
    } else {
      ++it;
    }
  }
  for (const auto& [sid, pub] : publications) {
    if (subscriptions_.count(sid)) {
      continue;
    }
    rtc::MediaSubscribeSpec spec;
    spec.publication = pub;
    spec.output.video = [this, sid](const auto& frame) {
      if (!stop_) {
        std::lock_guard lock(mutex_);
        frames_[sid] = frame.pixels;
      }
      return sdk::Result<void>::success();
    };
    auto result = engine_->subscribe_media(std::move(spec));
    if (!result) {
      return sdk::Result<void>::failure(result.failure_record());
    }
    subscriptions_[sid] = result->track;
  }
  auto check = [](const std::shared_ptr<rtc::MediaTrack>& track) -> sdk::Result<void> {
    const auto snapshot = track->snapshot();
    if (snapshot.failure) {
      return sdk::Result<void>::failure(snapshot.failure);
    }
    if (snapshot.transport.state == rtc::TrackState::closed ||
        snapshot.transport.state == rtc::TrackState::failed) {
      return sdk::Result<void>::failure(disconnected("Control media track ended"));
    }
    return sdk::Result<void>::success();
  };
  if (screen_) {
    if (auto result = check(screen_); !result) {
      return result;
    }
  }
  for (const auto& [sid, track] : subscriptions_) {
    if (auto result = check(track); !result) {
      return result;
    }
  }
  return status();
}

sdk::Result<std::vector<RtcConnection::Packet>> RtcConnection::take_data() {
  using Outcome = sdk::Result<std::vector<Packet>>;
  if (auto result = status(); !result) {
    return Outcome::failure(result.failure_record());
  }
  std::vector<Packet> packets;
  {
    std::lock_guard lock(mutex_);
    packets.swap(packets_);
  }
  return Outcome::success(std::move(packets));
}

sdk::Result<void> RtcConnection::send(std::vector<uint8_t> bytes) {
  if (!connected()) {
    return sdk::Result<void>::failure(disconnected("Control channel closed"));
  }
  const auto permissions = engine_->local_participant().snapshot().permissions;
  if (bytes.size() > 4096 || (permissions && !permissions->can_publish_data)) {
    return sdk::Result<void>::failure(sdk::make_failure(
        sdk::ErrorDomain::data, sdk::ErrorCode::invalid_state, "Control data cannot be submitted"));
  }
  rtc::DataPacket packet;
  packet.payload = std::move(bytes);
  packet.topic = sdk::remote_control_topic;
  packet.destinations = {peer_};
  engine_->publish_data(std::move(packet));
  return status();
}

std::shared_ptr<const sdk::VideoFrame> RtcConnection::take_frame() {
  std::map<rtc::TrackSid, std::shared_ptr<const sdk::VideoFrame>> frames;
  {
    std::lock_guard lock(mutex_);
    frames.swap(frames_);
  }
  for (const auto& [sid, frame] : frames) {
    if (owned(sid)) {
      return frame;
    }
  }
  return {};
}
}  // namespace remote_app
