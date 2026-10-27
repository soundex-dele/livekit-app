#include "services/session.hpp"
#include <iostream>
// Synthetic frames only. Never enumerates or captures the user's displays.
int main(int argc,char**argv){if(argc<3)return 2;screen_cast::Session session(true);session.start(argv[1],argv[2],{"fixture","Synthetic display",0,0,1280,720,true});bool preview=false;
 for(int i=0;i<6000;++i){auto s=session.snapshot();if(s.preview&&!preview){std::cout<<"PREVIEW "<<s.preview->width<<"x"<<s.preview->height<<std::endl;preview=true;}if(s.finished){if(!s.error.empty())std::cerr<<s.error<<std::endl;return s.error.empty()?0:1;}if(argc>3&&i==std::stoi(argv[3])*50)session.stop();std::this_thread::sleep_for(std::chrono::milliseconds(20));}session.stop();return 3;
}
