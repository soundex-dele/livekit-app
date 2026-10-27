#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <imgui.h>
#include <av/locale.hpp>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include "features/remote_control/ui.hpp"
#include <stdexcept>
#include <iostream>
int main(){try{
  auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
  unsigned calls=0;
  auto primary=remote_app::register_emergency_hotkey([&](unsigned key,unsigned long&){++calls;check(key==0x79,"Primary hotkey must be F10");return true;});
  check(primary.available()&&calls==1,"Primary registration");
  calls=0;
  auto fallback=remote_app::register_emergency_hotkey([&](unsigned key,unsigned long& error){++calls;error=1409;check(key!=0x7b,"Reserved F12 must never be registered");return key==0x78;});
  check(fallback.available()&&calls==2&&fallback.error==0&&std::string(fallback.label)=="Ctrl + Alt + F9","Conflict fallback");
  calls=0;
  auto unavailable=remote_app::register_emergency_hotkey([&](unsigned,unsigned long& error){++calls;error=1409;return false;});
  check(!unavailable.available()&&calls==2&&unavailable.error==1409,"All shortcuts unavailable must be nonfatal");
  SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
  auto*w=SDL_CreateWindow("test",0,0,760,560,SDL_WINDOW_HIDDEN);auto*r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);if(!r)throw std::runtime_error(SDL_GetError());
  ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;av::ui::load_chinese_font();ImGui_ImplSDL2_InitForSDLRenderer(w,r);ImGui_ImplSDLRenderer2_Init(r);
  remote_app::View view;
  for(const auto*state:{"idle","waiting","pending","approved","ended"}){
    view.hotkey=std::string(state)=="approved"?unavailable:std::string(state)=="pending"?fallback:primary;
    remote_app::Snapshot s;s.state=state;s.code="0123456789";s.name="协助者小明";s.finished=s.state=="idle"||s.state=="ended";
    ImGui_ImplSDLRenderer2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();view.draw(s);ImGui::Render();SDL_SetRenderDrawColor(r,18,22,29,255);SDL_RenderClear(r);ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),r);
    auto*surface=SDL_CreateRGBSurfaceWithFormat(0,760,560,32,SDL_PIXELFORMAT_ARGB8888);if(SDL_RenderReadPixels(r,nullptr,surface->format->format,surface->pixels,surface->pitch))throw std::runtime_error(SDL_GetError());
    if(SDL_SaveBMP(surface,(std::string("remote-host-")+state+".bmp").c_str()))throw std::runtime_error(SDL_GetError());SDL_FreeSurface(surface);
    if(ImGui::GetDrawData()->TotalVtxCount<100)throw std::runtime_error("Empty UI");
  }
  ImGui_ImplSDLRenderer2_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();std::cout<<"ImGui states rendered\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
