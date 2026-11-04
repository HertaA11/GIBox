#pragma once
#include "runtime.hpp"
#include "dobby.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <sys/syscall.h>
#include <unistd.h>

namespace ms::handoff {
// All offsets and fingerprints belong ONLY to abi::buildId. This is an
// in-process probe: C++ exceptions cannot recover from a native access fault.
inline constexpr abi::Offset components=static_cast<abi::Offset>(0x9577734),
 transform=static_cast<abi::Offset>(0x95777e8), activeSelf=static_cast<abi::Offset>(0x95779fc),
 activeHierarchy=static_cast<abi::Offset>(0x9577a00), enabled=static_cast<abi::Offset>(0x8f03330),
 frameCount=static_cast<abi::Offset>(0x957a238), gameObject=static_cast<abi::Offset>(0x80e5b50),
 ready=static_cast<abi::Offset>(abi::notFound+0x810), activate=static_cast<abi::Offset>(abi::notFound+0x820),   // 7.2: not found yet (7.1 0x8721e24 / 0x8713ec4)
 meshBody=static_cast<abi::Offset>(abi::notFound+0x830),   // 7.2: not found yet (7.1 0x49fc24c)
 instantiate=static_cast<abi::Offset>(0x913a648),
 receive=static_cast<abi::Offset>(0x9564b4c);
inline uint64_t ns(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
inline int tid(){return int(::syscall(SYS_gettid));}
inline std::string address(Ptr p){std::ostringstream s;s<<"0x"<<std::hex<<p;return s.str();}
inline std::string escape(const std::string& v){std::string s="\"";for(unsigned char c:v){if(c=='"'||c=='\\'){s+='\\';s+=char(c);}else if(c<32){const char* h="0123456789abcdef";s+="\\u00";s+=h[c>>4];s+=h[c&15];}else s+=char(c);}return s+'"';}
struct Json {
 std::string s="{";bool first=true;
 void raw(const char* k,const std::string& v){if(!first)s+=',';first=false;s+=escape(k)+":"+v;}
 void str(const char* k,const std::string& v){raw(k,escape(v));}
 void ptr(const char* k,Ptr p){str(k,address(p));}
 void num(const char* k,uint64_t n){raw(k,std::to_string(n));}
 void boolean(const char* k,bool b){raw(k,b?"true":"false");}
 std::string finish()const{return s+"}";}
};
struct Fingerprint {abi::Offset rva;const char* bytes;const char* name;};
inline const std::vector<Fingerprint>& fingerprints(){static const std::vector<Fingerprint> v={
 {components,"e403022ae2031f2a23008052e5031f2a","GetComponentsInChildren(Type,bool)"},
 {transform,"fe0f1ef8f44f01a9000e00b4080840f9","get_transform"},
 {activeSelf,"39f5091725f5091737f5091723f50917","get_activeSelf"},
 {activeHierarchy,"25f5091737f5091723f509173cf50917","get_activeInHierarchy"},
 {enabled,"d5e8f016e1e8f016ede8f016c00000b4","get_enabled"},
 {frameCount,"08bf06d0082940f900d540b9c0035fd6","get_frameCount"},
 {gameObject,"614d1c16604d1c16c00000b4080840f9","get_gameObject"},
 {abi::getMesh,"fe0f1df8f65701a9f44f02a9600e00b4","get_sharedMesh"},
 {abi::getBones,"fe0f1ef8f44f01a9e00200b4140840f9","get_bones"},
 {abi::getMaterials,"fe0f1ef8f44f01a9600200b4140840f9","get_sharedMaterials"},
 {abi::getName,"ff8302d1fe3b00f9f65708a9f44f09a9","get_name"},
 {abi::pin,"e80300aa490080523f1000712030891a","pin"},
 {abi::unpin,"604ebd14007c4093c0035fd6400000b4","unpin"},
 {abi::typeObject,"ffc302d1fd7b05a9fc6f06a9fa6707a9","typeObject"},
 {abi::classFromType,"fe4fbfa908284039f30300aae0031faa","classFromType"},
 {ready,"ff4301d1ea1300fde9a3026dfe1f00f9","entity_ready"},
 {activate,"ff8301d1fe1b00f9f65704a9f44f05a9","entity_activate"},
 {meshBody,"e00000b4080840f9410000b4210840f9","set_sharedMesh_body"},
 {instantiate,"fe0f1ef8f44f01a9684c06f0f303012a","LoadedBundle.Instanite"},
 {receive,"fe4fbfa9288706f0e50304aae403032a","resource_receiver"}};return v;}

class Diagnostics {
 using EntityFn=void(*)(Ptr,Ptr);
 using MeshFn=void(*)(Ptr,Ptr,Ptr);
 using InstantiateFn=Ptr(*)(Ptr,int32_t,Ptr);
 using ReceiveFn=void(*)(Ptr,uint64_t,bool,uint32_t,Ptr,Ptr);
 Engine e;
 const uint64_t captureStart=ns();
 inline static std::atomic<Diagnostics*> instance{nullptr};
 EntityFn readyOriginal=nullptr, activateOriginal=nullptr;
 MeshFn meshOriginal=nullptr;
 InstantiateFn instantiateOriginal=nullptr;
 ReceiveFn receiveOriginal=nullptr;
 std::atomic<int> tickTid{0};std::atomic<bool> tickConflict{false};
 std::atomic<uint64_t> tickCount{0},sequence{0},spawnCount{0},cycleCount{0},setterCount{0},skips{0},dropped{0},writeErrors{0},emptySpawns{0},foreignRoots{0};
 std::mutex qm;std::condition_variable cv;std::deque<std::string> queue;
 bool stopping=false;std::thread writer;std::string path;

 void enqueue(std::string line)noexcept{
  // The writer only holds qm to swap the queue (it waits on cv unlocked), so a blocking lock costs microseconds;
  // try_lock dropped snapshots whenever the swap happened to coincide.
  try{std::unique_lock<std::mutex> l(qm);if(queue.size()>=4096){dropped.fetch_add(1);return;}queue.push_back(std::move(line));l.unlock();cv.notify_one();}catch(...){dropped.fetch_add(1);}
 }
 Json event(const char* kind){Json j;j.str("kind",kind);j.num("capture_start_ns",captureStart);j.num("seq",sequence.fetch_add(1)+1);j.num("mono_ns",ns());j.num("tid",tid());j.num("tick_tid",tickTid.load());j.num("tick_count",tickCount.load());j.boolean("tick_conflict",tickConflict.load());j.boolean("control_known",controlKnown.load());j.boolean("replacement_enabled",replacementEnabled.load());j.boolean("contaminated",contaminated.load());j.num("own_assignments",ownAssignments.load());j.num("dropped",dropped.load());j.num("write_errors",writeErrors.load());j.num("empty_spawns",emptySpawns.load());j.num("foreign_roots",foreignRoots.load());return j;}
 void notice(const char* kind,const std::string& detail)noexcept{try{auto j=event(kind);j.str("detail",detail);enqueue(j.finish());}catch(...){dropped.fetch_add(1);}}
 void writeLoop(std::promise<std::string> status)noexcept{
  bool announced=false;
  auto reply=[&](const std::string& error){status.set_value(error);announced=true;};
  try{std::filesystem::create_directories(std::filesystem::path(path).parent_path());std::ofstream out(path,std::ios::app);if(!out){writeErrors.fetch_add(1);reply("Cannot open handoff log: "+path);return;}reply("");
   for(;;){std::deque<std::string> lines;{std::unique_lock<std::mutex> l(qm);cv.wait(l,[this]{return stopping||!queue.empty();});lines.swap(queue);if(stopping&&lines.empty())break;}
    for(auto& s:lines){out<<s<<'\n';}out.flush();if(!out){writeErrors.fetch_add(1);return;}}
  }catch(const std::exception& ex){writeErrors.fetch_add(1);if(!announced)try{reply(ex.what());}catch(...){}
  }catch(...){writeErrors.fetch_add(1);if(!announced)try{reply("Cannot initialize handoff log");}catch(...){}}
 }
 bool eligible(const char* phase)noexcept{
  if(!armed.load(std::memory_order_acquire)||ownWriteDepth||probeDepth)return false;
  const char* why=nullptr;
  if(!controlKnown.load()||replacementEnabled.load())why="replacement_not_confirmed_off";
  else if(!tickTid.load()||tickConflict.load()||tid()!=tickTid.load())why="not_verified_tick_thread";
  else if(!rendererType.load(std::memory_order_acquire)||!gameObjectClass.load(std::memory_order_acquire))why="renderer_type_not_seeded_enter_world";
  if(why){if(skips.fetch_add(1)<32)try{auto j=event("skip");j.str("phase",phase);j.str("reason",why);enqueue(j.finish());}catch(...){}return false;}
  return true;
 }
 // Pin roots and arrays while doing managed calls. No objects are destroyed here.
 struct Roots {
  Engine& e;std::vector<uint32_t> handles;
  explicit Roots(Engine& engine):e(engine){handles.reserve(256);}
  void add(Ptr p){if(!p)return;auto h=e.call<uint32_t>(abi::pin,p,2,Ptr(0));require(h!=0,"diagnostic pin failed");try{handles.push_back(h);}catch(...){e.call<void>(abi::unpin,h);throw;}}
  ~Roots(){for(auto h:handles)try{e.call<void>(abi::unpin,h);}catch(...){}}
 };
 static Ptr native(Ptr p){return p?load<Ptr>(p+0x10):0;}
 static size_t length(Ptr a,size_t limit){if(!a)return 0;auto n=load<uint64_t>(a+0x18);require(n<=limit,"diagnostic array length bound");return size_t(n);}
 static std::string refs(Ptr a,size_t n){std::string s="[";for(size_t i=0;i<n;++i){if(i)s+=',';s+=escape(address(load<Ptr>(a+0x20+i*8)));}return s+"]";}
 static void utf8(std::string& s,uint32_t c){if(c<0x80)s+=char(c);else if(c<0x800){s+=char(0xc0|(c>>6));s+=char(0x80|(c&63));}else if(c<0x10000){s+=char(0xe0|(c>>12));s+=char(0x80|((c>>6)&63));s+=char(0x80|(c&63));}else{s+=char(0xf0|(c>>18));s+=char(0x80|((c>>12)&63));s+=char(0x80|((c>>6)&63));s+=char(0x80|(c&63));}}
 std::string name(Ptr p,Roots& roots){if(!native(p))return {};Ptr s=e.get(abi::getName,p);if(!s)return {};roots.add(s);size_t n=std::min<size_t>(load<uint32_t>(s+0x10),96);std::string out;
  for(size_t i=0;i<n;++i){uint32_t c=load<uint16_t>(s+0x14+i*2);if(c>=0xd800&&c<=0xdbff&&i+1<n){auto d=load<uint16_t>(s+0x14+(i+1)*2);if(d>=0xdc00&&d<=0xdfff){++i;c=0x10000+((c-0xd800)<<10)+(d-0xdc00);}else c=0xfffd;}else if(c>=0xd800&&c<=0xdfff)c=0xfffd;utf8(out,c);}return out;}
 static void identity(Json& j,const char* key,Ptr p){j.ptr(key,p);std::string k=key;j.ptr((k+"_native").c_str(),native(p));if(native(p))j.num((k+"_native_id").c_str(),load<uint32_t>(native(p)+8));}
 void flags(Json& j,Ptr go){if(!native(go))return;j.boolean("active_self",e.call<bool>(activeSelf,go,Ptr(0)));j.boolean("active_hierarchy",e.call<bool>(activeHierarchy,go,Ptr(0)));}

 // Spawn phases (bundle Instantiate return, async receiver) fire for every object the game makes, effects included:
 // roots without a SkinnedMeshRenderer are only counted, so world noise does not use up the budget. Spawn and
 // entity-cycle phases (ready/activate) have separate budgets.
 void snapshot(const char* phase,Ptr entity,Ptr explicitRoot=0,uint64_t key=0,uint32_t handle=0,bool success=true,bool useExplicitRoot=false)noexcept{
  if(!eligible(phase))return;
  const bool spawn=std::strncmp(phase,"instantiate",11)==0||std::strncmp(phase,"resource",8)==0;
  ProbeScope guard;
  try{Roots roots(e);roots.add(entity);Ptr go=useExplicitRoot?explicitRoot:(explicitRoot?explicitRoot:(entity&&abi::liveRoot.load()?load<Ptr>(entity+abi::liveRoot.load()):0));roots.add(go);
   // 7.2: the entity's root GameObject field (abi::liveRoot) is not identified yet (0): then only the explicit root is
   // snapshot. The receiver's asset is not guaranteed to be a GameObject; GameObject icalls on
   // another type read the wrong native object. Only an exact class match gets those calls.
   Ptr goClass=Ptr(gameObjectClass.load(std::memory_order_acquire));const bool isGo=native(go)&&goClass&&load<Ptr>(go)==goClass;
   Ptr arr=0;size_t count=0;
   if(isGo){arr=e.call<Ptr>(components,go,rendererType.load(std::memory_order_acquire),uint32_t(1),Ptr(0));roots.add(arr);count=length(arr,4096);}
   if(spawn&&count==0){if(native(go)&&!isGo)foreignRoots.fetch_add(1);else emptySpawns.fetch_add(1);return;}
   auto& counter=spawn?spawnCount:cycleCount;uint64_t sample=counter.fetch_add(1)+1;
   if(sample>256){if(sample==257)notice(spawn?"spawn_budget_exhausted":"cycle_budget_exhausted","256 snapshots of this kind; restart process for another capture");return;}
   auto j=event("snapshot");j.str("phase",phase);j.num("sample",sample);j.ptr("entity",entity);{auto c=abi::liveConfig.load(),r=abi::liveRuntime.load();j.num("config_id",entity&&c?load<uint32_t>(entity+c):0);j.num("runtime_id",entity&&r?load<uint32_t>(entity+r):0);}j.ptr("resource_key",key);j.num("handle",handle);j.boolean("success",success);
   j.num("frame",uint32_t(e.call<int32_t>(frameCount,Ptr(0))));identity(j,"root",go);
   if(native(go)&&!isGo){foreignRoots.fetch_add(1);j.boolean("root_is_game_object",false);j.ptr("root_class",load<Ptr>(go));j.raw("renderers","[]");enqueue(j.finish());return;}
   flags(j,go);
   if(!native(go)){j.raw("renderers","[]");enqueue(j.finish());return;}
   j.str("root_name",name(go,roots));Ptr t=e.get(transform,go);roots.add(t);j.ptr("transform",t);
   size_t n=std::min<size_t>(count,32);j.num("renderer_count",count);j.boolean("renderers_truncated",n<count);
   std::string renderers="[";
   for(size_t i=0;i<n;++i){if(i)renderers+=',';Json r;Ptr p=load<Ptr>(arr+0x20+i*8);identity(r,"renderer",p);if(!native(p)){renderers+=r.finish();continue;}
    r.str("name",name(p,roots));r.boolean("enabled",e.call<bool>(enabled,p,Ptr(0)));Ptr obj=e.get(gameObject,p);roots.add(obj);r.ptr("game_object",obj);flags(r,obj);
    Ptr mesh=e.get(abi::getMesh,p);roots.add(mesh);identity(r,"mesh",mesh);if(native(mesh))r.str("mesh_name",name(mesh,roots));
    Ptr mats=e.get(abi::getMaterials,p);roots.add(mats);size_t nm=length(mats,256);r.num("material_count",nm);r.raw("materials",refs(mats,std::min<size_t>(nm,16)));r.boolean("materials_truncated",nm>16);
    Ptr bones=e.get(abi::getBones,p);roots.add(bones);size_t nb=length(bones,2048),present=0;for(size_t b=0;b<nb;++b)if(load<Ptr>(bones+0x20+b*8))++present;
    r.num("bone_count",nb);r.num("bones_present",present);r.raw("bone_sample",refs(bones,std::min<size_t>(nb,8)));renderers+=r.finish();
   }
   j.raw("renderers",renderers+"]");enqueue(j.finish());
  }catch(const std::exception& ex){notice("probe_error",std::string(phase)+": "+ex.what());}catch(...){notice("probe_error",phase);}
 }
 uint64_t meshEvent(const char* phase,Ptr r,Ptr m,Ptr caller,uint64_t call=0)noexcept{
  if(!armed.load(std::memory_order_acquire)||probeDepth)return 0;
  try{if(!call){call=setterCount.fetch_add(1)+1;if(call>2048){if(call==2049)notice("setter_budget_exhausted","2048 setter calls");return 0;}}
   auto j=event("mesh_setter");j.str("phase",phase);j.num("call",call);j.ptr("renderer",r);j.ptr("mesh_argument",m);j.ptr("caller",caller);j.str("source",ownWriteDepth?"gibox":"external");enqueue(j.finish());return call;
  }catch(...){dropped.fetch_add(1);return 0;}
 }
 static void onReady(Ptr entity,Ptr method){auto* p=instance.load(std::memory_order_acquire);p->snapshot("ready_enter",entity);p->readyOriginal(entity,method);p->snapshot("ready_leave",entity);}
 static void onActivate(Ptr entity,Ptr method){auto* p=instance.load(std::memory_order_acquire);p->snapshot("activate_enter",entity);p->activateOriginal(entity,method);p->snapshot("activate_leave",entity);}
 static void onMesh(Ptr r,Ptr m,Ptr method){auto* p=instance.load(std::memory_order_acquire);Ptr caller=reinterpret_cast<Ptr>(__builtin_extract_return_addr(__builtin_return_address(0)));auto c=p->meshEvent("enter",r,m,caller);p->meshOriginal(r,m,method);if(c)p->meshEvent("return",r,m,caller,c);}
 static Ptr onInstantiate(Ptr self,int32_t hash,Ptr method){auto* p=instance.load(std::memory_order_acquire);Ptr result=p->instantiateOriginal(self,hash,method);p->snapshot("instantiate_return",0,result,uint32_t(hash));return result;}
 static void onReceive(Ptr entity,uint64_t key,bool ok,uint32_t handle,Ptr asset,Ptr method){auto* p=instance.load(std::memory_order_acquire);if(ok)p->snapshot("resource_receive_enter",entity,asset,key,handle,ok,true);p->receiveOriginal(entity,key,ok,handle,asset,method);}
 template<class T>bool hook(abi::Offset o,T replacement,T& original){return DobbyHook(reinterpret_cast<void*>(e.base+o),reinterpret_cast<void*>(replacement),reinterpret_cast<void**>(&original))==0&&original;}
public:
 explicit Diagnostics(Engine engine,std::string filename):e(std::move(engine)),path(std::move(filename)){}
 // In production retain this object and its module until process exit. Dobby
 // trampolines reference it even if a later hook install failed.
 ~Diagnostics(){if(writer.joinable()){{std::lock_guard<std::mutex> l(qm);stopping=true;}cv.notify_one();writer.join();}}
 Diagnostics(const Diagnostics&)=delete;
 std::string preflight()const{
  for(const auto& f:fingerprints())if(!abi::found(f.rva))return std::string(f.name)+": offset not found yet for this build";
  for(const auto& f:fingerprints()){auto* p=reinterpret_cast<const unsigned char*>(e.base+f.rva);for(size_t i=0;i<16;++i){auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};if(p[i]!=unsigned(digit(f.bytes[i*2])*16+digit(f.bytes[i*2+1])))return std::string(f.name)+" @ "+address(f.rva)+" fingerprint mismatch";}}
  return {};
 }
 bool start(std::string& error){
  error=preflight();if(!error.empty())return false;
  Diagnostics* expected=nullptr;if(!instance.compare_exchange_strong(expected,this)){error="diagnostics already installed";return false;}
  // Start filesystem work only on this dedicated worker; game hooks enqueue.
  std::promise<std::string> status;auto opened=status.get_future();
  writer=std::thread([this,status=std::move(status)]()mutable{writeLoop(std::move(status));});
  if(opened.wait_for(std::chrono::seconds(5))!=std::future_status::ready){error="handoff log open timeout; no diagnostic hooks installed";return false;}
  error=opened.get();if(!error.empty())return false;
  if(!hook(ready,onReady,readyOriginal)||!hook(activate,onActivate,activateOriginal)||!hook(meshBody,onMesh,meshOriginal)||!hook(instantiate,onInstantiate,instantiateOriginal)||!hook(receive,onReceive,receiveOriginal)){
   error="DobbyHook failed: diagnostics unarmed; retain passthrough hooks until restart";notice("install_failed",error);return false;
  }
  auto j=event("meta");j.num("schema",1);j.str("build_id",abi::buildId);j.num("pid",::getpid());j.ptr("base",e.base);j.boolean("first_draw_proven",false);j.str("scope","handoff snapshots; sync instantiate and async resource receiver; no render fence");enqueue(j.finish());
  armed.store(true,std::memory_order_release);return true;
 }
 void onTick()noexcept{
  int current=tid(),expected=0;tickTid.compare_exchange_strong(expected,current);if(tickTid.load()!=current)tickConflict.store(true);auto n=tickCount.fetch_add(1)+1;
  if(n==1)notice("tick_thread","raw onTick callback; count is NOT Time.frameCount");
  if(n%600==0&&armed.load())notice("diagnostic_status",rendererType.load()?"renderer_type_ready":"enter world while replacement is off to seed renderer Type");
 }
};
}
