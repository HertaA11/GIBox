#include "camera_fade.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace ms;
namespace {
unsigned reads=0;Ptr activeCamera=0,activeTransform=0;
cameraFade::Vec direction{0,0,1};
Ptr getTransform(Ptr p,Ptr method){assert(p==activeCamera&&!method);reads++;return activeTransform;}
cameraFade::Vec getForward(Ptr p,Ptr method){assert(p==activeTransform&&!method);reads++;return direction;}
}
int main(){
 Bytes controller(cameraFade::boundCamera+8),camera(32),tr(32);
 Ptr self=reinterpret_cast<Ptr>(controller.data());activeCamera=reinterpret_cast<Ptr>(camera.data());activeTransform=reinterpret_cast<Ptr>(tr.data());
 store<Ptr>(self+cameraFade::boundCamera,activeCamera);store<Ptr>(activeCamera+0x10,1);store<Ptr>(activeTransform+0x10,1);
 cameraFade::transform=getTransform;cameraFade::forward=getForward;
 cameraFade::mode=cameraFade::Mode::Original;assert(cameraFade::adjust(self,.25f)==.25f&&!reads);
 cameraFade::mode=cameraFade::Mode::Disabled;assert(cameraFade::adjust(0,0)==1.f&&!reads);
 cameraFade::mode=cameraFade::Mode::LookUp;
 direction={0,.5f,.866f};assert(cameraFade::adjust(self,.2f)==1.f&&reads==2);
 direction={0,-.5f,.866f};assert(cameraFade::adjust(self,.2f)==.2f);
 direction={0,0,1};assert(cameraFade::adjust(self,.2f)==.2f);
 direction={0,std::numeric_limits<float>::quiet_NaN(),1};assert(cameraFade::adjust(self,.2f)==.2f);
 direction={0,.5f,.866f};store<Ptr>(activeCamera+0x10,0);reads=0;assert(cameraFade::adjust(self,.2f)==.2f&&!reads);
 store<Ptr>(activeCamera+0x10,1);store<Ptr>(activeTransform+0x10,0);assert(cameraFade::adjust(self,.2f)==.2f);
 store<Ptr>(activeTransform+0x10,1);cameraFade::mode=cameraFade::Mode::Original;assert(cameraFade::adjust(self,.2f)==.2f);
 assert(cameraFade::fromName("unknown")==cameraFade::Mode::Original);
 assert(cameraFade::fromName("disabled")==cameraFade::Mode::Disabled&&cameraFade::fromName("look_up")==cameraFade::Mode::LookUp);
 std::cout<<"Camera fade: PASS (original, disabled, pitch, missing camera, invalid direction and restoration)\n";
}
