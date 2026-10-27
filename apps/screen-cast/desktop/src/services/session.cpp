#include "services/session.hpp"
#include <av/http.hpp>
#include <algorithm>
#include <stdexcept>
using namespace std::chrono_literals;
namespace screen_cast {
Session::~Session(){stop();if(worker_.joinable())worker_.join();}
Snapshot Session::snapshot()const{std::lock_guard lock(mutex_);return snapshot_;}
void Session::stop(){
 stop_=true;cancellation_.request_stop();std::shared_ptr<sdk::Room> room;
 {std::lock_guard lock(mutex_);room=room_;snapshot_.preview.reset();if(!snapshot_.finished)snapshot_.state="stopping";}
 if(room)room->request_stop();
}
void Session::start(std::string url,std::string code,sdk::ScreenCaptureDevice display){
 if(!snapshot().finished)return;if(worker_.joinable())worker_.join();
 while(!url.empty()&&url.back()=='/')url.pop_back();
 stop_=false;cancellation_=std::stop_source{};
 {std::lock_guard lock(mutex_);snapshot_=Snapshot{};snapshot_.state="connecting";snapshot_.finished=false;}
 worker_=std::thread([this,url=std::move(url),code=std::move(code),display=std::move(display)]{run(url,code,display);});
}
void Session::run(std::string url,std::string code,sdk::ScreenCaptureDevice display){
 std::string id,key;std::shared_ptr<sdk::Room> room;
 const auto setState=[&](std::string state){std::lock_guard lock(mutex_);snapshot_.state=std::move(state);};
 try{
  if(code.size()!=10||!std::all_of(code.begin(),code.end(),[](char c){return c>='0'&&c<='9';}))throw std::runtime_error("Enter a 10-digit cast code");
  if(!fixture_){auto devices=sdk::screen_capture_devices();if(std::find(devices.begin(),devices.end(),display)==devices.end())throw std::runtime_error("Display changed; refresh and select again");}
  if(!stop_){
   sdk::Json body{{"code",code}};auto v=av::request(url,"/api/v1/screen-cast/redeem","",&body);id=v.at("id");key=v.at("credential");
   if(!stop_){
    auto empty=sdk::Json::object();auto conn=av::request(url,"/api/v1/screen-cast/sessions/"+id+"/connect",key,&empty);
    sdk::RoomCallback cb;cb.on_disconnected=[this](int){stop_=true;};cb.on_participant_disconnected=[this](const sdk::RemoteParticipant&){stop_=true;};
    room=std::make_shared<sdk::Room>(cb);{std::lock_guard lock(mutex_);room_=room;}
    sdk::RoomOptions opts;opts.subscribe=false;opts.audio_playout=false;opts.connect_timeout=10;
    opts.reconnect.enabled=false; // Both clients end the one-use business session on disconnect.
    auto size=sdk::screen_capture_size(display.width,display.height);sdk::MediaSpec screen;screen.width=size.width;screen.height=size.height;screen.fps=15;screen.local_preview=true;
    if(fixture_)screen.demo=true;else{screen.capture=sdk::CaptureKind::screen;screen.screen_id=display.id;}
    opts.media={screen};opts.video_frame_handler=[this](const sdk::VideoFrame& frame){if(stop_)return;auto copy=std::make_shared<sdk::VideoFrame>(frame);std::lock_guard lock(mutex_);if(!stop_)snapshot_.preview=std::move(copy);};
    if(!stop_){
     room->join_with_token(conn.at("media_url"),conn.at("media_token"),opts,cancellation_.get_token());
     auto next=std::chrono::steady_clock::now();setState("paired");
     while(!stop_&&room->connection_state()==sdk::ConnectionState::connected){
      if(std::chrono::steady_clock::now()>=next){sdk::Json beat{{"connected",true}};v=av::request(url,"/api/v1/screen-cast/sessions/"+id+"/heartbeat",key,&beat);auto state=v.at("state").get<std::string>();setState(state);if(state=="ended"||state=="expired"){auto reason=v.value("reason",std::string{});if(reason!="stopped"){std::lock_guard lock(mutex_);snapshot_.error=reason;}break;}next=std::chrono::steady_clock::now()+3s;}
      std::this_thread::sleep_for(20ms);
     }
    }
   }
  }
 }catch(const std::exception&e){if(!stop_){std::lock_guard lock(mutex_);snapshot_.error=e.what();}}
 // Join media/capture before notifying the service. No UI lock across SDK joins.
 stop();
 if(room){room->request_stop();try{room->wait();}catch(const std::exception&e){std::lock_guard lock(mutex_);snapshot_.error=e.what();}room->disconnect();}
 {std::lock_guard lock(mutex_);room_.reset();}
 room.reset();
 if(!id.empty()){try{auto empty=sdk::Json::object();av::request(url,"/api/v1/screen-cast/sessions/"+id+"/stop",key,&empty);}catch(const std::exception&e){std::lock_guard lock(mutex_);if(snapshot_.error.empty())snapshot_.error=e.what();}}
 {std::lock_guard lock(mutex_);snapshot_.preview.reset();snapshot_.state="ended";snapshot_.finished=true;}
}
}
