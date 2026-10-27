#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <imgui.h>
#include <av/locale.hpp>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include "features/ui.hpp"
#include <stdexcept>
#include <iostream>
int main(){try{
  auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
  SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
  auto*w=SDL_CreateWindow("test",0,0,880,680,SDL_WINDOW_HIDDEN);auto*r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);if(!r)throw std::runtime_error(SDL_GetError());
  ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;av::ui::load_chinese_font();ImGui_ImplSDL2_InitForSDLRenderer(w,r);ImGui_ImplSDLRenderer2_Init(r);
  screen_cast::View view;view.displays={{"primary","Main",0,0,1920,1080,true},{"left","Portrait",-1080,0,1080,1920,false}};
  auto* pixels=SDL_CreateRGBSurfaceWithFormat(0,320,180,32,SDL_PIXELFORMAT_ARGB8888);check(pixels!=nullptr,"surface allocation");
  SDL_FillRect(pixels,nullptr,SDL_MapRGB(pixels->format,40,100,160));auto*texture=SDL_CreateTextureFromSurface(r,pixels);SDL_FreeSurface(pixels);check(texture!=nullptr,"preview texture");
  for(const auto*state:{"idle","connecting","paired","streaming","stopping","ended"}){
    screen_cast::Snapshot s;s.state=state;s.finished=s.state=="idle"||s.state=="ended";if(s.state=="ended")s.error="Display removed; refresh and select again";
    if(s.state=="streaming"){auto frame=std::make_shared<livekit::sdk::VideoFrame>();frame->width=1280;frame->height=720;s.preview=frame;}
    ImGui_ImplSDLRenderer2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();view.draw(s,s.preview?reinterpret_cast<ImTextureID>(texture):0);ImGui::Render();SDL_SetRenderDrawColor(r,18,22,29,255);SDL_RenderClear(r);ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),r);
    auto*surface=SDL_CreateRGBSurfaceWithFormat(0,880,680,32,SDL_PIXELFORMAT_ARGB8888);if(SDL_RenderReadPixels(r,nullptr,surface->format->format,surface->pixels,surface->pitch))throw std::runtime_error(SDL_GetError());
    if(SDL_SaveBMP(surface,(std::string("screen-cast-")+state+".bmp").c_str()))throw std::runtime_error(SDL_GetError());SDL_FreeSurface(surface);
    if(ImGui::GetDrawData()->TotalVtxCount<100)throw std::runtime_error("Empty UI");
  }
  SDL_DestroyTexture(texture);ImGui_ImplSDLRenderer2_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();std::cout<<"ImGui states rendered\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
