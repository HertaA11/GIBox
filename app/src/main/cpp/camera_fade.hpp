#pragma once
#include <atomic>
#include <cstdint>
#include <string>
namespace ms::cameraFade {
enum class Mode { Original, Disabled };
inline std::atomic<Mode> mode{Mode::Original};
// Treat the removed conditional mode as enabled until the user next changes the switch.
inline Mode fromName(const std::string& name){return name=="disabled"||name=="look_up"?Mode::Disabled:Mode::Original;}
inline float adjust(uintptr_t,float requested){return mode.load(std::memory_order_relaxed)==Mode::Disabled?1.f:requested;}
}
