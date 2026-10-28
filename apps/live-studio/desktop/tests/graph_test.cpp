#include "engines/studio/studio_graph.hpp"
#include "media_nodes/capture/video_source.hpp"
#include "media_nodes/capture/audio_source.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <thread>

using namespace livekit::sdk;
using namespace livekit::sdk::studio_detail;
using Clock=std::chrono::steady_clock;
using graphflow::core::LifecycleState;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void fails(F operation){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}check(failed,"expected operation to fail");}
void states(const StudioGraph& graph,LifecycleState expected){for(const auto& node:graph.nodes())check(node.state==expected,"graph must own every node lifecycle");}
std::shared_ptr<StudioImage> solid(uint8_t r,uint8_t g,uint8_t b){
 auto image=std::make_shared<StudioImage>();image->width=image->height=2;
 for(int i=0;i<4;++i)image->rgba.insert(image->rgba.end(),{r,g,b,255});return image;
}
void color(const StudioImage& image,int x,int y,int r,int g,int b){
 auto p=image.rgba.data()+(size_t(y)*image.width+x)*4;
 check(std::abs(int(p[0])-r)<5&&std::abs(int(p[1])-g)<5&&std::abs(int(p[2])-b)<5,"unexpected composited pixel");
}
void routing_and_lifecycle(){
 StudioOptions options;options.software_gpu=true;StudioGraphState state(options);
 StudioSource red;red.id="local";red.color={1,0,0,1};state.scene.sources={red};
 state.remoteVideo["guest"]=solid(0,0,255);state.remoteSeen["guest"]=Clock::now();
 state.remoteAudio["guest"].assign(19200,.25f);state.settings.remote_volume=.5f;
 detail::I420SourceReader local_video(state.localVideo),program_video(state.programVideo);
 detail::PcmSourceReader local_audio(state.localAudio),program_audio(state.programAudio);
 local_video.start();program_video.start();local_audio.start();program_audio.start();
 StudioGraph graph(state);
 states(graph,LifecycleState::Created);fails([&]{graph.step();});
 std::set<std::pair<std::string,std::string>> expected{
  {"studio.clock","studio.sources"},{"studio.clock","studio.recorder"},
  {"studio.sources","studio.mixer"},{"studio.sources","studio.compositor"},
  {"studio.mixer","studio.audio-output"},{"studio.mixer","studio.monitor"},{"studio.mixer","studio.recorder"},
  {"studio.compositor","studio.video-output"},{"studio.compositor","studio.preview"},{"studio.compositor","studio.recorder"}
 };
 for(const auto& edge:graph.connections()){check(!edge.type.empty(),"typed graph edge required");check(expected.erase({edge.from,edge.to})==1,"unexpected graph connection");}
 check(expected.empty(),"missing graph connections");
 graph.start();states(graph,LifecycleState::Running);graph.step();
 check(state.status.frames==1&&state.status.preview,"preview receives graph frame");
 check(state.programVideo->stats().queued_frames>0&&state.localVideo->stats().queued_frames>0,"video branches receive graph frame");
 auto local=local_video.read({}),program=program_video.read({});check(local&&program,"video publication frames");
 color(*from_i420(local->frame),960,360,255,0,0);
 auto program_image=from_i420(program->frame);color(*program_image,320,360,255,0,0);color(*program_image,960,360,0,0,255);
 color(*state.status.preview,960,360,0,0,255);
 check(state.programAudio->stats().queued_blocks>0&&state.localAudio->stats().queued_blocks>0,"audio branches receive graph blocks");
 auto local_pcm=local_audio.read(),program_pcm=program_audio.read();check(local_pcm&&program_pcm,"audio publication blocks");
 check(local_pcm->captured==program_pcm->captured,"program and local share audio clock");
 check(std::all_of(local_pcm->samples.begin(),local_pcm->samples.end(),[](int16_t v){return v==0;}),"remote audio leaked into interaction output");
 check(std::abs(int(program_pcm->samples[0])-4096)<3,"program remote gain");
 auto next_pcm=program_audio.read();check(next_pcm&&next_pcm->captured-program_pcm->captured==std::chrono::milliseconds(10),"10 ms audio timeline");
 for(const auto& node:graph.nodes())check(node.name=="studio.clock"?node.published>0:node.received>0,"media/control did not traverse a node");

 // Scene changes and removal of remote inputs must affect the next video tick.
 state.scene.sources[0].color={0,1,0,1};state.remoteVideo.clear();state.remoteAudio.clear();state.remoteSeen.clear();
 std::this_thread::sleep_for(std::chrono::milliseconds(35));graph.step();
 color(*state.status.preview,960,360,0,255,0);
 auto frames=state.status.frames;graph.stop();states(graph,LifecycleState::Stopped);
 fails([&]{graph.step();});check(state.status.frames==frames,"stopped graph produced frames");
 graph.start();graph.step();check(state.status.frames==frames+1,"graph restart did not produce a frame");

 // Exercise bounded publication queues and ensure backpressure cannot block
 // preview. Readers deliberately stop consuming while the graph keeps running.
 for(int i=0;i<5;++i){std::this_thread::sleep_for(std::chrono::milliseconds(35));graph.step();}
 check(state.programVideo->stats().queued_frames<=3&&state.programAudio->stats().queued_blocks<=6,"unbounded publisher queue");
 check(state.status.frames>=frames+6,"backpressure blocked preview");
 graph.release();graph.release();states(graph,LifecycleState::Released);
 check(state.programVideo->stats().closed&&state.localVideo->stats().closed&&state.programAudio->stats().closed&&state.localAudio->stats().closed,"release did not close all output branches");
 check(!program_video.read({})&&!program_audio.read(),"release did not signal EOS");fails([&]{graph.start();});
}
void recording_and_failure(const std::filesystem::path& directory){
 StudioOptions options;options.software_gpu=true;StudioGraphState state(options);
 StudioSource source;source.id="color";source.color={0,1,0,1};state.scene.sources={source};
 state.remoteAudio["guest"].assign(19200,.25f);state.remoteSeen["guest"]=Clock::now();
 auto path=directory/("graph-"+std::to_string(Clock::now().time_since_epoch().count())+".mp4");
 {
  StudioGraph graph(state);state.recordPath=path;state.recordWanted=true;
  graph.start();graph.step();check(state.status.recording,"recording control did not reach sink");
  for(int i=0;i<4;++i){std::this_thread::sleep_for(std::chrono::milliseconds(35));graph.step();}
  graph.stop();check(!state.status.recording&&state.status.error.empty(),"graph stop did not drain recorder");
 }
 check(std::filesystem::exists(path)&&std::filesystem::file_size(path)>1024,"graph recorder did not finalize MP4");
 auto mkv=path;mkv.replace_extension(".mkv");check(std::filesystem::exists(mkv),"recovery MKV missing");
 {
  // Feed the recorded program back through Player -> Sources -> Mixer/Composite.
  // This exercises both graph-owned pipelines without capture devices or a room.
  StudioGraphState playback(options);StudioSource file;file.id="file";file.kind=StudioSourceKind::video;file.input=path.string();
  playback.scene.sources={file};StudioGraph graph(playback);graph.start();
  bool saw_video=false,saw_audio=false;auto deadline=Clock::now()+std::chrono::seconds(3);
  while(Clock::now()<deadline&&!(saw_video&&saw_audio)) {
   graph.step();saw_audio|=playback.status.output_peak>.05f;
   if(auto preview=playback.status.preview){auto p=(size_t(360)*preview->width+640)*4;saw_video|=preview->rgba[p+1]>240&&preview->rgba[p]<10;}
   std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  check(playback.status.source_errors.empty(),"recorded file failed to open as a source");
  check(saw_video&&saw_audio,"file media did not reach composition and mixing through graph edges");
 }
 {
  StudioGraphState collision(options);collision.scene=state.scene;collision.recordPath=path;collision.recordWanted=true;
  StudioGraph graph(collision);graph.start();graph.step();
  check(!collision.recordWanted&&!collision.status.recording&&!collision.status.error.empty(),"recorder collision must be visible and nonfatal");
  check(collision.status.frames>0,"recorder failure stopped preview");
 }
 {
  StudioGraphState broken(options);broken.scene=state.scene;
  // Bypass the facade validator to force a processing error inside EventBus.
  broken.settings.microphone_volume=5;StudioGraph graph(broken);graph.start();
  fails([&]{graph.step();});check(broken.status.frames==0,"graph continued after a processing failure");
  fails([&]{graph.step();});graph.release();states(graph,LifecycleState::Released);
 }
 std::cout<<"studio graph recording="<<path.string()<<"\n";
}
}
int main(int argc,char** argv){
 HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 try{
  check(SUCCEEDED(com),"test COM initialization");routing_and_lifecycle();
  auto directory=argc>1?std::filesystem::path(argv[1]):std::filesystem::path("fixtures");std::filesystem::create_directories(directory);
  recording_and_failure(directory);CoUninitialize();std::cout<<"studio graph passed\n";return 0;
 }catch(const std::exception& e){if(SUCCEEDED(com))CoUninitialize();std::cerr<<e.what()<<"\n";return 1;}
}
