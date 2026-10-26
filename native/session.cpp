#include "session.hpp"
#include <algorithm>
#include <future>
#include <stdexcept>
using namespace std::chrono_literals;
namespace remote_app {
Session::Session(bool fixture,std::shared_ptr<sdk::ControlInputSink> sink):fixture_(fixture){
  if(fixture&&!sink)throw std::invalid_argument("Fixture requires recording input sink");
  control_=std::make_shared<sdk::RemoteControlSession>(sink?sink:sdk::make_windows_control_input());
}
Session::~Session(){stop();if(worker_.joinable())worker_.join();}
Snapshot Session::snapshot()const{std::lock_guard lock(mutex_);return snapshot_;}
void Session::start(std::string url){
  if(!snapshot().finished)return;if(worker_.joinable())worker_.join();
  while(!url.empty()&&url.back()=='/')url.pop_back();if(url.empty())throw std::invalid_argument("Service URL required");
  stop_=false;action_=0;cancellation_=std::stop_source{};{std::lock_guard lock(mutex_);snapshot_=Snapshot{};snapshot_.state="creating";snapshot_.finished=false;incoming_.clear();}
  worker_=std::thread([this,url=std::move(url)]{run(url);});
}
void Session::approve(){action_=1;}
void Session::reject(){action_=2;}
void Session::stop(){stop_=true;cancellation_.request_stop();control_->stop("local_stop");std::shared_ptr<sdk::Room> room;{std::lock_guard lock(mutex_);room=room_;}if(room)room->request_stop();}
void Session::update(const sdk::Json& v,sdk::ControlClock::time_point started){
  std::lock_guard lock(mutex_);snapshot_.state=v.at("state");snapshot_.name=v.at("controller_name");snapshot_.id=v.at("id");
  grant_={v.at("grant_id"),v.at("controller_identity"),v.at("epoch"),started+std::chrono::milliseconds(v.at("lease_remaining_ms").get<int64_t>())};
}
void Session::run(std::string url){
  std::string id,credential;bool started_media=false;
  try {
    sdk::Json empty=sdk::Json::object();auto began=sdk::ControlClock::now();auto v=request(url,"/api/v1/pairings","",&empty);id=v.at("id");credential=v.at("credential");update(v,began);
    {std::lock_guard lock(mutex_);snapshot_.code=v.at("pairing_code");}
    auto next=sdk::ControlClock::now();
    while(!stop_){
      auto now=sdk::ControlClock::now();int action=action_.exchange(0);
      if(action||now>=next){
        std::string route="/api/v1/control-grants/"+id;
        const auto state=snapshot().state;
        if(action)route+=action==1?"/approve":"/reject";else if(state=="approved")route+="/renew";
        auto request_started=sdk::ControlClock::now();v=request(url,route,credential,(action||state=="approved")?&empty:nullptr);update(v,request_started);
        if(stop_)break;
        if(v.at("state")=="approved"){
          if(!started_media){started_media=true;rtc_=std::thread([this,url,id,credential]{media(url,id,credential);});}
        }else if(v.at("state")!="pending"&&v.at("state")!="waiting")break;
        next=sdk::ControlClock::now()+(v.at("state")=="approved"?3s:1s);
      }
      std::this_thread::sleep_for(20ms);
    }
  }catch(const std::exception&e){std::lock_guard lock(mutex_);snapshot_.error=e.what();}
  stop();if(rtc_.joinable())rtc_.join();
  if(!id.empty())try{sdk::Json empty=sdk::Json::object();request(url,"/api/v1/control-grants/"+id+"/revoke",credential,&empty);}catch(const std::exception&e){std::lock_guard lock(mutex_);if(snapshot_.error.empty())snapshot_.error=e.what();}
  // Room destruction joins its callback thread. Never hold the snapshot mutex
  // across that join: participant callbacks also acquire this mutex.
  std::shared_ptr<sdk::Room> finished_room;{std::lock_guard lock(mutex_);finished_room=std::move(room_);}
  if(finished_room){finished_room->request_stop();finished_room->disconnect();finished_room.reset();}
  {std::lock_guard lock(mutex_);snapshot_.state="ended";snapshot_.finished=true;}
}
void Session::media(std::string url,std::string id,std::string credential){
  try{
    sdk::Json empty=sdk::Json::object();auto connection=request(url,"/api/v1/control-grants/"+id+"/connect",credential,&empty);if(stop_)return;
    sdk::RoomCallback cb;
    cb.on_disconnected=[this](int){control_->stop("media_disconnected");stop_=true;};
    cb.on_participant_disconnected=[this](const sdk::RemoteParticipant& p){std::string identity;{std::lock_guard lock(mutex_);identity=grant_.controller_identity;}if(p.identity()==identity){control_->stop("controller_disconnected");stop_=true;}};
    cb.on_data_received=[this](const auto& bytes,const auto& params){
      if(params.topic!=sdk::remote_control_topic)return;
      bool overflow=false;{std::lock_guard lock(mutex_);if(bytes.size()>4096||incoming_.size()>=256)overflow=true;else incoming_.emplace_back(bytes,params);}
      if(overflow){control_->stop("input_queue_overflow");stop_=true;}
    };
    auto room=std::make_shared<sdk::Room>(cb);{std::lock_guard lock(mutex_);room_=room;}
    if(stop_)return;
    sdk::RoomOptions options;options.connect_timeout=10;options.audio_playout=false;
    auto layout=fixture_?sdk::ControlLayout{1,0,0,1280,720}:sdk::primary_control_layout();
    sdk::MediaSpec screen;screen.width=1280;screen.height=std::max(2,static_cast<int>(1280.0*layout.height/layout.width)/2*2);screen.fps=20;
    if(fixture_)screen.demo=true;else screen.capture=sdk::CaptureKind::screen;
    options.media={screen};
    room->join_with_token(connection.at("media_url"),connection.at("media_token"),options,cancellation_.get_token());
    if(stop_){room->request_stop();room->wait();return;}
    sdk::ControlGrant grant;{std::lock_guard lock(mutex_);grant=grant_;}
    control_->authorize(grant,layout);
    std::deque<std::future<void>> sends;
    auto next=sdk::ControlClock::now();
    while(!stop_&&room->connection_state()==sdk::ConnectionState::connected){
      std::deque<std::pair<std::vector<uint8_t>,sdk::DataReceiveParams>> packets;
      {std::lock_guard lock(mutex_);grant=grant_;packets.swap(incoming_);}
      if(!control_->renew(grant)){throw std::runtime_error(control_->reason());}
      for(auto&[bytes,params]:packets)control_->receive(bytes,params);
      if(!control_->active())throw std::runtime_error(control_->reason());
      while(!sends.empty()&&sends.front().wait_for(0ms)==std::future_status::ready){sends.front().get();sends.pop_front();}
      if(sends.size()>=8)throw std::runtime_error("Control status send queue stalled");
      if(sdk::ControlClock::now()>=next){
        if(!fixture_){auto current=sdk::primary_control_layout();current.version=layout.version;if(current!=layout){current.version++;layout=current;control_->set_layout(layout);}}
        auto raw=control_->status().dump();sends.push_back(room->local_participant().publish_data({raw.begin(),raw.end()},{true,sdk::remote_control_topic,{grant.controller_identity}}));
        next=sdk::ControlClock::now()+500ms;
      }
      std::this_thread::sleep_for(10ms);
    }
    control_->stop("media_ended");room->request_stop();room->wait();stop_=true;
  }catch(const std::exception&e){control_->stop("media_error");stop_=true;std::lock_guard lock(mutex_);snapshot_.error=e.what();}
}
}
