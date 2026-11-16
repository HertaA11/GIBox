#include "camera_fade.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace ms;
int main(){
 cameraFade::mode=cameraFade::Mode::Original;
 for(float value:{0.f,.25f,1.f}){assert(cameraFade::adjust(0,value)==value);}
 cameraFade::mode=cameraFade::Mode::Disabled;
 for(float value:{0.f,.25f,1.f,std::numeric_limits<float>::quiet_NaN()}){assert(cameraFade::adjust(0,value)==1.f);}
 cameraFade::mode=cameraFade::Mode::Original;assert(cameraFade::adjust(0,.25f)==.25f);
 assert(cameraFade::fromName("disabled")==cameraFade::Mode::Disabled);
 assert(cameraFade::fromName("look_up")==cameraFade::Mode::Disabled);
 assert(cameraFade::fromName("original")==cameraFade::Mode::Original&&cameraFade::fromName("unknown")==cameraFade::Mode::Original);
 std::cout<<"Camera fade: PASS (on/off, original value restoration, legacy conditional setting migration)\n";
}
