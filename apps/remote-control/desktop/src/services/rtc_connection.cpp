#include "rtc_blocking_bridge.hpp"
#include "livekit/rtc/livekit.hpp"
#include "services/rtc_connection.hpp"
#include <algorithm>

namespace remote_app {
using namespace std::chrono_literals;

namespace {
template <class T, class Start>
sdk::Result<T> complete(Start start) {
  std::promise<sdk::Result<T>> completion;
  auto future = completion.get_future();
  start([&completion](sdk::Result<T> value) { completion.set_value(std::move(value)); });
  return future.get();
}
}  // namespace

RtcConnection::RtcConnection(std::string url, std::string token, std::string peer,
                             bool receive_video, const std::atomic<bool>& stop)
    : peer_(std::move(peer)), stop_(stop), receive_video_(receive_video) {
  auto created = sdk::app_support::create_blocking_engine(rtc::AudioOutputOptions{});
  if (!created) {
    failure_ = created.failure_record();
    return;
  }
  engine_ = *std::move(created);
  engine_events_.attach(*engine_);
  if (auto status = engine_events_.status(); !status) {
    failure_ = status.failure_record();
    return;
  }
  rtc::LiveKitRoomOptions options;
  options.url = std::move(url);
  options.token = token;
  options.connect_timeout = 10;
  options.operation_timeout = 10s;
  options.reconnect.enabled = false;
  auto joining = complete<rtc::RoomSnapshot>(
      [&](auto done) { engine_->raw().join(options, std::move(done)); });
  if (!joining) {
    auto failure = *joining.failure_record();
    if (!token.empty()) {
      for (size_t pos; (pos = failure.message.find(token)) != std::string::npos;) {
        failure.message.replace(pos, token.size(), "[redacted]");
      }
    }
    failure_ = std::make_shared<const sdk::FailureInfo>(std::move(failure));
    return;
  }
  auto services = engine_->room().services();
  if (!services) {
    failure_ = services.failure_record();
    return;
  }
  data_events_ = std::make_unique<sdk::app_support::DataEventMailbox>(*services);
  if (auto status = data_events_->status(); !status) {
    failure_ = status.failure_record();
    return;
  }
  if (auto state = engine_->room().snapshot().session)
    for (const auto& [sid, participant] : state->remote)
      if (participant.identity == peer_)
        peer_sid_ = sid;
}

RtcConnection::~RtcConnection() {
  (void)close();
}

sdk::Result<void> RtcConnection::status() const {
  return failure_ ? sdk::Result<void>::failure(failure_) : sdk::Result<void>::success();
}

bool RtcConnection::connected() const {
  return !failure_ && !closed_ && engine_ &&
         engine_->room().snapshot().state == rtc::ConnectionState::connected;
}

sdk::Result<void> RtcConnection::close() {
  if (closed_) {
    return sdk::Result<void>::success();
  }
  closed_ = true;
  auto result = engine_ ? complete<void>([&](auto done) { engine_->raw().leave(std::move(done)); })
                        : sdk::Result<void>::success();
  std::lock_guard lock(frames_mutex_);
  frames_.clear();
  return result;
}

bool RtcConnection::owned(const std::string& sid) const {
  const auto state = engine_->room().snapshot().session;
  if (!state)
    return false;
  for (const auto& [id, participant] : state->remote)
    if (participant.identity == peer_ && (peer_sid_.empty() || id == peer_sid_) &&
        participant.publications.count(sid))
      return true;
  return false;
}

sdk::Result<void> RtcConnection::publish_screen(int width, int height, bool generated) {
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
  if (!status()) {
    return status();
  }
  auto publication = engine_->publish_video(std::move(config));
  return publication ? sdk::Result<void>::success()
                     : sdk::Result<void>::failure(publication.failure_record());
}

sdk::Result<void> RtcConnection::subscribe(const rtc::TrackDescriptor& d) {
  rtc::engine::MediaSubscribeSpec config;
  config.track = d;
  config.output.video = [this, id = d.id](const auto& frame) -> sdk::Result<void> {
    if (stop_)
      return sdk::Result<void>::success();
    std::lock_guard lock(frames_mutex_);
    frames_[id] = frame.pixels;
    return sdk::Result<void>::success();
  };
  auto subscription = engine_->subscribe_media(std::move(config));
  if (!subscription) {
    return sdk::Result<void>::failure(subscription.failure_record());
  }
  subscriptions_.insert(d.id);
  return sdk::Result<void>::success();
}

sdk::Result<void> RtcConnection::poll() {
  if (!status()) {
    return status();
  }
  while (true) {
    auto received = engine_events_.take_event(0ms);
    if (!received) {
      return sdk::Result<void>::failure(received.failure_record());
    }
    auto event = *std::move(received);
    if (!event) {
      break;
    }
    if (event->kind == rtc::RoomEventKind::closed)
      return sdk::Result<void>::failure(sdk::make_failure(
          sdk::ErrorDomain::transport, sdk::ErrorCode::session_closed, "Media connection ended"));
    if (event->kind == rtc::RoomEventKind::failed ||
        event->kind == rtc::RoomEventKind::reconnecting)
      return sdk::Result<void>::failure(
          sdk::make_failure(sdk::ErrorDomain::transport, sdk::ErrorCode::connection_lost,
                            "Media connection ended; request a new control grant"));
    if (event->kind == rtc::RoomEventKind::remote_track_added && event->descriptor &&
        receive_video_ && event->descriptor->kind == rtc::MediaKind::video) {
      if (discovered_.size() >= 2)
        return sdk::Result<void>::failure(sdk::make_failure(sdk::ErrorDomain::transport,
                                                            sdk::ErrorCode::connection_lost,
                                                            "Too many remote screen tracks"));
      discovered_[event->track_id] = *event->descriptor;
    }
    if (event->kind == rtc::RoomEventKind::remote_track_removed ||
        event->kind == rtc::RoomEventKind::track_removed) {
      if (event->track_id == "screen")
        return sdk::Result<void>::failure(sdk::make_failure(sdk::ErrorDomain::transport,
                                                            sdk::ErrorCode::connection_lost,
                                                            "Screen publication revoked"));
      if (subscriptions_.count(event->track_id)) {
        auto removed = complete<void>(
            [&](auto done) { engine_->raw().unsubscribe(event->track_id, std::move(done)); });
        if (!removed) {
          return removed;
        }
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
          return sdk::Result<void>::failure(
              sdk::make_failure(sdk::ErrorDomain::transport, sdk::ErrorCode::session_closed,
                                "Control peer connection was replaced"));
        peer_sid_ = sid;
        present = true;
      }
  if (!peer_sid_.empty() && !present)
    return sdk::Result<void>::failure(sdk::make_failure(
        sdk::ErrorDomain::transport, sdk::ErrorCode::session_closed, "Control peer disconnected"));
  for (const auto& [id, d] : discovered_)
    if (!subscriptions_.count(id) && owned(d.remote_sid))
      if (auto subscribed = subscribe(d); !subscribed) {
        return subscribed;
      }

  auto failure = engine_events_.take_failure();
  if (!failure) {
    return sdk::Result<void>::failure(failure.failure_record());
  }
  if (*failure) {
    return sdk::Result<void>::failure((*failure)->failure
                                          ? (*failure)->failure
                                          : sdk::make_failure(sdk::ErrorDomain::media,
                                                              sdk::ErrorCode::processing_failed,
                                                              (*failure)->message));
  }
  return sdk::Result<void>::success();
}

sdk::Result<std::vector<RtcConnection::Packet>> RtcConnection::take_data() {
  using Outcome = sdk::Result<std::vector<Packet>>;
  if (!status()) {
    return Outcome::failure(failure_);
  }
  std::vector<Packet> packets;
  for (size_t count = 0; count < 256; ++count) {
    auto received = data_events_->take_event(0ms);
    if (!received) {
      return Outcome::failure(received.failure_record());
    }
    auto event = *std::move(received);
    if (!event)
      break;
    if (const auto* error = std::get_if<rtc::DataServiceError>(&event->value))
      return Outcome::failure(error->failure ? error->failure
                                             : sdk::make_failure(sdk::ErrorDomain::data,
                                                                 sdk::ErrorCode::request_failed,
                                                                 error->message));
    const auto* data = std::get_if<rtc::ReceivedData>(&event->value);
    if (!data || data->topic != sdk::remote_control_topic)
      continue;
    if (data->bytes.size() > 4096 || packets.size() >= 256)
      return Outcome::failure(sdk::make_failure(sdk::ErrorDomain::data,
                                                sdk::ErrorCode::resource_exhausted,
                                                "Control input queue overflow"));
    packets.push_back({data->bytes, {event->sender_identity, data->topic, event->reliable}});
  }
  return Outcome::success(std::move(packets));
}

std::future<sdk::Result<void>> RtcConnection::send(std::vector<uint8_t> data) {
  rtc::DataSendOptions options;
  options.topic = sdk::remote_control_topic;
  options.destinations = {peer_};
  options.operation.timeout = 2s;
  auto services = engine_ ? engine_->room().services()
                          : sdk::Result<std::shared_ptr<rtc::RoomServices>>::failure(failure_);
  if (!services) {
    std::promise<sdk::Result<void>> completed;
    completed.set_value(sdk::Result<void>::failure(services.failure_record()));
    return completed.get_future();
  }
  return (*services)->send_data(std::move(data), options);
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
