#include "services/session.hpp"
#include "services/rtc_connection.hpp"
#include <algorithm>
#include <future>
#include <stdexcept>
#include <iostream>

using namespace std::chrono_literals;
namespace remote_app {
Session::Session(bool fixture,std::shared_ptr<sdk::ControlInputSink> sink):fixture_(fixture){
  if(fixture&&!sink)throw std::invalid_argument("Fixture requires recording input sink");
  control_=std::make_shared<sdk::RemoteControlSession>(sink?sink:sdk::make_platform_control_input());
}
Session::~Session(){stop();if(worker_.joinable())worker_.join();}
Snapshot Session::snapshot()const{std::lock_guard lock(mutex_);return snapshot_;}
void Session::start(std::string url){
  if(!snapshot().finished)return;if(worker_.joinable())worker_.join();
  while(!url.empty()&&url.back()=='/')url.pop_back();if(url.empty())throw std::invalid_argument("Service URL required");
  stop_=false;action_=0;{std::lock_guard lock(mutex_);snapshot_=Snapshot{};snapshot_.state="creating";snapshot_.finished=false;}
  worker_=std::thread([this,url=std::move(url)]{run(url);});
}
void Session::approve(){if(fixture_||sdk::control_permissions().ready())action_=1;}
void Session::reject(){action_=2;}
void Session::stop(){stop_=true;std::lock_guard lock(mutex_);control_->stop("local_stop");}
void Session::update(const sdk::Json& v,sdk::ControlClock::time_point started){
  std::lock_guard lock(mutex_);snapshot_.state=v.at("state");snapshot_.name=v.at("controller_name");snapshot_.id=v.at("id");snapshot_.expires=v.value("expires_at","");
  grant_={v.at("grant_id"),v.at("controller_identity"),v.at("epoch"),started+std::chrono::milliseconds(v.at("lease_remaining_ms").get<int64_t>())};
}
void Session::run(std::string url){
  std::string id,credential;bool started_media=false;
  try {
    sdk::Json empty=sdk::Json::object();auto began=sdk::ControlClock::now();auto v=request(url,"/api/v1/remote-control/pairings","",&empty);id=v.at("id");credential=v.at("credential");update(v,began);
    {std::lock_guard lock(mutex_);snapshot_.code=v.at("pairing_code");}
    auto next=sdk::ControlClock::now();
    while(!stop_){
      auto now=sdk::ControlClock::now();int action=action_.exchange(0);
      if(action||now>=next){
        std::string route="/api/v1/remote-control/control-grants/"+id;
        const auto state=snapshot().state;
        if(action==1&&!fixture_&&!sdk::control_permissions().ready())throw std::runtime_error("屏幕或辅助功能权限不足，请检查系统设置");
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
  if(!id.empty())try{sdk::Json empty=sdk::Json::object();request(url,"/api/v1/remote-control/control-grants/"+id+"/revoke",credential,&empty);}catch(const std::exception&e){std::lock_guard lock(mutex_);if(snapshot_.error.empty())snapshot_.error=e.what();}
  {std::lock_guard lock(mutex_);snapshot_.state="ended";snapshot_.finished=true;}
}
void Session::media(std::string url,std::string id,std::string credential){
  // Fixture diagnostics contain timing and envelope metadata only, never text,
  // input contents, credentials or media tokens. Production does not emit them.
  auto trace=[enabled=fixture_,began=sdk::ControlClock::now()](const char* event,sdk::Json fields=sdk::Json::object()){
    if(!enabled)return;fields["event"]=event;fields["elapsed_ms"]=std::chrono::duration_cast<std::chrono::milliseconds>(sdk::ControlClock::now()-began).count();
    static std::mutex trace_mutex;std::lock_guard lock(trace_mutex);std::cerr<<fields.dump()<<'\n';
  };
  std::unique_ptr<RtcConnection> connection_media;
  try{
    sdk::Json empty=sdk::Json::object();auto connection=request(url,"/api/v1/remote-control/control-grants/"+id+"/connect",credential,&empty);if(stop_)return;
    sdk::ControlGrant grant;{std::lock_guard lock(mutex_);grant=grant_;}
    connection_media=std::make_unique<RtcConnection>(connection.at("media_url"),connection.at("media_token"),grant.controller_identity,false,stop_);
    auto layout=fixture_?sdk::ControlLayout{1,0,0,1280,720}:sdk::primary_control_layout();
    const auto height=std::max(2,static_cast<int>(1280.0*layout.height/layout.width)/2*2);
    connection_media->publish_screen(1280,height,fixture_);
    trace("room_joined");
    {std::lock_guard lock(mutex_);if(stop_)return;grant=grant_;control_->authorize(grant,layout);}
    std::deque<std::future<void>> sends;
    auto next=sdk::ControlClock::now();
    while(!stop_){
      connection_media->poll();
      auto packets=connection_media->take_data();
      {std::lock_guard lock(mutex_);grant=grant_;}
      if(!control_->renew(grant)){throw std::runtime_error(control_->reason());}
      for(auto&[bytes,params]:packets){trace("data_received",{{"bytes",bytes.size()},{"topic",params.topic},{"reliable",params.reliable}});auto accepted=control_->receive(bytes,params);trace("control_packet",{{"accepted",accepted},{"active",control_->active()}});}
      if(!control_->active()){
        if(control_->reason()=="peer_stopped"){stop_=true;break;}
        throw std::runtime_error(control_->reason());
      }
      while(!sends.empty()&&sends.front().wait_for(0ms)==std::future_status::ready){sends.front().get();sends.pop_front();trace("status_sent");}
      if(sends.size()>=8)throw std::runtime_error("Control status send queue stalled");
      if(sdk::ControlClock::now()>=next){
        if(!fixture_){if(!sdk::control_permissions().ready())throw std::runtime_error("权限或交互会话已失效");auto current=sdk::primary_control_layout();current.version=layout.version;if(current!=layout){current.version++;layout=current;control_->set_layout(layout);}}
        auto raw=control_->status().dump();sends.push_back(connection_media->send({raw.begin(),raw.end()}));
        next=sdk::ControlClock::now()+500ms;
      }
      std::this_thread::sleep_for(10ms);
    }
    control_->stop("media_ended");connection_media->close();stop_=true;
  }catch(const ConnectionEnded&){control_->stop("media_ended");stop_=true;}
  catch(const std::exception&e){const bool cancelled=stop_.exchange(true);trace("media_failed",{{"reason",control_->reason()}});control_->stop("media_error");if(!cancelled){std::lock_guard lock(mutex_);snapshot_.error=e.what();}}
  // Release held input before any potentially blocking graph/transport teardown.
  control_->stop("media_ended");
  if(connection_media)try{connection_media->close();}catch(const std::exception&e){std::lock_guard lock(mutex_);if(snapshot_.error.empty())snapshot_.error=e.what();}
}
}
