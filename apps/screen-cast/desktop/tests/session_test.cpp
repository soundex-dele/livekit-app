#include "services/session.hpp"
#include <iostream>
int main(){screen_cast::Session session(true);
 for(int i=0;i<5;++i){session.start("http://127.0.0.1:1","1234567890",{"fixture","fixture",0,0,1280,720,true});session.stop();auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!session.snapshot().finished&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(10));if(!session.snapshot().finished||session.snapshot().preview){std::cerr<<"Cancellation failed";return 1;}}
 std::cout<<"Repeated start/cancel released workers\n";
}
