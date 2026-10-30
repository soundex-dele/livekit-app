#include "rtc_result.hpp"
using livekit::sdk::app_support::require_result;
#include "livekit/rtc/livekit.hpp"
#include "services/rtc_connection.hpp"
#include <algorithm>

namespace remote_app {
using namespace std::chrono_literals;

RtcConnection::RtcConnection(std::string url, std::string token, std::string peer,
                             bool receive_video, const std::atomic<bool>& stop)
    : engine_(require_result(rtc::engine::create_livekit_engine({}))),
      engine_events_(*engine_),
      data_events_(require_result(engine_->room().services())),
      peer_(std::move(peer)),
      stop_(stop),
      receive_video_(receive_video) {
  rtc::LiveKitRoomOptions options;
  options.url = std::move(url);
  options.token = token;
  options.connect_timeout = 10;
  options.operation_timeout = 10s;
  options.reconnect.enabled = false;
  try {
    auto joining = engine_->join(options);
    await(joining);
  } catch (const std::exception& error) {
    std::string message = error.what();
    if (!token.empty())
      for (size_t pos; (pos = message.find(token)) != std::string::npos;)
        message.replace(pos, token.size(), "[redacted]");
    throw std::runtime_error(message);
  }
  if (auto state = engine_->room().snapshot().session)
    for (const auto& [sid, participant] : state->remote)
      if (participant.identity == peer_)
        peer_sid_ = sid;
}

RtcConnection::~RtcConnection() {
  try {
    close();
  } catch (...) {
  }
}

bool RtcConnection::connected() const {
  return !closed_ && engine_->room().snapshot().state == rtc::ConnectionState::connected;
}

void RtcConnection::close() {
  if (closed_)
    return;
  closed_ = true;
  require_result(engine_->close());
  std::lock_guard lock(frames_mutex_);
  frames_.clear();
}

bool RtcConnection::owned(const std::string& sid) const {
  const auto state = engine_->room().snapshot().session;
  if (!state)
    return false;
  for (const auto& [id, participant] : state->remote)
    if (participant.identity == peer_ && (peer_sid_.empty() || id == peer_sid_) &&
        participant.publications.contains(sid))
      return true;
  return false;
}

void RtcConnection::publish_screen(int width, int height, bool generated) {
  rtc::TrackDescriptor d;
  d.id = "screen";
  d.name = "Screen";
  d.stream_group = "screen";
  d.source = rtc::PublicationSource::screen;
  d.format.codec = rtc::Codec::vp8;
  d.format.width = width;
  d.format.height = height;
  rtc::engine::MediaInput input;
  input.generated = generated;
  input.fps = 20;
  if (!generated)
    input.capture = sdk::CaptureKind::screen;
  rtc::engine::MediaPublishSpec config;
  config.track = d;
  config.input = input;
  auto publication = require_result(engine_->publish_video(std::move(config)));
}

void RtcConnection::subscribe(const rtc::TrackDescriptor& d) {
  rtc::engine::MediaSubscribeSpec config;
  config.track = d;
  config.output.video = [this, id = d.id](const auto& frame) {
    if (stop_)
      return;
    std::lock_guard lock(frames_mutex_);
    frames_[id] = frame.pixels;
  };
  auto subscription = require_result(engine_->subscribe_media(std::move(config)));
  subscriptions_.insert(d.id);
}

void RtcConnection::poll() {
  while (auto event = engine_events_.take_event(0ms)) {
    if (event->kind == rtc::RoomEventKind::closed)
      throw ConnectionEnded("Media connection ended");
    if (event->kind == rtc::RoomEventKind::failed ||
        event->kind == rtc::RoomEventKind::reconnecting)
      throw std::runtime_error("Media connection ended; request a new control grant");
    if (event->kind == rtc::RoomEventKind::remote_track_added && event->descriptor &&
        receive_video_ && event->descriptor->kind == rtc::MediaKind::video) {
      if (discovered_.size() >= 2)
        throw std::runtime_error("Too many remote screen tracks");
      discovered_[event->track_id] = *event->descriptor;
    }
    if (event->kind == rtc::RoomEventKind::remote_track_removed ||
        event->kind == rtc::RoomEventKind::track_removed) {
      if (event->track_id == "screen")
        throw std::runtime_error("Screen publication revoked");
      if (subscriptions_.contains(event->track_id)) {
        require_result(engine_->unsubscribe(event->track_id));
        subscriptions_.erase(event->track_id);
        std::lock_guard lock(frames_mutex_);
        frames_.erase(event->track_id);
      }
      discovered_.erase(event->track_id);
    }
  }
  const auto state = engine_->room().snapshot().session;
  bool present = false;
  if (state)
    for (const auto& [sid, participant] : state->remote)
      if (participant.identity == peer_) {
        if (!peer_sid_.empty() && peer_sid_ != sid)
          throw ConnectionEnded("Control peer connection was replaced");
        peer_sid_ = sid;
        present = true;
      }
  if (!peer_sid_.empty() && !present)
    throw ConnectionEnded("Control peer disconnected");
  for (const auto& [id, d] : discovered_)
    if (!subscriptions_.contains(id) && owned(d.remote_sid))
      subscribe(d);

  if (auto failure = engine_events_.take_failure())
    throw std::runtime_error(failure->message);
}

std::vector<RtcConnection::Packet> RtcConnection::take_data() {
  std::vector<Packet> packets;
  for (size_t count = 0; count < 256; ++count) {
    auto event = data_events_.take_event(0ms);
    if (!event)
      break;
    if (const auto* error = std::get_if<rtc::DataServiceError>(&event->value))
      throw std::runtime_error(error->message);
    const auto* data = std::get_if<rtc::ReceivedData>(&event->value);
    if (!data || data->topic != sdk::remote_control_topic)
      continue;
    if (data->bytes.size() > 4096 || packets.size() >= 256)
      throw std::runtime_error("Control input queue overflow");
    packets.push_back({data->bytes, {event->sender_identity, data->topic, event->reliable}});
  }
  return packets;
}

std::future<void> RtcConnection::send(std::vector<uint8_t> data) {
  rtc::DataSendOptions options;
  options.topic = sdk::remote_control_topic;
  options.destinations = {peer_};
  options.operation.timeout = 2s;
  return require_result(engine_->room().services())->send_data(std::move(data), options);
}

std::shared_ptr<const sdk::VideoFrame> RtcConnection::take_frame() {
  std::map<rtc::TrackId, std::shared_ptr<const sdk::VideoFrame>> frames;
  {
    std::lock_guard lock(frames_mutex_);
    frames.swap(frames_);
  }
  for (const auto& [id, frame] : frames) {
    const auto found = discovered_.find(id);
    if (found != discovered_.end() && owned(found->second.remote_sid))
      return frame;
  }
  return {};
}
}  // namespace remote_app
