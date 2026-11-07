#pragma once
#include "core.hpp"
#include "offsets.hpp"
#include <functional>
#include <memory>
namespace ms {
using Ptr=uintptr_t;
template<class T>T load(Ptr p){require(p!=0,"Null native pointer");T v;std::memcpy(&v,reinterpret_cast<void*>(p),sizeof(v));return v;}
template<class T>void store(Ptr p,T v){require(p!=0,"Null native pointer");std::memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));}
struct Engine {
 Ptr base;std::function<Ptr(abi::Offset)> resolver;
 explicit Engine(Ptr b):base(b){}
 template<class R,class... A>R call(abi::Offset o,A... a){Ptr p=resolver?resolver(o):base+o;require(p!=0,"Unresolved engine function");return reinterpret_cast<R(*)(A...)>(p)(a...);}
 Ptr get(abi::Offset o,Ptr p){return call<Ptr>(o,p,Ptr(0));}
 void set(abi::Offset o,Ptr p,Ptr v){call<void>(o,p,v,Ptr(0));}
 void op(abi::Offset o,Ptr p,int v){call<void>(o,p,v,Ptr(0));}
 void action(abi::Offset o,Ptr p){call<void>(o,p,Ptr(0));}
};
class Runtime {
 struct Impl;std::unique_ptr<Impl> p;
 public:
 // threaded=false runs all file work inline (host tests); the game uses a background thread so
 // the game thread only makes engine calls.
 Runtime(Engine engine,std::string packageName,std::string privateDir,std::string mediaRoot="/storage/emulated/0/Android/media",bool threaded=true);~Runtime();
 // Calls must be serial, inside the game's Tick, including shutdown().
 void tick(uint64_t now);void shutdown();
 // The game's async model hand-off (abi::receive), before and after its own callback; same thread as tick(), never inside it.
 void spawned(Ptr gameObject)noexcept;void spawnedAfter()noexcept;
 // The game's mesh + bone binding (abi::lodBind, abi::meshBones): the mesh it should bind instead (native pointers).
 Ptr bindMesh(Ptr rendererNative,Ptr meshNative)noexcept;
 // Post-call phase of the same mesh/bone hooks: first-time renderers now have their game skeleton bound.
 void meshBound(Ptr rendererNative)noexcept;
};
std::string loadedBuildId(Ptr base,const void* headers,size_t count,size_t stride);
}
