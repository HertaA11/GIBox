#include "runtime.hpp"
#include "json.hpp"
#include "logfmt.hpp"
#include "logic_import.hpp"
#include <atomic>
#include <chrono>
#include <jni.h>
#include <link.h>
#include <dlfcn.h>
#include <iterator>
#include <thread>
#include "dobby.h"
#include "handoff_diagnostics.hpp"
#include "vk_capture.hpp"

namespace {
std::atomic<bool> started{false};std::unique_ptr<ms::Runtime> runtime;
std::atomic<ms::Runtime*> published{nullptr};
// Process lifetime: partial hook installation still needs its passthroughs.
std::atomic<ms::handoff::Diagnostics*> handoffDiagnostics{nullptr};
uint64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void onTick(void*,DobbyRegisterContext*){
 if(ms::handoff::probeDepth)return; // avoid Runtime reentry caused by a getter
 if(auto* d=handoffDiagnostics.load(std::memory_order_acquire))d->onTick();
 if(auto* p=published.load(std::memory_order_acquire))p->tick(now());
}
#if !GIBOX_HANDOFF_DIAGNOSTICS
// The game's async model hand-off (abi::receive): attach before its own callback, check after (Runtime::spawned).
using ReceiveFn=void(*)(uintptr_t,uint64_t,bool,uint32_t,uintptr_t,uintptr_t);
ReceiveFn receiveOriginal=nullptr;
void onReceive(uintptr_t self,uint64_t key,bool ok,uint32_t handle,uintptr_t asset,uintptr_t method){
 auto* p=published.load(std::memory_order_acquire);const bool handled=p&&ok&&asset;
 if(handled)p->spawned(asset);
 receiveOriginal(self,key,ok,handle,asset,method);
 if(handled)p->spawnedAfter();
}
// The game's own mesh + bone binding: the mesh argument is swapped before the original runs (Runtime::bindMesh). Up to
// eight integer arguments and x0 are passed through untouched; neither entry takes floating-point arguments.
using BindFn=uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
BindFn lodBindOriginal=nullptr,meshBonesOriginal=nullptr;
uint64_t onLodBind(uintptr_t loader,uintptr_t mesh,uintptr_t renderer,uintptr_t a3,uintptr_t a4,uintptr_t a5,uintptr_t a6,uintptr_t a7){
 if(auto* p=published.load(std::memory_order_acquire))mesh=p->bindMesh(renderer,mesh);
 auto result=lodBindOriginal(loader,mesh,renderer,a3,a4,a5,a6,a7);
 if(auto* p=published.load(std::memory_order_acquire))p->meshBound(renderer);
 return result;}
uint64_t onMeshBones(uintptr_t renderer,uintptr_t mesh,uintptr_t hashes,uintptr_t bones,uintptr_t a4,uintptr_t a5,uintptr_t a6,uintptr_t a7){
 if(auto* p=published.load(std::memory_order_acquire))mesh=p->bindMesh(renderer,mesh);
 auto result=meshBonesOriginal(renderer,mesh,hashes,bones,a4,a5,a6,a7);
 if(auto* p=published.load(std::memory_order_acquire))p->meshBound(renderer);
 return result;}
// The recorded entry bytes (7.1) against this build's: equal, except the immediates that move with the code's address
// (ADRP / ADR pages, B / BL / conditional branch offsets, literal loads, and the low 12 bits of a load / store / add
// off a register other than SP: an ADRP's page offset). 7.2's get_gameObject and resource receiver differ only there.
uint32_t codeMask(uint32_t w){
 if((w&0x9F000000)==0x90000000||(w&0x9F000000)==0x10000000)return 0x9F00001F;
 if((w&0x7C000000)==0x14000000)return 0xFC000000;
 if((w&0xFF000010)==0x54000000||(w&0x7E000000)==0x34000000)return 0xFF00001F;
 if((w&0x7E000000)==0x36000000)return 0xFFF8001F;
 if((w&0x3B000000)==0x18000000)return 0xFF00001F;
 if((w&0x3B000000)==0x39000000&&((w>>5)&31)!=31)return 0xFFC003FF;
 if((w&0x7F800000)==0x11000000&&((w>>5)&31)!=31)return 0xFFC003FF;
 return 0xFFFFFFFF;}
bool codeMatches(uintptr_t base,const ms::abi::CodeCheck& c){auto* p=reinterpret_cast<const unsigned char*>(base+c.rva);auto digit=[](char x){return x<='9'?x-'0':x-'a'+10;};
 for(size_t i=0;i<16;i+=4){uint32_t want=0,got=0;for(size_t b=0;b<4;b++){want|=uint32_t(digit(c.hex[2*(i+b)])*16+digit(c.hex[2*(i+b)+1]))<<(8*b);got|=uint32_t(p[i+b])<<(8*b);}
  uint32_t m=codeMask(want);if((want&m)!=(got&m))return false;}
 return true;}
#endif
struct Image{uintptr_t base=0;const ElfW(Phdr)* ph=nullptr;size_t count=0;};
int findImage(dl_phdr_info* info,size_t,void* ptr){std::string name=info->dlpi_name?info->dlpi_name:"";auto p=name.find_last_of('/');name=name.substr(p==name.npos?0:p+1);if(name!="libyuanshen.so")return 0;auto& out=*static_cast<Image*>(ptr);out={info->dlpi_addr,info->dlpi_phdr,info->dlpi_phnum};return 1;}
std::string jstringValue(JNIEnv* env,jstring s){ms::require(s!=nullptr,"Null Java string");const char* p=env->GetStringUTFChars(s,nullptr);ms::require(p!=nullptr,"Cannot decode Java string");std::string out=p;env->ReleaseStringUTFChars(s,p);return out;}
void exception(JNIEnv* env,const std::exception& e){env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"),e.what());}
std::string sharedDir(const std::string& pkg){return "/storage/emulated/0/Android/media/"+pkg+"/GIBox";}
// Start-up lines go to the same "GIBox Log.txt" as the runtime (logger "bootstrap", XXMI line format).
void bootLog(const std::string& pkg,const std::string& state,const std::string& detail=""){try{auto dir=sharedDir(pkg);auto level=ms::levelOf(state);if(int(level)<ms::logThresholdOf(dir))return;
 std::filesystem::create_directories(dir);ms::appendLog(dir,ms::logLine("bootstrap",level,detail.empty()?state:state+' '+detail));}catch(const std::exception&){}}
void bootStatus(const std::string& pkg,const std::string& state,const std::string& detail){auto dir=sharedDir(pkg);try{std::filesystem::create_directories(dir);auto s="{\"state\":"+ms::quote(state)+",\"detail\":"+ms::quote(detail)+",\"characterId\":0,\"model\":\"\"}";ms::write(dir+"/status.json.tmp",s);std::filesystem::rename(dir+"/status.json.tmp",dir+"/status.json");}catch(const std::exception&){}bootLog(pkg,state,detail);}
}
extern "C" JNIEXPORT void JNICALL Java_com_gibox_tool_NativeBridge_start(JNIEnv* env,jclass,jstring jp,jstring jd){try{
 auto pkg=jstringValue(env,jp),priv=jstringValue(env,jd);ms::require(pkg=="com.miHoYo.Yuanshen"||pkg=="com.miHoYo.ys.bilibili","Unsupported package");if(started.exchange(true))return;
 // The index-buffer hash diagnostic (vk_capture.hpp) hooks the Vulkan loader now, before the game makes its device;
 // only when control.json textureMods.ibHash is true at this start (switching it later takes a restart).
 {bool on=false;try{auto c=ms::JsonParser(ms::text(sharedDir(pkg)+"/control.json")).parse();const auto& t=c.at("textureMods");on=t.type==ms::Json::Object&&t.at("ibHash").type==ms::Json::Bool&&t.at("ibHash").boolean;}catch(const std::exception&){}
  if(on){std::string detail;bool ok=false;try{ok=ms::vkhook::install(detail);}catch(const std::exception& e){detail=e.what();}bootLog(pkg,ok?"vulkan-capture":"vulkan-capture-disabled",ok?"installed: "+detail+" (read-only index-buffer hashes)":detail);}}
 std::thread([pkg,priv]{try{bootStatus(pkg,"waiting","等待 libyuanshen.so");Image image;for(unsigned n=0;n<480&&!image.base;n++){dl_iterate_phdr(findImage,&image);if(!image.base)std::this_thread::sleep_for(std::chrono::milliseconds(500));}ms::require(image.base!=0,"libyuanshen.so load timeout");
 auto actual=ms::loadedBuildId(image.base,image.ph,image.count,sizeof(ElfW(Phdr)));if(actual!=ms::abi::buildId){bootStatus(pkg,"buildid-mismatch","实际 Build ID："+actual);return;}
 // Offsets of this build not found yet (offsets.hpp pendingRequired): nothing is hooked or called.
 if(*ms::abi::pendingRequired){bootStatus(pkg,"offsets-pending",std::string("此版本还缺偏移：")+ms::abi::pendingRequired);return;}
 ms::Engine engine(image.base);
 // Debug glow trace only (glow_trace.hpp): the image's extent, and Dobby as the inline hook installer.
 for(size_t i=0;i<image.count;i++)if(image.ph[i].p_type==PT_LOAD)engine.size=std::max<uintptr_t>(engine.size,image.ph[i].p_vaddr+image.ph[i].p_memsz);
 engine.hook=[base=image.base](uintptr_t rva,void* replacement,void** original){
  return DobbyHook(reinterpret_cast<void*>(base+rva),reinterpret_cast<dobby_dummy_func_t>(replacement),reinterpret_cast<dobby_dummy_func_t*>(original))==0&&*original;};
 // Debug readback only: exported names from this exact image, never a new fixed RVA.
 engine.namedExport=[base=image.base](const char* name)->ms::Ptr{
  void* handle=dlopen("libyuanshen.so",RTLD_NOW|RTLD_NOLOAD);
  void* symbol=dlsym(handle?handle:RTLD_DEFAULT,name);Dl_info owner{};
  bool matches=symbol&&dladdr(symbol,&owner)&&reinterpret_cast<uintptr_t>(owner.dli_fbase)==base;
  if(handle)dlclose(handle);
  return matches?reinterpret_cast<ms::Ptr>(symbol):0;
 };
 auto rt=std::make_unique<ms::Runtime>(std::move(engine),pkg,priv);
 // Publish before Dobby makes Tick executable through its trampoline.
 runtime=std::move(rt);published.store(runtime.get(),std::memory_order_release);
 int result=DobbyInstrument(reinterpret_cast<void*>(image.base+ms::abi::tick),onTick);
 if(result!=0){published.store(nullptr,std::memory_order_release);runtime.reset();throw std::runtime_error("DobbyInstrument failed: "+std::to_string(result));}bootStatus(pkg,"ready","模型运行时已就绪");
 #if GIBOX_HANDOFF_DIAGNOSTICS
 // A diagnostic failure leaves the existing replacement runtime running.
 try{auto* d=new ms::handoff::Diagnostics(ms::Engine(image.base),sharedDir(pkg)+"/handoff-"+std::to_string(::getpid())+".jsonl");
  handoffDiagnostics.store(d,std::memory_order_release);std::string error;
  if(!d->start(error))bootStatus(pkg,"handoff-disabled",error);
 }catch(const std::exception& e){bootStatus(pkg,"handoff-disabled",e.what());}
 #else
 // Replacement at the hand-off; without it the scan still replaces, just not before activation.
 {std::string bad;for(auto& c:ms::abi::handoffChecks)if(!codeMatches(image.base,c)){char b[24];std::snprintf(b,sizeof(b),"0x%llx ",static_cast<unsigned long long>(c.rva));bad+=b;}
  if(!bad.empty())bootLog(pkg,"spawn-hook-disabled","code mismatch at "+bad);
  else if(DobbyHook(reinterpret_cast<void*>(image.base+ms::abi::receive),reinterpret_cast<dobby_dummy_func_t>(onReceive),reinterpret_cast<dobby_dummy_func_t*>(&receiveOriginal))!=0||!receiveOriginal)bootLog(pkg,"spawn-hook-disabled","DobbyHook failed");
  else bootLog(pkg,"spawn-hook","installed");}
 // Mesh + bone binding: each entry is hooked only when its own code bytes match.
 for(size_t i=0;i<std::size(ms::abi::bindChecks);i++){auto& c=ms::abi::bindChecks[i];const bool lod=i==0;const char* what=lod?"lod-bind-hook":"mesh-bones-hook";
  if(!ms::abi::found(c.rva)){bootLog(pkg,std::string(what)+"-disabled","offset not found yet for this build");continue;}
  if(!codeMatches(image.base,c)){bootLog(pkg,std::string(what)+"-disabled","code mismatch");continue;}
  auto replacement=lod?reinterpret_cast<dobby_dummy_func_t>(onLodBind):reinterpret_cast<dobby_dummy_func_t>(onMeshBones);auto* original=reinterpret_cast<dobby_dummy_func_t*>(lod?&lodBindOriginal:&meshBonesOriginal);
  if(DobbyHook(reinterpret_cast<void*>(image.base+c.rva),replacement,original)!=0||!*original)bootLog(pkg,std::string(what)+"-disabled","DobbyHook failed");else bootLog(pkg,what,"installed");}
 #endif
 }catch(const std::exception& e){bootStatus(pkg,"error",e.what());}}).detach();
 }catch(const std::exception& e){exception(env,e);} }
// The converter's 3DMigoto backend: what the INI draws (logic_import.hpp drawsJson), UTF-8 JSON in and out.
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_gibox_tool_NativeBridge_drawsUtf8(JNIEnv* env,jclass,jbyteArray input){try{ms::require(input!=nullptr,"Null draws request");auto n=env->GetArrayLength(input);ms::require(n>0&&n<=16*1024*1024,"Draws request size");
 std::string request(size_t(n),'\0');env->GetByteArrayRegion(input,0,n,reinterpret_cast<jbyte*>(request.data()));if(env->ExceptionCheck())return nullptr;auto out=ms::drawsJson(request);
 auto a=env->NewByteArray(jsize(out.size()));if(!a)return nullptr;env->SetByteArrayRegion(a,0,jsize(out.size()),reinterpret_cast<const jbyte*>(out.data()));return a;}catch(const std::exception& e){exception(env,e);return nullptr;}}
// The converter's Present rehearsal (logic_import.hpp): UTF-8 JSON in, UTF-8 JSON out.
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_gibox_tool_NativeBridge_rehearseUtf8(JNIEnv* env,jclass,jbyteArray input){try{ms::require(input!=nullptr,"Null rehearsal request");auto n=env->GetArrayLength(input);ms::require(n>0&&n<=16*1024*1024,"Rehearsal request size");
 std::string request(size_t(n),'\0');env->GetByteArrayRegion(input,0,n,reinterpret_cast<jbyte*>(request.data()));if(env->ExceptionCheck())return nullptr;auto out=ms::rehearseJson(request);
 auto a=env->NewByteArray(jsize(out.size()));if(!a)return nullptr;env->SetByteArrayRegion(a,0,jsize(out.size()),reinterpret_cast<const jbyte*>(out.data()));return a;}catch(const std::exception& e){exception(env,e);return nullptr;}}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_gibox_tool_NativeBridge_decodeDds(JNIEnv* env,jclass,jbyteArray input){try{ms::require(input!=nullptr,"Null DDS");auto n=env->GetArrayLength(input);ms::require(n>=128&&n<=256*1024*1024,"DDS input size");ms::Bytes b(n);env->GetByteArrayRegion(input,0,n,reinterpret_cast<jbyte*>(b.data()));if(env->ExceptionCheck())return nullptr;auto t=ms::decodeDds(b);ms::Bytes out(8+t.pixels.size());ms::put32(out.data(),t.w);ms::put32(out.data()+4,t.h);std::memcpy(out.data()+8,t.pixels.data(),t.pixels.size());auto a=env->NewByteArray(out.size());if(!a)return nullptr;env->SetByteArrayRegion(a,0,out.size(),reinterpret_cast<jbyte*>(out.data()));return a;}catch(const std::exception& e){exception(env,e);return nullptr;}}
