// Glow trace (glow_trace.hpp) without a game: entry resolution, the hooks' filters and depth rules, what the registry
// derives from the game's own calls, and the summary lines a window produces.
#include "glow_trace.hpp"
#include <cassert>
#include <iostream>
#include <sys/mman.h>
#include <thread>
using namespace ms;using namespace ms::glow;
namespace {
std::vector<void*> blocks;
Ptr alloc(size_t n){void* p=std::calloc(1,n);assert(p);blocks.push_back(p);return reinterpret_cast<Ptr>(p);}
Ptr object(){Ptr p=alloc(0x40);store<Ptr>(p+0x10,alloc(16));return p;}
Ptr managedString(const std::string& s){Ptr p=alloc(0x14+2*s.size()+2);store<int32_t>(p+0x10,int32_t(s.size()));for(size_t i=0;i<s.size();i++)store<uint16_t>(p+0x14+2*i,uint16_t(s[i]));return p;}
Ptr array(std::vector<Ptr> v){Ptr p=alloc(0x20+8*v.size());store<uint64_t>(p+0x18,v.size());for(size_t i=0;i<v.size();i++)store<Ptr>(p+0x20+8*i,v[i]);return p;}
size_t at(const char* name){for(size_t i=0;i<kCount;i++)if(std::string(kHooks[i].name)==name)return i;std::cerr<<"no hook "<<name<<"\n";std::abort();}
template<class F>F hook(const char* name){return reinterpret_cast<F>(replacements()[at(name)]);}
void setOriginal(const char* name,void* f){trace().originals[at(name)]=f;}
std::map<Ptr,std::array<float,4>> colors;std::map<Ptr,float> floats;std::atomic<unsigned> colorWrites{0};std::mutex fakeLock;   // the fakes run on test threads too
void colorImpl(Ptr s,int32_t,const float* v,Ptr){colorWrites++;std::array<float,4> c;std::memcpy(c.data(),v,16);std::lock_guard<std::mutex> l(fakeLock);colors[s]=c;}
// The public overload is an inlined copy in the game; here it reaches the Impl hook, which must not record again.
void colorPublic(Ptr s,int32_t id,V4 v,Ptr mi){hook<void(*)(Ptr,int32_t,const float*,Ptr)>("Material.INTERNAL_CALL_SetColorImpl(Material,int,Color*)")(s,id,&v.x,mi);}
void floatImpl(Ptr s,int32_t,float v,Ptr){floats[s]=v;}
int32_t propertyToId(Ptr,Ptr){return 7;}
int32_t keywordToId(Ptr,Ptr){return 3;}
void materials(Ptr,Ptr,Ptr){}
void nothing2(Ptr,Ptr,Ptr){}
void proxyCtor(Ptr self,Ptr m,Ptr r,int32_t slot,Ptr){store<Ptr>(self+0x18,r);store<int32_t>(self+0x20,slot);store<Ptr>(self+0x28,m);}
// GGCDDCOCKLG.SetFloat writes to the material it holds (+0x30 here): the proxy call and the engine write both count.
void proxyFloat(Ptr self,int32_t id,float v,Ptr mi){hook<void(*)(Ptr,int32_t,float,Ptr)>("Material.SetFloatImpl(int,float)")(load<Ptr>(self+0x30),id,v,mi);}
void groupFloat(Ptr,int32_t,float,Ptr){}
void keywordImpl(Ptr,Ptr,Ptr){}
int32_t frame(){return 4321;}
bool has(const std::vector<std::string>& lines,const std::string& a,const std::string& b=""){for(auto& l:lines)if(l.find(a)!=std::string::npos&&l.find(b)!=std::string::npos)return true;return false;}
}
int main(){
 // 1. Entry resolution: a 4-byte entry is followed through its B (also backwards), never patched in place.
 {uint8_t image[0x100]={};uint32_t b=0x14000000u|((0x80-0x10)/4);std::memcpy(image+0x10,&b,4);uint32_t back=0x14000000u|(0x03FFFFFFu&uint32_t(-(0x80-0x40)/4));std::memcpy(image+0x80,&back,4);
  uintptr_t target=0;std::string how;Hook h{0x10,4,Shape::None,Op::SetFloat,Scope::Material,"t"};assert(resolveEntry(image,sizeof image,h,target,how)&&target==0x40&&how=="B 0x80 B 0x40");
  Hook shortEntry{0x20,4,Shape::None,Op::SetFloat,Scope::Material,"t"};assert(!resolveEntry(image,sizeof image,shortEntry,target,how)&&how=="4-byte entry is not a branch");
  Hook direct{0x20,0x20,Shape::None,Op::SetFloat,Scope::Material,"t"};assert(resolveEntry(image,sizeof image,direct,target,how)&&target==0x20&&how=="direct");
  Hook outside{0xf8,0x20,Shape::None,Op::SetFloat,Scope::Material,"t"};assert(!resolveEntry(image,sizeof image,outside,target,how));}
 // 2. Install against a fake image: the B entries planted here are followed, the rest of the 4-byte entries skipped.
 const size_t imageSize=0x16000000;void* mapped=mmap(nullptr,imageSize,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);assert(mapped!=MAP_FAILED);
 Ptr base=reinterpret_cast<Ptr>(mapped);auto plant=[&](const char* name,uintptr_t to){uintptr_t from=kHooks[at(name)].rva;uint32_t insn=0x14000000u|(uint32_t((int64_t(to)-int64_t(from))/4)&0x03FFFFFFu);std::memcpy(reinterpret_cast<void*>(base+from),&insn,4);};
 plant("Material.EnableKeywordImpl(string)",0xc000000);plant("Material.EnableKeyword(string)",0xc000000);    // both reach the same native code (a B reaches 128 MiB)
 Engine e(base);e.size=imageSize;std::map<uintptr_t,void*> hooked;static int dummy;
 e.hook=[&](uintptr_t rva,void* replacement,void** original){hooked[rva]=replacement;*original=&dummy;return true;};
 e.resolver=[](abi::Offset o)->Ptr{assert(o==rvaFrameCount);return reinterpret_cast<Ptr>(&frame);};
 // Every entry in one call; a second call hooks nothing again.
 auto& t=trace();assert(!t.everything);auto report=install(e);for(size_t i=0;i<kCount;i++)assert(t.done[i]);
 assert(t.everything&&install(e).find("0 of 0 entries hooked")==0);
 assert(hooked.count(0xc000000)&&hooked.count(kHooks[at("Material.SetFloatImpl(int,float)")].rva)&&!hooked.count(kHooks[at("Renderer.set_enabled(bool)")].rva));
 assert(report.find("Material.EnableKeyword(string) 0x80e77f0: B 0xc000000, same code as Material.EnableKeywordImpl(string)")!=std::string::npos);
 assert(report.find("Renderer.set_enabled(bool) 0x8f03334: skipped, 4-byte entry is not a branch")!=std::string::npos&&report.find(" entries hooked, 1 share code")!=std::string::npos);
 assert(t.frameFn.load()==&frame);
 setOriginal("Material.INTERNAL_CALL_SetColorImpl(Material,int,Color*)",reinterpret_cast<void*>(&colorImpl));setOriginal("Material.SetColor(int,Color)",reinterpret_cast<void*>(&colorPublic));
 setOriginal("Material.SetFloatImpl(int,float)",reinterpret_cast<void*>(&floatImpl));setOriginal("Shader.PropertyToID(string)",reinterpret_cast<void*>(&propertyToId));
 setOriginal("Shader.ShaderKeywordToID(string)",reinterpret_cast<void*>(&keywordToId));setOriginal("Renderer.set_sharedMaterials(Material[])",reinterpret_cast<void*>(&materials));
 setOriginal("GGCDDCOCKLG..ctor(Material,Renderer,int)",reinterpret_cast<void*>(&proxyCtor));setOriginal("GGCDDCOCKLG.SetFloat(int,float)",reinterpret_cast<void*>(&proxyFloat));
 setOriginal("Renderer.SetRenderersCustomPropertyFloat(Renderer[],int,float)",reinterpret_cast<void*>(&groupFloat));setOriginal("Material.EnableKeywordImpl(string)",reinterpret_cast<void*>(&keywordImpl));
 setOriginal("Material..ctor(Material)",reinterpret_cast<void*>(&nothing2));
 // 3. Nothing is kept before the trace is armed or for objects nobody registered.
 Ptr renderer=object(),original=object(),clone=object(),other=object(),assigned=object(),proxy=object(),copy=object();V4 green{0.2f,0.9f,1,1};
 hook<void(*)(Ptr,int32_t,V4,Ptr)>("Material.SetColor(int,Color)")(clone,7,green,0);assert(colorWrites==1&&t.head==0);
 t.testClockMs=1000;t.armed=true;uint16_t owner=t.owner("Exp","Part0",10000107);
 t.beginRefresh();t.put(renderer,Role::Renderer,owner,-1,"Exp/Part0 renderer Body");t.put(original,Role::Original,owner,0,"Exp/Part0 original slot 0 Mat_Body");
 t.put(clone,Role::Clone,owner,0,"Exp/Part0 clone row 0 Mat_Body(Clone)");t.endRefresh();
 assert(hook<int32_t(*)(Ptr,Ptr)>("Shader.PropertyToID(string)")(managedString("_ElementRimColor"),0)==7&&t.property(7)=="_ElementRimColor");
 hook<int32_t(*)(Ptr,Ptr)>("Shader.ShaderKeywordToID(string)")(managedString("_ELEMENT_ON"),0);assert(t.keyword(3)=="_ELEMENT_ON");
 hook<void(*)(Ptr,int32_t,V4,Ptr)>("Material.SetColor(int,Color)")(other,7,green,0);assert(t.head==0);
 // 4. The public overload and its Impl are one write; four more identical writes only count up.
 for(int i=0;i<5;i++){t.testClockMs=1000+16*uint64_t(i);hook<void(*)(Ptr,int32_t,V4,Ptr)>("Material.SetColor(int,Color)")(clone,7,green,0);}
 assert(t.head==1&&t.ring[1].repeat==4&&colors[clone][1]==0.9f&&t.ring[1].frame==4321&&t.ring[1].own==0&&t.ring[1].caller[0]!=0);
 V4 off{0,0,0,0};t.testClockMs=2500;hook<void(*)(Ptr,int32_t,V4,Ptr)>("Material.SetColor(int,Color)")(original,7,off,0);
 {OwnScope own;hook<void(*)(Ptr,int32_t,float,Ptr)>("Material.SetFloatImpl(int,float)")(clone,7,1.f,0);}
 assert(t.head==3&&t.ring[3].own==1);
 // 5. A material the game puts on a tracked renderer is tracked from then on; so are copies of tracked materials.
 hook<void(*)(Ptr,Ptr,Ptr)>("Renderer.set_sharedMaterials(Material[])")(renderer,array({clone,assigned}),0);
 hook<void(*)(Ptr,int32_t,float,Ptr)>("Material.SetFloatImpl(int,float)")(assigned,7,1.5f,0);assert(floats[assigned]==1.5f);
 hook<void(*)(Ptr,Ptr,Ptr)>("Material..ctor(Material)")(copy,clone,0);
 hook<void(*)(Ptr,int32_t,float,Ptr)>("Material.SetFloatImpl(int,float)")(copy,7,0.25f,0);
 // 6. The game's Material subclass on our renderer: its own call and the engine write it ends in.
 hook<void(*)(Ptr,Ptr,Ptr,int32_t,Ptr)>("GGCDDCOCKLG..ctor(Material,Renderer,int)")(proxy,original,renderer,2,0);store<Ptr>(proxy+0x30,assigned);
 hook<void(*)(Ptr,int32_t,float,Ptr)>("GGCDDCOCKLG.SetFloat(int,float)")(proxy,7,2.f,0);assert(floats[assigned]==2.f);
 hook<void(*)(Ptr,Ptr,Ptr)>("Material.EnableKeywordImpl(string)")(clone,managedString("_ELEMENT_ON"),0);
 hook<void(*)(Ptr,int32_t,float,Ptr)>("Renderer.SetRenderersCustomPropertyFloat(Renderer[],int,float)")(array({other,renderer}),9,0.5f,0);
 // 7. A destroyed object (native pointer cleared) is no longer matched, even at the same address.
 auto before=t.head;store<Ptr>(clone+0x10,0);hook<void(*)(Ptr,int32_t,V4,Ptr)>("Material.SetColor(int,Color)")(clone,7,off,0);assert(t.head==before);store<Ptr>(clone+0x10,alloc(16));
 // 8. The window summary.
 bool truncated=true;auto events=t.collect(0,uint64_t(5000)*1000000,truncated);assert(!truncated);std::string counts;
 auto lines=summarize(t,events,[&](uint64_t tag){return ownerOf(tag)==owner;},uint64_t(1000)*1000000,3,80,counts);

 assert(has(lines,"+0ms f4321 game x5 SetColor _ElementRimColor#7 on Exp/Part0 clone row 0 Mat_Body(Clone): (0.2,0.9,1,1)","(last +64ms f4321) by 0x"));
 assert(has(lines,"+1500ms f4321 game x1 SetColor _ElementRimColor#7 on Exp/Part0 original slot 0 Mat_Body: (0,0,0,0)"));
 assert(has(lines,"GIBox x1 SetFloat _ElementRimColor#7 on Exp/Part0 clone row 0 Mat_Body(Clone): 1 by"));
 assert(has(lines,"SetMaterials on Exp/Part0 renderer Body: slot 1 = assigned slot 1 (SetMaterials) of Exp/Part0 renderer Body"));
 assert(has(lines,"SetFloat _ElementRimColor#7 on assigned slot 1 (SetMaterials) of Exp/Part0 renderer Body: 1.5 -> 2"));
 assert(has(lines,"CreatedFrom on copy slot 0 (CreatedFrom) of Exp/Part0 clone row 0 Mat_Body(Clone): from Exp/Part0 clone row 0")&&has(lines,"SetFloat _ElementRimColor#7 on copy slot 0 (CreatedFrom) of","0.25"));
 assert(has(lines,"ProxyCreated on proxy int 2 (proxy) of Exp/Part0 renderer Body: over Exp/Part0 original slot 0 Mat_Body, int 2, renderer Exp/Part0 renderer Body")&&has(lines,"SetFloat _ElementRimColor#7 on proxy int 2 (proxy) of Exp/Part0 renderer Body: 2"));
 assert(has(lines,"EnableKeyword _ELEMENT_ON on Exp/Part0 clone row 0")&&has(lines,"GroupCustomFloat #9 on Exp/Part0 renderer Body: 0.5"));
 assert(counts=="16 by the game, 1 by GIBox");
 auto ids=t.materialIds();assert(ids.at(7)==(KindColor|KindFloat)&&t.customIds().at(9)==KindFloat);
 // 9. Registry sweep: objects not registered again are dropped; what the game derived stays for two minutes.
 t.beginRefresh();t.put(renderer,Role::Renderer,owner,-1,"Exp/Part0 renderer Body");t.endRefresh();uint64_t tag=0;
 assert(!t.table.find(original,tag)&&t.table.find(assigned,tag)&&roleOf(tag)==Role::Assigned);
 t.testClockMs=1000+130000;t.beginRefresh();t.put(renderer,Role::Renderer,owner,-1,"Exp/Part0 renderer Body");t.endRefresh();assert(!t.table.find(assigned,tag));
 // 10. Hooks on other threads while the registry is refreshed: lookups never lock, writers do.
 {t.testClockMs=0;std::atomic<bool> stop{false};auto colour=hook<void(*)(Ptr,int32_t,V4,Ptr)>("Material.SetColor(int,Color)");
  auto work=[&](Ptr target){for(int i=0;!stop.load()&&i<200000;i++){V4 v{float(i&7),0,0,1};colour(target,7,v,0);}};
  std::thread a(work,clone),b(work,other);
  for(int i=0;i<300;i++){t.beginRefresh();t.put(renderer,Role::Renderer,owner,-1,"Exp/Part0 renderer Body");if(i%2)t.put(clone,Role::Clone,owner,0,"Exp/Part0 clone row 0 Mat_Body(Clone)");t.endRefresh();}
  stop=true;a.join();b.join();bool cut=false;assert(!t.collect(0,~0ull,cut).empty());}
 t.armed=false;munmap(mapped,imageSize);for(void* p:blocks)std::free(p);
 std::cout<<"Glow trace: PASS ("<<t.hooked<<" of "<<kCount<<" entries hooked in the fake image)\n";
}
