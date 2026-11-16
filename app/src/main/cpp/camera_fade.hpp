#pragma once
#include "runtime.hpp"
#include <atomic>
#include <cmath>
#include <string>

namespace ms::cameraFade {
enum class Mode { Original, Disabled, LookUp };
inline std::atomic<Mode> mode{Mode::Original};
inline Mode fromName(const std::string& name){return name=="disabled"?Mode::Disabled:name=="look_up"?Mode::LookUp:Mode::Original;}
struct Vec {float x,y,z;};
using TransformFn=Ptr(*)(Ptr,Ptr);
using ForwardFn=Vec(*)(Ptr,Ptr);
inline TransformFn transform=nullptr;
inline ForwardFn forward=nullptr;
// CN 7.2 BGMCJLBBGJF.BIDIEDGAIGK: the camera bound to this camera controller.
inline constexpr uintptr_t boundCamera=0xca0;
inline bool configure(uintptr_t base){
 if(!abi::found(abi::at(abi::cameraTransform))||!abi::found(abi::at(abi::cameraForward)))return false;
 transform=reinterpret_cast<TransformFn>(base+abi::at(abi::cameraTransform));
 forward=reinterpret_cast<ForwardFn>(base+abi::at(abi::cameraForward));return true;
}
inline bool live(Ptr p){return p&&load<Ptr>(p+0x10)!=0;}
inline float adjust(Ptr controller,float requested){
 auto selected=mode.load(std::memory_order_relaxed);
 if(selected==Mode::Disabled)return 1.f;
 if(selected!=Mode::LookUp||!controller||!transform||!forward)return requested;
 Ptr camera=load<Ptr>(controller+boundCamera);if(!live(camera))return requested;
 Ptr tr=transform(camera,0);if(!live(tr))return requested;
 Vec direction=forward(tr,0);
 if(!std::isfinite(direction.x)||!std::isfinite(direction.y)||!std::isfinite(direction.z))return requested;
 return direction.y>.01f?1.f:requested;
}
}
