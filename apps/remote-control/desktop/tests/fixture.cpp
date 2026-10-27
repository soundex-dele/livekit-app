#include "services/session.hpp"
#include <iostream>
#include <fstream>
struct Recorder:livekit::sdk::ControlInputSink {
  std::mutex mutex;std::ofstream output;bool text_enabled;
  explicit Recorder(const std::string&path,bool text=true):output(path),text_enabled(text){}
  bool supports_text()const noexcept override{return text_enabled;}
  bool apply(const livekit::sdk::ControlInput&e)noexcept override{try{std::lock_guard lock(mutex);output<<livekit::sdk::Json{{"type",e.type},{"code",e.code},{"text",e.text},{"down",e.down},{"x",e.x},{"y",e.y},{"button",e.button},{"delta",e.delta}}.dump()<<'\n';output.flush();return output.good();}catch(...){return false;}}
};
// Dedicated synthetic-media test executable. Never creates a native input sink.
int main(int argc,char**argv){if(argc!=3&&argc!=4)return 2;if(argc==4&&std::string(argv[3])!="--no-text")return 2;try{
  remote_app::Session session(true,std::make_shared<Recorder>(argv[2],argc==3));session.start(argv[1]);bool printed=false;
  for(int i=0;i<6000;i++){auto state=session.snapshot();if(!printed&&!state.code.empty()){std::cout<<"PAIRING "<<state.code<<std::endl;printed=true;}if(state.state=="pending")session.approve();if(state.finished){if(!state.error.empty())std::cerr<<state.error<<std::endl;return state.error.empty()?0:1;}std::this_thread::sleep_for(std::chrono::milliseconds(20));}
  session.stop();return 3;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
