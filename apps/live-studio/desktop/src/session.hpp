#pragma once
#include <livekit/studio.hpp>
#include "media_session.hpp"
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXWebSocket.h>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace studio_app {
namespace sdk=livekit::sdk;using Json=nlohmann::json;
struct Snapshot {Json user,live,rooms=Json::array(),events=Json::array();std::string server_url,broadcast,error,state="未开播";bool busy=false,connected=false;double bitrate_kbps=0,publish_fps=0;Json media_stats=Json::array();uint64_t stage_video_frames=0,stage_audio_frames=0;size_t stage_subscriptions=0;};
class Session {
 public:
 Session(std::filesystem::path settings,sdk::StudioOptions options,bool offline=false);
 ~Session();
 std::shared_ptr<sdk::Studio> engine()const;
 Snapshot snapshot()const;
 void login(std::string url,std::string name,std::string password,bool register_account);
 void logout();void change_password(std::string old_password,std::string password);
 void start(std::string title);void stop();
 void command(std::string action,Json payload=Json::object());
 void refresh_rooms();void reconfigure(sdk::StudioOptions options);
 void set_scene(sdk::StudioScene scene);void set_audio(sdk::StudioAudioSettings settings);
 private:
 void enqueue(std::function<void()>);void run();void connect(bool interaction);void close_stage();void update_stage();void store_token();void disconnect();
 Json request(const std::string&,const Json* body=nullptr);void events_start();
 mutable std::mutex mutex_;Snapshot state_;std::deque<std::function<void()>>commands_;std::deque<Json>incoming_;std::shared_ptr<sdk::Studio>engine_;
 std::filesystem::path settings_;std::string url_="http://127.0.0.1:8080",token_,broadcast_,stage_key_;
 std::unique_ptr<MediaSession>program_,stage_;
 std::unique_ptr<ix::WebSocket>events_;int64_t sequence_=0;std::atomic<bool>closing_{false};std::jthread worker_;sdk::StudioScene scene_;sdk::StudioAudioSettings audio_;
};
}
