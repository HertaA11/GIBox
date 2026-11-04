#include "handoff_diagnostics.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <sys/mman.h>
using namespace ms;
namespace hd=ms::handoff;
namespace {
Ptr image=0,root=0,entity=0,renderer=0,mesh=0,mesh2=0,componentArray=0,materials=0,bones=0,str=0,goClass=0,foreign=0,bare=0;
std::vector<std::unique_ptr<unsigned char[]>> memory;
std::map<uintptr_t,void*> replacements;
std::map<uintptr_t,void*> originals;
bool receiveForeign=false,instantiateBare=false;int installCount=0,failAt=0,reads=0,readyCalls=0,activateCalls=0,meshCalls=0,receiveCalls=0,instanceCalls=0,pins=0,unpins=0;
uint32_t serial=1;bool active=false,throwMesh=false,throwRead=false;
std::set<uint32_t> liveHandles;
Ptr alloc(size_t n){auto p=std::make_unique<unsigned char[]>(n);std::memset(p.get(),0,n);Ptr result=reinterpret_cast<Ptr>(p.get());memory.push_back(std::move(p));return result;}
Ptr object(){Ptr p=alloc(0x80),n=alloc(0x60);store(p+0x10,n);store<uint32_t>(n+8,serial++);return p;}
Ptr array(std::initializer_list<Ptr> values){Ptr a=alloc(0x20+values.size()*8);store<uint64_t>(a+0x18,values.size());size_t i=0;for(auto v:values)store(a+0x20+(i++)*8,v);return a;}
void originalReady(Ptr a,Ptr b){assert(a==entity&&b==0x123);++readyCalls;}
void originalActivate(Ptr a,Ptr b){assert(a==entity&&b==0x123);++activateCalls;active=true;}
void originalMesh(Ptr a,Ptr b,Ptr c){assert(a==renderer);assert(c==0x123||c==0);++meshCalls;if(throwMesh)throw std::runtime_error("mock mesh exception");mesh=b;}
Ptr originalInstantiate(Ptr self,int32_t h,Ptr method){assert(self==0x456&&method==0x123);++instanceCalls;if(h==-99)throw std::runtime_error("mock instance exception");assert(h==-42);return instantiateBare?bare:root;}
void originalReceive(Ptr a,uint64_t key,bool ok,uint32_t handle,Ptr asset,Ptr method){assert(a==entity&&key==0xf123456789abcdefULL&&ok&&handle==0xfedcba98&&asset==(receiveForeign?foreign:root)&&method==0x123);++receiveCalls;}
Ptr getter(Ptr p,Ptr){++reads;if(throwRead)throw std::runtime_error("mock getter exception");if(p==renderer)return mesh;return 0;}
Ptr getName(Ptr,Ptr){++reads;return str;}
Ptr getTransform(Ptr p,Ptr){++reads;assert(p==root);return root;}
Ptr getGO(Ptr p,Ptr){++reads;assert(p==renderer);return root;}
Ptr getMats(Ptr,Ptr){++reads;return materials;}
Ptr getBones(Ptr,Ptr){++reads;return bones;}
Ptr getComponents(Ptr p,Ptr type,uint32_t inactive,Ptr method){++reads;assert((p==root||p==bare)&&type==0xcafe&&inactive==1&&method==0);return p==bare?array({}):componentArray;}
bool getActive(Ptr p,Ptr){++reads;assert(p==root||p==bare);return active;}
bool getEnabled(Ptr p,Ptr){++reads;assert(p==renderer);return true;}
int32_t getFrame(Ptr method){++reads;assert(!method);return 1024;}
uint32_t gcPin(Ptr p,int mode,Ptr method){assert(p&&mode==2&&!method);++pins;auto h=serial++;assert(liveHandles.insert(h).second);return h;}
void gcUnpin(uint32_t h){assert(h&&liveHandles.erase(h)==1);++unpins;}
void meshEntry(Ptr p,Ptr m,Ptr method){reinterpret_cast<void(*)(Ptr,Ptr,Ptr)>(replacements.at(hd::meshBody))(p,m,method);}
Ptr resolve(abi::Offset o){switch(o){
 case abi::getMesh:return reinterpret_cast<Ptr>(getter);case abi::getName:return reinterpret_cast<Ptr>(getName);
 case abi::getMaterials:return reinterpret_cast<Ptr>(getMats);case abi::getBones:return reinterpret_cast<Ptr>(getBones);
 case abi::pin:return reinterpret_cast<Ptr>(gcPin);case abi::unpin:return reinterpret_cast<Ptr>(gcUnpin);
 case abi::setMesh:return reinterpret_cast<Ptr>(meshEntry);
 default:break;}
 if(o==hd::components)return reinterpret_cast<Ptr>(getComponents);
 if(o==hd::transform)return reinterpret_cast<Ptr>(getTransform);
 if(o==hd::gameObject)return reinterpret_cast<Ptr>(getGO);
 if(o==hd::activeSelf||o==hd::activeHierarchy)return reinterpret_cast<Ptr>(getActive);
 if(o==hd::enabled)return reinterpret_cast<Ptr>(getEnabled);
 if(o==hd::frameCount)return reinterpret_cast<Ptr>(getFrame);
 throw std::runtime_error("unexpected Engine call");}
void setup(){abi::liveRoot.store(0x278);abi::liveConfig.store(0x3a4);abi::liveRuntime.store(0x430);   // the fake entity's layout
 image=reinterpret_cast<Ptr>(mmap(nullptr,0x16000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));assert(image!=Ptr(MAP_FAILED));
 for(auto& f:hd::fingerprints())for(size_t i=0;i<16;++i){std::string byte(f.bytes+i*2,2);store<uint8_t>(image+f.rva+i,uint8_t(std::stoul(byte,nullptr,16)));}
 originals[hd::ready]=reinterpret_cast<void*>(originalReady);originals[hd::activate]=reinterpret_cast<void*>(originalActivate);originals[hd::meshBody]=reinterpret_cast<void*>(originalMesh);originals[hd::instantiate]=reinterpret_cast<void*>(originalInstantiate);originals[hd::receive]=reinterpret_cast<void*>(originalReceive);
 goClass=alloc(0x40);foreign=object();store<Ptr>(foreign,alloc(0x40));bare=object();store<Ptr>(bare,goClass);
 root=object();store<Ptr>(root,goClass);renderer=object();mesh=object();mesh2=object();entity=alloc(0x500);store(entity+0x278,root);store<uint32_t>(entity+0x3a4,10000061);store<uint32_t>(entity+0x430,42);
 componentArray=array({renderer});materials=array({object(),object()});bones=array({object(),0,object()});str=alloc(0x40);store<uint32_t>(str+0x10,4);std::memcpy(reinterpret_cast<void*>(str+0x14),u"test",8);
}
}
extern "C" int DobbyHook(void* addr,void* replacement,void** original){++installCount;if(installCount==failAt)return -1;auto o=reinterpret_cast<Ptr>(addr)-image;*original=originals.at(o);replacements[o]=replacement;return 0;}

int main(int argc,char** argv){assert(argc==3);setup();std::string mode=argv[1],error;Engine e(image);e.resolver=resolve;
 {
 hd::Diagnostics d(e,argv[2]);
 if(mode=="mismatch"){store<uint8_t>(image+hd::components,0);assert(!d.start(error));assert(error.find("fingerprint mismatch")!=std::string::npos&&installCount==0);std::cout<<"mismatch PASS\n";return 0;}
 if(mode=="partial"){failAt=3;assert(!d.start(error)&&installCount==3&&!hd::armed.load());reinterpret_cast<void(*)(Ptr,Ptr)>(replacements.at(hd::ready))(entity,0x123);assert(readyCalls==1&&reads==0);std::cout<<"partial passthrough PASS\n";return 0;}
 if(mode=="badlog"){assert(!d.start(error)&&installCount==0&&!hd::armed.load());assert(!error.empty());std::cout<<"bad log blocks hooks PASS\n";return 0;}
 assert(d.start(error)&&installCount==5);d.onTick();
 auto ready=reinterpret_cast<void(*)(Ptr,Ptr)>(replacements.at(hd::ready));
 auto activate=reinterpret_cast<void(*)(Ptr,Ptr)>(replacements.at(hd::activate));
 auto instantiate=reinterpret_cast<Ptr(*)(Ptr,int32_t,Ptr)>(replacements.at(hd::instantiate));
 auto receive=reinterpret_cast<void(*)(Ptr,uint64_t,bool,uint32_t,Ptr,Ptr)>(replacements.at(hd::receive));
 ready(entity,0x123);assert(reads==0); // no confirmed control or Type
 hd::publishControl(false);hd::rendererType.store(0xcafe);assert(!hd::contaminated.load());
 // Before the GameObject class is known, no GameObject getter runs on a receiver asset.
 {int r0=reads;receive(entity,0xf123456789abcdefULL,true,0xfedcba98,root,0x123);assert(reads==r0&&receiveCalls==1);receiveCalls=0;}
 hd::gameObjectClass.store(goClass);
 // A receiver asset of another class gets no GameObject calls; a GameObject without skinned renderers is only counted.
 {int r0=reads;assert(foreign);receiveForeign=true;receive(entity,0xf123456789abcdefULL,true,0xfedcba98,foreign,0x123);receiveForeign=false;assert(reads==r0&&receiveCalls==1);receiveCalls=0;
  instantiateBare=true;assert(instantiate(0x456,-42,0x123)==bare);instantiateBare=false;instanceCalls=0;}
 assert(instantiate(0x456,-42,0x123)==root);assert(instanceCalls==1);
 try{instantiate(0x456,-99,0x123);assert(false);}catch(const std::runtime_error&){}assert(instanceCalls==2);
 receive(entity,0xf123456789abcdefULL,true,0xfedcba98,root,0x123);assert(receiveCalls==1);
 ready(entity,0x123);assert(readyCalls==2&&reads>0&&pins==unpins);
 meshEntry(renderer,mesh2,0x123);activate(entity,0x123);assert(activateCalls==1&&active&&pins==unpins);
 throwRead=true;ready(entity,0x123);throwRead=false;assert(readyCalls==3&&pins==unpins&&liveHandles.empty());
 int before=reads;std::thread t([&]{ready(entity,0x123);});t.join();assert(reads==before&&readyCalls==4);
 hd::publishControl(true);ready(entity,0x123);assert(reads==before&&readyCalls==5);
 hd::publishControl(false);e.set(abi::setMesh,renderer,mesh2);assert(hd::ownWriteDepth==0&&hd::ownAssignments.load()==1&&hd::contaminated.load());
 throwMesh=true;try{e.set(abi::setMesh,renderer,mesh2);assert(false);}catch(const std::runtime_error&){}throwMesh=false;assert(hd::ownWriteDepth==0);
 {hd::ProbeScope probe;ready(entity,0x123);}assert(reads==before&&readyCalls==6&&hd::probeDepth==0);
 std::thread conflict([&]{d.onTick();});conflict.join();ready(entity,0x123);assert(reads==before&&readyCalls==7);
 assert(meshCalls==3&&pins==unpins&&liveHandles.empty());hd::disarm();
 }
 std::cout<<"handoff forwards/gates/GC/TLS/class guard PASS\n";
}
