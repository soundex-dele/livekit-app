#define SDL_MAIN_HANDLED
#define NOMINMAX
#include <windows.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include "ui.hpp"
#include <iostream>
int main(){
  SDL_SetMainReady();SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS,"permonitorv2");
  SDL_Window* window=nullptr;SDL_Renderer* renderer=nullptr;HWND hwnd=nullptr;bool hotkey=false;int result=0;
  try{
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0)throw std::runtime_error(SDL_GetError());
    window=SDL_CreateWindow("AV Remote Assistance",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,660,440,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);if(!window)throw std::runtime_error(SDL_GetError());SDL_SetWindowMinimumSize(window,600,420);
    SDL_SysWMinfo wm{};SDL_VERSION(&wm.version);if(!SDL_GetWindowWMInfo(window,&wm))throw std::runtime_error(SDL_GetError());hwnd=wm.info.win.window;
    hotkey=RegisterHotKey(hwnd,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F12)!=0;if(!hotkey)throw std::runtime_error("Cannot register Ctrl+Alt+F12 emergency stop");SDL_EventState(SDL_SYSWMEVENT,SDL_ENABLE);
    renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);if(!renderer)throw std::runtime_error(SDL_GetError());
    IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window,renderer);ImGui_ImplSDLRenderer2_Init(renderer);
    {remote_app::Session session;remote_app::View view;bool closing=false;
      for(;;){SDL_Event event;while(SDL_PollEvent(&event)){
        ImGui_ImplSDL2_ProcessEvent(&event);
        if(event.type==SDL_SYSWMEVENT&&event.syswm.msg->msg.win.msg==WM_HOTKEY)session.stop();
        if(event.type==SDL_QUIT||(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_CLOSE)){closing=true;session.stop();}
        if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_MINIMIZED&&session.snapshot().state=="approved")session.stop();
      }
      auto state=session.snapshot();if(closing&&state.finished)break;SDL_SetWindowAlwaysOnTop(window,state.state=="approved"?SDL_TRUE:SDL_FALSE);
      ImGui_ImplSDLRenderer2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();auto action=view.draw(state);
      if(!closing){if(action==remote_app::Action::Start)session.start(view.url);if(action==remote_app::Action::Approve)session.approve();if(action==remote_app::Action::Reject)session.reject();}if(action==remote_app::Action::Stop)session.stop();
      ImGui::Render();SDL_SetRenderDrawColor(renderer,18,22,29,255);SDL_RenderClear(renderer);ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);SDL_Delay(16);
    }}
  }catch(const std::exception&e){std::cerr<<e.what()<<'\n';result=1;}
  if(hotkey)UnregisterHotKey(hwnd,1);if(ImGui::GetCurrentContext()){ImGui_ImplSDLRenderer2_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext();}if(renderer)SDL_DestroyRenderer(renderer);if(window)SDL_DestroyWindow(window);SDL_Quit();return result;
}
