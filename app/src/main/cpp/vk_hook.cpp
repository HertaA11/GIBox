// The Vulkan side of the index-buffer hash diagnostic (vk_capture.hpp), off by default. Two exported entry points of
// the system loader libvulkan.so are hooked, vkGetInstanceProcAddr and vkGetDeviceProcAddr: when the game asks for one
// of the buffer functions below, it gets a wrapper that tells the tracker what happened and then calls the loader's
// exported function of that name with the same arguments. No call is skipped or changed; the wrappers only read.
// Installed at the module's start, before the game creates its Vulkan device; a function the game resolved earlier is
// simply not seen.
//
// No inline hook (Dobby) in libvulkan.so: on the 0.7.56 device (Android 16, BTI hardware) the system libraries are
// mapped with branch target checks, so an indirect branch may only land on a "bti" / "paciasp" instruction. Dobby
// relocated the exported stub's direct "b" into the loader's internal GetInstanceProcAddr as "br x17", which lands on a
// function only ever reached by direct branches: SIGILL ILL_ILLOPC at its first instruction, x17 = pc. Here exactly one
// instruction of each stub is rewritten: its direct "b" becomes a direct "b" into a page of ours within 128 MiB, which
// goes on to the wrapper; the original target is reached from that page by a direct "b" too. A stub of another shape is
// left alone and its first words are logged.
#include "vk_capture.hpp"
#include "dobby.h"
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <dlfcn.h>
#include <string>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>

namespace {
// The few Vulkan declarations used (vulkan_core.h; LP64 layout, VKAPI_CALL is empty on arm64).
using VkResult=int32_t;using PFN=void(*)();
struct VkBufferCreateInfo {int32_t sType;const void* pNext;uint32_t flags;uint64_t size;uint32_t usage;int32_t sharingMode;uint32_t queueFamilyIndexCount;const uint32_t* pQueueFamilyIndices;};
static_assert(offsetof(VkBufferCreateInfo,size)==24&&offsetof(VkBufferCreateInfo,usage)==32,"VkBufferCreateInfo layout");
struct VkBindBufferMemoryInfo {int32_t sType;const void* pNext;uint64_t buffer,memory,memoryOffset;};
static_assert(offsetof(VkBindBufferMemoryInfo,buffer)==16&&offsetof(VkBindBufferMemoryInfo,memoryOffset)==32,"VkBindBufferMemoryInfo layout");
static_assert(sizeof(ms::vkcap::Copy)==24,"VkBufferCopy layout");

using GetProc=PFN(*)(void*,const char*);
using CreateBuffer=VkResult(*)(void*,const VkBufferCreateInfo*,const void*,uint64_t*);
using DestroyBuffer=void(*)(void*,uint64_t,const void*);
using BindBufferMemory=VkResult(*)(void*,uint64_t,uint64_t,uint64_t);
using BindBufferMemory2=VkResult(*)(void*,uint32_t,const VkBindBufferMemoryInfo*);
using MapMemory=VkResult(*)(void*,uint64_t,uint64_t,uint64_t,uint32_t,void**);
using UnmapMemory=void(*)(void*,uint64_t);
using FreeMemory=void(*)(void*,uint64_t,const void*);
using CmdCopyBuffer=void(*)(void*,uint64_t,uint64_t,uint32_t,const ms::vkcap::Copy*);
using CmdBindIndexBuffer=void(*)(void*,uint64_t,uint64_t,int32_t);

GetProc instanceProcOriginal=nullptr,deviceProcOriginal=nullptr;
PFN onGetDeviceProcAddr(void* device,const char* name);
std::atomic<CreateBuffer> createBuffer{nullptr};std::atomic<DestroyBuffer> destroyBuffer{nullptr};
std::atomic<BindBufferMemory> bindBufferMemory{nullptr};std::atomic<BindBufferMemory2> bindBufferMemory2{nullptr},bindBufferMemory2KHR{nullptr};
std::atomic<MapMemory> mapMemory{nullptr};std::atomic<UnmapMemory> unmapMemory{nullptr};std::atomic<FreeMemory> freeMemory{nullptr};
std::atomic<CmdCopyBuffer> cmdCopyBuffer{nullptr};std::atomic<CmdBindIndexBuffer> cmdBindIndexBuffer{nullptr};
ms::vkcap::Tracker& T(){return ms::vkcap::tracker();}

VkResult onCreateBuffer(void* d,const VkBufferCreateInfo* info,const void* a,uint64_t* out){VkResult r=createBuffer.load()(d,info,a,out);if(r==0&&info&&out)T().created(*out,info->size,info->usage);return r;}
void onDestroyBuffer(void* d,uint64_t b,const void* a){T().destroyed(b);destroyBuffer.load()(d,b,a);}
VkResult onBindBufferMemory(void* d,uint64_t b,uint64_t m,uint64_t o){VkResult r=bindBufferMemory.load()(d,b,m,o);if(r==0)T().bound(b,m,o);return r;}
VkResult bound2(VkResult r,uint32_t n,const VkBindBufferMemoryInfo* p){if(r==0&&p)for(uint32_t i=0;i<n;i++)T().bound(p[i].buffer,p[i].memory,p[i].memoryOffset);return r;}
VkResult onBindBufferMemory2(void* d,uint32_t n,const VkBindBufferMemoryInfo* p){return bound2(bindBufferMemory2.load()(d,n,p),n,p);}
VkResult onBindBufferMemory2KHR(void* d,uint32_t n,const VkBindBufferMemoryInfo* p){return bound2(bindBufferMemory2KHR.load()(d,n,p),n,p);}
VkResult onMapMemory(void* d,uint64_t m,uint64_t o,uint64_t size,uint32_t flags,void** pp){VkResult r=mapMemory.load()(d,m,o,size,flags,pp);if(r==0&&pp&&*pp)T().mapped(m,o,size,*pp);return r;}
void onUnmapMemory(void* d,uint64_t m){T().unmapped(m);unmapMemory.load()(d,m);}
void onFreeMemory(void* d,uint64_t m,const void* a){T().unmapped(m);freeMemory.load()(d,m,a);}
void onCmdCopyBuffer(void* c,uint64_t src,uint64_t dst,uint32_t n,const ms::vkcap::Copy* r){if(r)T().copied(src,dst,r,n);cmdCopyBuffer.load()(c,src,dst,n,r);}
void onCmdBindIndexBuffer(void* c,uint64_t b,uint64_t o,int32_t type){T().indexBound(b,o);cmdBindIndexBuffer.load()(c,b,o,type);}

// Each wrapper calls the loader's exported function of the same name (its trampoline), which dispatches through the
// handle's own table: right for any device the game makes, a probe device included. A name the loader does not export
// (vkBindBufferMemory2KHR on some loaders) is handed back unwrapped and counted.
template<class F>PFN wrap(std::atomic<F>& slot,PFN real,F replacement){if(!slot.load()){T().resolvedName(false);return real;}T().resolvedName(true);return reinterpret_cast<PFN>(replacement);}
PFN wrapped(const char* name,PFN real){if(!real||!name||!T().enabled)return real;std::string n=name;
 if(n=="vkCreateBuffer")return wrap(createBuffer,real,&onCreateBuffer);
 if(n=="vkDestroyBuffer")return wrap(destroyBuffer,real,&onDestroyBuffer);
 if(n=="vkBindBufferMemory")return wrap(bindBufferMemory,real,&onBindBufferMemory);
 if(n=="vkBindBufferMemory2")return wrap(bindBufferMemory2,real,&onBindBufferMemory2);
 if(n=="vkBindBufferMemory2KHR")return wrap(bindBufferMemory2KHR,real,&onBindBufferMemory2KHR);
 if(n=="vkMapMemory")return wrap(mapMemory,real,&onMapMemory);
 if(n=="vkUnmapMemory")return wrap(unmapMemory,real,&onUnmapMemory);
 if(n=="vkFreeMemory")return wrap(freeMemory,real,&onFreeMemory);
 if(n=="vkCmdCopyBuffer")return wrap(cmdCopyBuffer,real,&onCmdCopyBuffer);
 if(n=="vkCmdBindIndexBuffer")return wrap(cmdBindIndexBuffer,real,&onCmdBindIndexBuffer);
 // vkGetDeviceProcAddr looked up through vkGetInstanceProcAddr is the loader's internal function, not the exported stub:
 // ours instead, which goes through the stub's original target (the same lookup).
 if(n=="vkGetDeviceProcAddr"&&deviceProcOriginal){T().resolvedName(true);return reinterpret_cast<PFN>(&onGetDeviceProcAddr);}
 return real;}
PFN onGetInstanceProcAddr(void* instance,const char* name){return wrapped(name,instanceProcOriginal(instance,name));}
PFN onGetDeviceProcAddr(void* device,const char* name){return wrapped(name,deviceProcOriginal(device,name));}
}

namespace {
// ---------------------------------------------------------------- one-instruction redirect
constexpr uint32_t kB=0x14000000u,kLdrX17=0x58000051u /* ldr x17, #8 */,kBrX17=0xD61F0220u;
bool isB(uint32_t w){return (w&0xFC000000u)==kB;}
bool isLanding(uint32_t w){return w==0xD503245Fu||w==0xD503249Fu||w==0xD50324DFu||w==0xD503233Fu;}   // bti c / j / jc, paciasp
uintptr_t bTarget(uint32_t w,uintptr_t at){int64_t imm=int64_t(w&0x03FFFFFFu);if(imm&0x02000000){imm-=0x04000000;}return uintptr_t(int64_t(at)+imm*4);}
bool bTo(uintptr_t from,uintptr_t to,uint32_t& out){int64_t d=int64_t(to)-int64_t(from);if(d%4||d<-(int64_t(1)<<27)||d>=(int64_t(1)<<27))return false;out=kB|(uint32_t(d/4)&0x03FFFFFFu);return true;}
std::string words(uintptr_t at){char b[64];uint32_t w[3];std::memcpy(w,reinterpret_cast<const void*>(at),12);std::snprintf(b,sizeof b,"%08x %08x %08x",w[0],w[1],w[2]);return b;}
// A page of ours within reach of a direct branch from near (no PROT_BTI: anything may land in it).
uint32_t* islandNear(uintptr_t near){const size_t page=size_t(sysconf(_SC_PAGESIZE));
 for(int64_t mb=2;mb<=96;mb+=2)for(int sign:{-1,1}){uintptr_t hint=(uintptr_t(int64_t(near)+sign*mb*(int64_t(1)<<20)))&~uintptr_t(page-1);
   void* p=mmap(reinterpret_cast<void*>(hint),page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(p==MAP_FAILED)continue;
   int64_t d=int64_t(reinterpret_cast<uintptr_t>(p))-int64_t(near);if(d>-(int64_t(120)<<20)&&d<(int64_t(120)<<20))return static_cast<uint32_t*>(p);munmap(p,page);}
 return nullptr;}
// The stub at `at`: [landing pad] b target. Fills island[0..4]: ldr x17, #8; br x17; .quad hook (where the stub's "b"
// will go), then "b target" (the original, returned). The page must be writable; nothing in the stub is written here:
// `site` and `jump` are the stub's branch and its new encoding, written by patch() once every entry is ready.
struct Redirect {uintptr_t site=0;uint32_t jump=0;void* original=nullptr;std::string what;};
bool prepare(uintptr_t at,void* hook,uint32_t* island,Redirect& r,std::string& why){uint32_t w0,w1;std::memcpy(&w0,reinterpret_cast<const void*>(at),4);std::memcpy(&w1,reinterpret_cast<const void*>(at+4),4);
 uintptr_t site=0;if(isB(w0))site=at;else if(isLanding(w0)&&isB(w1))site=at+4;const std::string before=words(at);
 if(!site){why="不是 [bti] + b 形式的入口，开头为 "+before;return false;}uint32_t insn;std::memcpy(&insn,reinterpret_cast<const void*>(site),4);uintptr_t target=bTarget(insn,site);
 uint32_t back,jump;if(!bTo(reinterpret_cast<uintptr_t>(island+4),target,back)||!bTo(site,reinterpret_cast<uintptr_t>(island),jump)){why="GIBox 的页超出跳转范围";return false;}
 island[0]=kLdrX17;island[1]=kBrX17;uint64_t h=reinterpret_cast<uint64_t>(hook);std::memcpy(island+2,&h,8);island[4]=back;
 char b[128];std::snprintf(b,sizeof b,"入口 %s，+%u 处的 b 改经 0x%lx",before.c_str(),unsigned(site-at),static_cast<unsigned long>(reinterpret_cast<uintptr_t>(island)));
 r.site=site;r.jump=jump;r.original=island+4;r.what=b;return true;}
// Our page executable, then each stub's branch rewritten (4 bytes, one aligned store).
bool patch(uint32_t* island,std::initializer_list<Redirect*> rs,std::string& why){const size_t page=size_t(sysconf(_SC_PAGESIZE));void* p=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(island)&~uintptr_t(page-1));
 if(mprotect(p,page,PROT_READ|PROT_EXEC)!=0){why="GIBox 页的 mprotect 失败";return false;}__builtin___clear_cache(static_cast<char*>(p),static_cast<char*>(p)+page);
 for(auto* r:rs){if(DobbyCodePatch(reinterpret_cast<void*>(r->site),reinterpret_cast<uint8_t*>(&r->jump),4)!=0){why="写入入口跳转失败";return false;}}return true;}
}

namespace ms::vkhook {
// Hooks the loader's two lookup functions; the result and the reason when it cannot are kept in the tracker's stats.
bool install(std::string& detail){
 void* lib=dlopen("libvulkan.so",RTLD_NOW);if(!lib){detail="无法加载 libvulkan.so";vkcap::tracker().installed(false,detail);return false;}
 void* gipa=dlsym(lib,"vkGetInstanceProcAddr");void* gdpa=dlsym(lib,"vkGetDeviceProcAddr");
 if(!gipa||!gdpa){detail="libvulkan.so 没有导出 vkGetInstanceProcAddr / vkGetDeviceProcAddr";vkcap::tracker().installed(false,detail);return false;}
 // The loader's trampolines the wrappers call (see wrap).
 auto get=[lib](auto& slot,const char* name){slot.store(reinterpret_cast<typename std::remove_reference_t<decltype(slot)>::value_type>(dlsym(lib,name)));};
 get(createBuffer,"vkCreateBuffer");get(destroyBuffer,"vkDestroyBuffer");get(bindBufferMemory,"vkBindBufferMemory");get(bindBufferMemory2,"vkBindBufferMemory2");get(bindBufferMemory2KHR,"vkBindBufferMemory2KHR");
 get(mapMemory,"vkMapMemory");get(unmapMemory,"vkUnmapMemory");get(freeMemory,"vkFreeMemory");get(cmdCopyBuffer,"vkCmdCopyBuffer");get(cmdBindIndexBuffer,"vkCmdBindIndexBuffer");
 if(!createBuffer.load()||!bindBufferMemory.load()||!mapMemory.load()||!cmdCopyBuffer.load()||!cmdBindIndexBuffer.load()){detail="libvulkan.so 没有导出缓冲相关函数";vkcap::tracker().installed(false,detail);return false;}
 // Both stubs are checked before either is written: one of another shape leaves libvulkan.so untouched.
 auto shape=[](void* at){uint32_t w0,w1;std::memcpy(&w0,at,4);std::memcpy(&w1,static_cast<const char*>(at)+4,4);return isB(w0)||(isLanding(w0)&&isB(w1));};
 if(!shape(gipa)||!shape(gdpa)){detail="libvulkan.so 的入口不是 [bti] + b 形式（vkGetInstanceProcAddr "+words(reinterpret_cast<uintptr_t>(gipa))+"，vkGetDeviceProcAddr "+words(reinterpret_cast<uintptr_t>(gdpa))+"），未写入";vkcap::tracker().installed(false,detail);return false;}
 uint32_t* island=islandNear(reinterpret_cast<uintptr_t>(gipa));if(!island){detail="在 libvulkan.so 的跳转范围内没有可用的 GIBox 页，未写入";vkcap::tracker().installed(false,detail);return false;}
 Redirect gi,gd;std::string why;
 if(!prepare(reinterpret_cast<uintptr_t>(gipa),reinterpret_cast<void*>(&onGetInstanceProcAddr),island,gi,why)||!prepare(reinterpret_cast<uintptr_t>(gdpa),reinterpret_cast<void*>(&onGetDeviceProcAddr),island+8,gd,why)){
  detail=why+"，未写入";vkcap::tracker().installed(false,detail);return false;}
 instanceProcOriginal=reinterpret_cast<GetProc>(gi.original);deviceProcOriginal=reinterpret_cast<GetProc>(gd.original);
 vkcap::tracker().installed(true,"");   // before the stubs change: a lookup through them already wraps
 if(!patch(island,{&gi,&gd},why)){detail=why;vkcap::tracker().installed(false,detail);return false;}
 detail="vkGetInstanceProcAddr："+gi.what+"；vkGetDeviceProcAddr："+gd.what+"；包装函数调用加载器自己的导出";return true;}
}
