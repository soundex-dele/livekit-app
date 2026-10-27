#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <imgui.h>
#include <av/locale.hpp>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include "features/ui.hpp"
#include "services/session.hpp"
#include <iostream>
int main(){
 SDL_SetMainReady();SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS,"permonitorv2");
 SDL_Window*window=nullptr;SDL_Renderer*renderer=nullptr;SDL_Texture*texture=nullptr;int tw=0,th=0,result=0;
 try{
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0)throw std::runtime_error(SDL_GetError());
  window=SDL_CreateWindow("局域网传屏",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,880,680,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);if(!window)throw std::runtime_error(SDL_GetError());SDL_SetWindowMinimumSize(window,640,500);
  renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);if(!renderer)throw std::runtime_error(SDL_GetError());
  ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::StyleColorsDark();
  av::ui::load_chinese_font();
  ImGui_ImplSDL2_InitForSDLRenderer(window,renderer);ImGui_ImplSDLRenderer2_Init(renderer);
  {screen_cast::Session session;screen_cast::View view;view.displays=livekit::sdk::screen_capture_devices();bool closing=false;std::string uiError;std::shared_ptr<const livekit::sdk::VideoFrame> last;
   for(;;){SDL_Event e;while(SDL_PollEvent(&e)){ImGui_ImplSDL2_ProcessEvent(&e);if(e.type==SDL_QUIT||(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_CLOSE)){closing=true;session.stop();}}
    auto state=session.snapshot();if(closing&&state.finished)break;if(!uiError.empty())state.error=uiError;
    if(state.preview&&state.preview!=last){auto&f=*state.preview;if(!texture||tw!=f.width||th!=f.height){if(texture)SDL_DestroyTexture(texture);texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_IYUV,SDL_TEXTUREACCESS_STREAMING,f.width,f.height);tw=f.width;th=f.height;if(!texture)throw std::runtime_error(SDL_GetError());}
     if(SDL_UpdateYUVTexture(texture,nullptr,f.y.data(),f.stride_y,f.u.data(),f.stride_u,f.v.data(),f.stride_v)!=0)throw std::runtime_error(SDL_GetError());last=state.preview;
    }
    if(!state.preview){last.reset();if(texture){SDL_DestroyTexture(texture);texture=nullptr;}}
    ImGui_ImplSDLRenderer2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();auto action=view.draw(state,reinterpret_cast<ImTextureID>(texture));
    if(action==screen_cast::Action::Stop)session.stop();
    if(!closing)try{if(action==screen_cast::Action::Refresh){view.displays=livekit::sdk::screen_capture_devices();uiError.clear();}if(action==screen_cast::Action::Start&&!view.displays.empty()){uiError.clear();session.start(view.url,view.code,view.displays[view.selected]);}}catch(const std::exception&e){uiError=e.what();}
    ImGui::Render();SDL_SetRenderDrawColor(renderer,18,22,29,255);SDL_RenderClear(renderer);ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);SDL_Delay(16);
   }
  }
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';result=1;}
 if(texture)SDL_DestroyTexture(texture);if(ImGui::GetCurrentContext()){ImGui_ImplSDLRenderer2_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext();}if(renderer)SDL_DestroyRenderer(renderer);if(window)SDL_DestroyWindow(window);SDL_Quit();return result;
}
