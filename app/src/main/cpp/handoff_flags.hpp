#pragma once
#include <atomic>
#include <cstdint>

// -DGIBOX_HANDOFF_DIAGNOSTICS=1 builds the hand-off diagnostics (handoff_diagnostics_zh.md). Off by default since
// 0.3.5: they hook the same hand-off callback that replacement now uses, and need replacement switched off.
#ifndef GIBOX_HANDOFF_DIAGNOSTICS
#define GIBOX_HANDOFF_DIAGNOSTICS 0
#endif
namespace ms::handoff {
inline std::atomic<bool> armed{false}, replacementEnabled{true}, controlKnown{false};
inline std::atomic<bool> contaminated{false};
inline std::atomic<uintptr_t> rendererType{0};
// Il2CppClass of UnityEngine.GameObject, read from a world renderer's gameObject when the Type is seeded.
// GameObject getters are only called on objects of exactly this class.
inline std::atomic<uintptr_t> gameObjectClass{0};
// Component.get_gameObject; Diagnostics::preflight checks its fingerprint before armed, and canSeed() needs armed.
inline constexpr uintptr_t componentGameObjectRva=0x80e5b50;
inline std::atomic<uint64_t> ownAssignments{0};
inline thread_local unsigned ownWriteDepth=0, probeDepth=0;
struct OwnWriteScope {
 bool on;
 explicit OwnWriteScope(bool value):on(value){if(on){++ownWriteDepth;ownAssignments.fetch_add(1,std::memory_order_relaxed);contaminated.store(true);}}
 ~OwnWriteScope(){if(on)--ownWriteDepth;}
 OwnWriteScope(const OwnWriteScope&)=delete;
};
struct ProbeScope {ProbeScope(){++probeDepth;}~ProbeScope(){--probeDepth;}ProbeScope(const ProbeScope&)=delete;};
inline void publishControl(bool enabled){
 replacementEnabled.store(enabled,std::memory_order_release);
 controlKnown.store(true,std::memory_order_release);
 if(enabled)contaminated.store(true,std::memory_order_release);
}
inline bool canSeed(){return GIBOX_HANDOFF_DIAGNOSTICS&&armed.load(std::memory_order_acquire)&&controlKnown.load(std::memory_order_acquire)&&!replacementEnabled.load(std::memory_order_acquire)&&!rendererType.load(std::memory_order_acquire);}
inline void disarm(){armed.store(false,std::memory_order_release);rendererType.store(0,std::memory_order_release);gameObjectClass.store(0,std::memory_order_release);}
}
