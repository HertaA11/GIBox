// Fake managed objects exercise runtime state transitions without a game process.
#include "runtime.hpp"
#include "vk_capture.hpp"
#include "glow_trace.hpp"
#include "texture_diag.hpp"
#include "json.hpp"
#include "model_fixture.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <sys/mman.h>
#include <thread>
using namespace ms;
using fixture::Spec;using fixture::writeModel;using fixture::learnedAt;
namespace {
struct Obj {std::string kind,name;Ptr mesh=0,materials=0,bones=0,native=0,shared=0,parent=0;std::map<abi::Offset,Ptr> arrays;};
struct Fake {
 Ptr stringBase=0x7020000000ull,base=0,manager=0,owner=0,klass=0,typeObj=0;std::map<Ptr,Obj> obj;std::vector<void*> allocated;std::map<unsigned,Ptr> handles;unsigned nextHandle=1,nextId=1;
 // Each built part now clones one restoration snapshot and one mod mesh; count both engine allocations.
 unsigned clones=0,meshClones=0,textures=0,weightAllocs=0,typeLookups=0,findCalls=0,findAllCalls=0,childCalls=0,bindposeReads=0;std::map<Ptr,Ptr> goOfRenderer;std::map<Ptr,std::vector<Ptr>> goChildren;Ptr lastWeights=0;std::map<Ptr,std::string> strings;std::string lightProperty;bool failMaterials=false,failFind=false,failPosition=false;
 unsigned expectedVertices=3;std::map<Ptr,unsigned> textureBytes;
 std::map<Ptr,Ptr> diffuse;std::map<Ptr,std::map<int32_t,Ptr>> named;std::map<Ptr,float> glow;std::map<Ptr,std::map<int32_t,std::array<float,4>>> props;std::map<Ptr,std::vector<std::string>> keywords; // material properties copied by Instantiate, including copies made by the game
 std::map<Ptr,unsigned> nameReads,boneReads; // actual engine getter counts, used to check binding hot-path work
 std::vector<Ptr> scene,assets;/* assets: only in the everything-loaded search (prefabs, inactive pooled instances) */std::vector<std::pair<Ptr,unsigned>> triangles;std::map<std::pair<Ptr,int>,std::vector<uint32_t>> submeshIndices;Ptr skeletonRoot=0,kavehRoot=0;std::map<std::string,Ptr> kinds;
 // Il2CppClass per object kind, laid out unlike stock il2cpp: byval_arg at +0x98 with a type enum that is not the stock
 // 0x12, this_arg (byref bit 0x20) at +0x40, not next to it. Decoys: a pair whose data is the same in every class, and
 // a pair present only in the renderer class.
 Ptr kindClass(const std::string& k){auto it=kinds.find(k);if(it!=kinds.end())return it->second;Ptr c=alloc(0x200);uint64_t id=0x1234+kinds.size();
  for(unsigned off:{0x98u,0x40u}){store<uint64_t>(c+off,id);store<uint16_t>(c+off+8,0x0006);store<uint8_t>(c+off+0xa,0x1c);store<uint8_t>(c+off+0xb,off==0x40?0x21:0x01);}
  for(unsigned off:{0x150u,0x160u}){store<uint64_t>(c+off,0x77);store<uint8_t>(c+off+0xa,0x1c);store<uint8_t>(c+off+0xb,off==0x160?0x20:0);}
  if(k=="renderer")for(unsigned off:{0x110u,0x120u}){store<uint64_t>(c+off,0x99);store<uint8_t>(c+off+0xa,0x05);store<uint8_t>(c+off+0xb,off==0x120?0x40:0);}
  return kinds[k]=c;}
 // Managed array of one element. Its class's byval_arg points either at the element class's byval_arg (stock il2cpp)
 // or at a separate metadata copy of it (so only the this_arg pair can locate byval_arg); plus a decoy pointer.
 std::vector<std::pair<Ptr,std::string>> typedClasses;bool pointerArrays=false;
 void linkArray(Ptr cls,const std::string& kind){Ptr target=kindClass(kind)+0x98;if(!pointerArrays){Ptr copy=alloc(16);std::memcpy(reinterpret_cast<void*>(copy),reinterpret_cast<void*>(target),16);target=copy;}
  store<Ptr>(cls+0x98,target);store<uint8_t>(cls+0xa2,0x1d);store<Ptr>(cls+0x48,kindClass(kind)+0x30);}
 void linkArrays(bool pointers){pointerArrays=pointers;for(auto& t:typedClasses)linkArray(t.first,t.second);}
 void breakPairs(){for(auto& kv:kinds)std::memset(reinterpret_cast<void*>(kv.second+0x40),0,16);}
 Ptr typed(const std::string& kind,Ptr element){Ptr a=array(8,1,&element),cls=load<Ptr>(a);typedClasses.push_back({cls,kind});linkArray(cls,kind);return a;}
 Ptr alloc(size_t n){auto p=std::calloc(1,n);assert(p);allocated.push_back(p);return reinterpret_cast<Ptr>(p);}
 Ptr array(unsigned stride,unsigned count,const void* data=nullptr){Ptr cls=alloc(0x200),p=alloc(0x20+stride*count);store<uint16_t>(cls+0xba,stride);store<Ptr>(p,cls);store<uint64_t>(p+0x18,count);if(data&&count)std::memcpy(reinterpret_cast<void*>(p+0x20),data,stride*count);return p;}
 Ptr object(std::string kind,std::string name){Ptr p=alloc(0x20),n=alloc(0x100);store<Ptr>(p,kindClass(kind));store<Ptr>(p+0x10,n);store<uint32_t>(n+8,nextId++);obj[p]=Obj{kind,name,0,0,0,n,0,0,{}};return p;}
 Ptr mesh(std::string name="Body",unsigned vertices=3){Ptr p=object("mesh",name),shared=alloc(0x120);auto&o=obj[p];o.shared=shared;store<Ptr>(o.native+0x48,shared);store<uint32_t>(shared+0x74,vertices);store<uint32_t>(shared+0xc0,1);store<uint64_t>(shared+0xe0,1);store<uint64_t>(o.native+0x90,1);store<uint32_t>(shared+0xf8,15);store<Ptr>(shared+0xf0,alloc(32*6));store<uint64_t>(shared+0x100,3);store<uint64_t>(shared+0x108,6);
  for(auto c:{std::pair{abi::getPositions,12},std::pair{abi::getNormals,12},std::pair{abi::getUV,8},std::pair{abi::getTriangles,4},std::pair{abi::getColors,4},std::pair{abi::getTangents,16}})o.arrays[c.first]=array(c.second,vertices);
  Bytes binds(64);for(int i=0;i<4;i++){float f=1;std::memcpy(binds.data()+i*20,&f,4);}o.arrays[abi::getBindposes]=array(64,1,binds.data());return p;}
 Ptr renderer(Ptr mesh,const char* bone="BoneA",const char* name="Body",Ptr root=0){Ptr r=object("renderer",name);auto&o=obj[r];o.mesh=mesh;Ptr m=object("material","Material");o.materials=typed("material",m);Ptr b=object("bone",bone);obj[b].parent=root?root:skeletonRoot;o.bones=typed("bone",b);return r;}
 // Like Object.Instantiate of a renderer's GameObject: same mesh and material references, new bone transforms.
 Ptr instantiate(Ptr src){Ptr r=object("renderer",obj.at(src).name);Ptr b=object("bone","BoneA");obj[b].parent=skeletonRoot;auto& o=obj[r];o.mesh=obj.at(src).mesh;o.materials=obj.at(src).materials;o.bones=typed("bone",b);return r;}
 // A model GameObject as the game hands it over: inactive, renderers already set up.
 Ptr gameObjectOf(std::vector<Ptr> renderers){Ptr g=object("gameobject","Avatar_Girl_Catalyst_Citlali_Manekin");for(Ptr r:renderers)goOfRenderer[r]=g;goChildren[g]=renderers;return g;}
 void world(Ptr r){store<Ptr>(owner+0x68,r?array(8,1,&r):array(8,0));}
 Fake(){void* p=mmap(nullptr,0x16000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);assert(p!=MAP_FAILED);base=reinterpret_cast<Ptr>(p);Ptr root=alloc(0x8c10);manager=alloc(0x300);store<Ptr>(base+abi::manager,root);store<Ptr>(root+0x8c00,manager);store<uint32_t>(manager+0x288,7);
  Ptr entity=alloc(0x440),list=alloc(0x20);store<Ptr>(manager+0x10,list);store<Ptr>(list+0x10,array(8,1,&entity));store<uint32_t>(list+0x18,1);store<uint32_t>(entity+0x430,7);store<uint32_t>(entity+0x3a4,10000107);owner=alloc(0x70);store<Ptr>(entity+0xf8,owner);
  klass=kindClass("renderer");Ptr scenes=object("transform","Scene");skeletonRoot=object("transform","Avatar_Girl_Catalyst_Citlali");obj[skeletonRoot].parent=scenes;kavehRoot=object("transform","Avatar_Boy_Sword_Kaveh");obj[kavehRoot].parent=scenes;}
 ~Fake(){for(void* p:allocated)std::free(p);munmap(reinterpret_cast<void*>(base),0x16000000);}
}*f=nullptr;
Ptr getMesh(Ptr p,Ptr){return f->obj.at(p).mesh;}Ptr getMaterials(Ptr p,Ptr){return f->obj.at(p).materials;}Ptr getBones(Ptr p,Ptr){f->boneReads[p]++;return f->obj.at(p).bones;}
Ptr getName(Ptr p,Ptr){f->nameReads[p]++;auto n=f->obj.at(p).name;Ptr s=f->alloc(0x14+2*n.size());store<uint32_t>(s+0x10,n.size());for(size_t i=0;i<n.size();i++)store<uint16_t>(s+0x14+i*2,n[i]);return s;}
Ptr clone(Ptr p,Ptr){f->clones++;auto original=f->obj.at(p);if(original.kind!="mesh"){Ptr out=f->object(original.kind,original.name);if(original.kind=="material"){f->diffuse[out]=f->diffuse[p];f->named[out]=f->named[p];f->glow[out]=f->glow[p];f->props[out]=f->props[p];f->keywords[out]=f->keywords[p];}return out;}f->meshClones++;Ptr out=f->mesh(original.name,load<uint32_t>(original.shared+0x74));auto& o=f->obj[out];o.arrays=original.arrays;store<uint64_t>(o.shared+0xe0,load<uint64_t>(original.shared+0xe0));store<uint64_t>(o.native+0x90,load<uint64_t>(original.native+0x90));return out;}
void destroy(Ptr p,Ptr){store<Ptr>(p+0x10,0);}Ptr parent(Ptr p,Ptr){return f->obj.at(p).parent;}
uint32_t pin(Ptr p,int,Ptr){auto h=f->nextHandle++;f->handles[h]=p;return h;}void unpin(uint32_t h){assert(f->handles.erase(h)==1);}
unsigned gameWrites=0;void setMesh(Ptr p,Ptr m,Ptr){gameWrites++;f->obj.at(p).mesh=m;}void setMaterials(Ptr p,Ptr m,Ptr){gameWrites++;if(f->failMaterials){f->failMaterials=false;throw std::runtime_error("fake-material-failure");}f->obj.at(p).materials=m;}
Ptr newArray(Ptr cls,uint64_t count){return f->array(load<uint16_t>(cls+0xba),count);}void op(Ptr,int,Ptr){}void action(Ptr,Ptr){}
void submeshCount(Ptr m,int n,Ptr){assert(n>=1);store<uint32_t>(f->obj.at(m).shared+0xc0,n);}
void sub(Ptr m,Ptr a,int i,Ptr){assert(i>=0&&unsigned(i)<load<uint32_t>(f->obj.at(m).shared+0xc0));unsigned n=unsigned(load<uint64_t>(a+0x18));f->triangles.push_back({m,n});
 auto& v=f->submeshIndices[{m,i}];v.assign(n,0);if(n)std::memcpy(v.data(),reinterpret_cast<const void*>(a+0x20),4ull*n);}
Ptr getPos(Ptr p,Ptr){return f->obj.at(p).arrays.at(abi::getPositions);}Ptr getNorm(Ptr p,Ptr){return f->obj.at(p).arrays.at(abi::getNormals);}Ptr getUV(Ptr p,Ptr){return f->obj.at(p).arrays.at(abi::getUV);}Ptr getIdx(Ptr p,Ptr){return f->obj.at(p).arrays.at(abi::getTriangles);}Ptr getBinds(Ptr p,Ptr){f->bindposeReads++;return f->obj.at(p).arrays.at(abi::getBindposes);}Ptr getColors(Ptr p,Ptr){return f->obj.at(p).arrays.at(abi::getColors);}Ptr getTangents(Ptr p,Ptr){return f->obj.at(p).arrays.at(abi::getTangents);}
// Like il2cpp object_new: the managed shell has no native object until the constructor runs.
Ptr texType(){static Ptr d=0;if(!d){d=f->alloc(0x20);store<Ptr>(d,f->kindClass("texture"));store<Ptr>(d+0x10,f->alloc(8));}return d;}/* Texture2D.whiteTexture: its class is Texture2D's */Ptr objectNew(Ptr cls){assert(cls);Ptr p=f->alloc(0x20);f->obj[p]=Obj{"texture","",0,0,0,0,0,0,{}};return p;}
void texInit(Ptr o,int w,int h,int fmt,int mip,int linear,Ptr){assert(w>0&&h>0&&fmt==4&&mip==0&&(linear==0||linear==1)&&load<Ptr>(o+0x10)==0);Ptr n=f->alloc(0x80);store<Ptr>(o+0x10,n);f->obj[o].native=n;f->textures++;f->textureBytes[o]=unsigned(w*h*4);}
void texPixels(Ptr o,Ptr data,int n,Ptr){assert(load<Ptr>(o+0x10)&&data&&n>0&&unsigned(n)==f->textureBytes.at(o));}
// Managed strings live above 4 GiB in the game; a 32-bit truncation must not survive the round trip.
Ptr newString(Ptr self,const char* s){assert(self==0&&s);Ptr p=(f->stringBase+=0x40);f->strings[p]=s;return p;}
uint32_t hasProp(Ptr mat,Ptr name,Ptr){assert(mat&&f->strings.count(name));return f->strings[name]=="_LightMapTex"?0xffffff01u:0xffffff00u;}
Ptr nativeAlloc(Ptr allocator,Ptr old,uint64_t size,int align,int label,int options,const char* name,int line){assert(allocator==load<Ptr>(f->base+abi::nativeAllocator)&&old==0&&size==32ull*f->expectedVertices&&align==16&&label==15&&options==0&&name&&line==1);f->weightAllocs++;return f->lastWeights=f->alloc(size);}
int32_t glowPropertyId(Ptr name,Ptr);
// A texture by property name; null puts a property back to empty (direct textures restore what a material held).
void setTex(Ptr mat,Ptr name,Ptr tex,Ptr){gameWrites++;assert(mat&&(!tex||load<Ptr>(tex+0x10))&&f->strings.count(name));f->lightProperty=f->strings[name];f->named[mat][glowPropertyId(name,0)]=tex;}
void setMainTexture(Ptr mat,Ptr tex,Ptr){gameWrites++;assert(mat);f->diffuse[mat]=tex;}
unsigned channelSets=0;std::vector<Bytes> channelData;void setChannel(Ptr,Ptr a,Ptr){assert(load<uint64_t>(a+0x18)==f->expectedVertices);channelSets++;
 unsigned stride=load<uint16_t>(load<Ptr>(a)+0xba);channelData.emplace_back(reinterpret_cast<const uint8_t*>(a+0x20),reinterpret_cast<const uint8_t*>(a+0x20)+size_t(f->expectedVertices)*stride);}void setPos(Ptr p,Ptr a,Ptr){if(f->failPosition){f->failPosition=false;throw std::runtime_error("fake-animation-channel-error");}store<uint32_t>(f->obj.at(p).shared+0x74,load<uint64_t>(a+0x18));}void setAny(Ptr,Ptr,Ptr){}
Ptr classFromType(Ptr t,int init){assert(init==1&&load<uint8_t>(t+0xa)!=0);f->typeLookups++;for(auto& kv:f->kinds)if(t==kv.second+0x98)return kv.second;return f->alloc(0x10);}
Ptr typeObject(Ptr t){assert(t==f->klass+0x98);if(!f->typeObj){f->typeObj=f->alloc(0x20);store<Ptr>(f->typeObj+0x10,t);}return f->typeObj;}
Ptr goOf(Ptr r,Ptr){assert(f->obj.at(r).kind=="renderer"||f->obj.at(r).kind=="transform");auto it=f->goOfRenderer.find(r);if(it!=f->goOfRenderer.end())return it->second;Ptr g=f->object("gameobject","GO");f->goOfRenderer[r]=g;return g;}
Ptr children(Ptr go,Ptr type,uint32_t inactive,Ptr){assert(f->obj.at(go).kind=="gameobject"&&type&&type==f->typeObj&&inactive==1);f->childCalls++;auto& v=f->goChildren[go];return f->array(8,v.size(),v.data());}
// Scripting::ScriptingWrapperFor: the managed object of a native one.
Ptr wrapperFor(Ptr native){for(auto& kv:f->obj)if(kv.second.native==native)return kv.first;return 0;}
Ptr findNative(Ptr type,int mode){assert(type&&type==f->typeObj&&mode==2);f->findAllCalls++;if(f->failFind)throw std::runtime_error("fake-find-failure");std::vector<Ptr> all=f->scene;all.insert(all.end(),f->assets.begin(),f->assets.end());return f->array(8,all.size(),all.data());}
Ptr findObjects(Ptr type,Ptr){assert(type&&type==f->typeObj);f->findCalls++;if(f->failFind)throw std::runtime_error("fake-find-failure");return f->array(8,f->scene.size(),f->scene.data());}
// Glow trace read-back (glow_trace.hpp): one colour property (#7) and one texture (#5) per material.
int32_t glowFrame(){static int32_t n=100;return ++n;}
Ptr glowShader(Ptr,Ptr){static Ptr s=f->object("shader","miHoYo/Character/Body");return s;}int32_t glowQueue(Ptr,Ptr){return 2000;}
Ptr glowKeywords(Ptr m,Ptr){auto& k=f->keywords[m];std::vector<Ptr> list;for(auto& w:k){Ptr s=f->alloc(0x14+2*w.size());store<int32_t>(s+0x10,int32_t(w.size()));for(size_t i=0;i<w.size();i++)store<uint16_t>(s+0x14+2*i,uint16_t(w[i]));list.push_back(s);}return f->array(8,unsigned(list.size()),list.data());}
uint32_t glowHas(Ptr,int32_t id,Ptr){return id==7?1u:0u;}float glowFloat(Ptr m,int32_t id,Ptr){return f->props[m][id][0];}int32_t glowInt(Ptr,int32_t,Ptr){return 0;}
void glowColor(Ptr m,int32_t id,float* out,Ptr){std::memcpy(out,f->props[m][id].data(),16);}void glowZero(Ptr,int32_t,float* out,Ptr){std::memset(out,0,16);}
Ptr glowTexture(Ptr m,int32_t id,Ptr){if(id==5)return f->diffuse[m];auto it=f->named.find(m);return it!=f->named.end()&&it->second.count(id)?it->second.at(id):0;}
// Shader.PropertyToID for names made by newString (the Runtime's own lookups): _MainTex 5, _ElementRimColor 7, others above 100.
int32_t glowPropertyId(Ptr name,Ptr){auto it=f->strings.find(name);std::string n=it!=f->strings.end()?it->second:"";if(n=="_MainTex")return 5;if(n=="_ElementRimColor")return 7;return 100+int32_t(std::hash<std::string>()(n)%1000);}Ptr glowTextureIds(Ptr,Ptr){int32_t id=5;return f->array(4,1,&id);}float glowCustom(Ptr,int32_t,Ptr){return 0;}
// Per-slot overrides (slotTextures experiment): MaterialPropertyBlock contents, the renderer's block per material index
// (Get copies it into the block given, Set stores the block's contents, an empty block removes it), AllowPerMaterialProp
// with garbage above bit 0, and the custom per-slot textures, which can only be written. Writing a slot's block drops
// that slot's custom textures here: one way the engine may behave, and the order of a mode change must hold up to it.
struct SlotFake {std::map<Ptr,std::map<int32_t,Ptr>> blocks;std::map<std::pair<Ptr,int32_t>,std::map<int32_t,Ptr>> slots,custom;std::map<Ptr,uint32_t> allow;unsigned ctors=0,sets=0;}slotFake;
Ptr slotShared(Ptr){static Ptr b=0;if(!b){b=f->alloc(0x20);store<Ptr>(b,f->kindClass("block"));store<Ptr>(b+0x10,f->alloc(8));}return b;}
void blockCtor(Ptr b,Ptr){assert(load<Ptr>(b+0x10)==0);store<Ptr>(b+0x10,f->alloc(8));slotFake.ctors++;}
void blockClear(Ptr b,Ptr){assert(load<Ptr>(b+0x10));slotFake.blocks[b].clear();}
void blockSet(Ptr b,int32_t id,Ptr t,Ptr){assert(load<Ptr>(b+0x10)&&t);slotFake.blocks[b][id]=t;}
Ptr blockGet(Ptr b,int32_t id,Ptr){auto& m=slotFake.blocks[b];auto it=m.find(id);return it==m.end()?0:it->second;}
void slotGet(Ptr r,Ptr b,int32_t i,Ptr){assert(f->obj.at(r).kind=="renderer"&&i>=0);auto it=slotFake.slots.find({r,i});slotFake.blocks[b]=it==slotFake.slots.end()?std::map<int32_t,Ptr>{}:it->second;}
void slotSet(Ptr r,Ptr b,int32_t i,Ptr){assert(f->obj.at(r).kind=="renderer"&&i>=0);slotFake.sets++;slotFake.custom.erase({r,i});auto& m=slotFake.blocks[b];if(m.empty())slotFake.slots.erase({r,i});else slotFake.slots[{r,i}]=m;}
uint32_t slotAllowed(Ptr r,Ptr){return 0xabcdef00u|slotFake.allow[r];}void slotAllow(Ptr r,int on,Ptr){assert(on==0||on==1);slotFake.allow[r]=uint32_t(on);}
void slotCustom(Ptr r,int32_t i,int32_t id,Ptr t,int32_t update,Ptr){assert(f->obj.at(r).kind=="renderer"&&i>=0&&update==1);slotFake.custom[{r,i}][id]=t;}
Ptr resolveGlow(abi::Offset o){
 for(auto x:{std::pair{glow::rvaFrameCount,reinterpret_cast<Ptr>(&glowFrame)},{glow::rvaGetShader,reinterpret_cast<Ptr>(&glowShader)},{glow::rvaGetRenderQueue,reinterpret_cast<Ptr>(&glowQueue)},
   {glow::rvaGetKeywords,reinterpret_cast<Ptr>(&glowKeywords)},{glow::rvaHasProperty,reinterpret_cast<Ptr>(&glowHas)},{glow::rvaGetFloat,reinterpret_cast<Ptr>(&glowFloat)},
   {glow::rvaGetInt,reinterpret_cast<Ptr>(&glowInt)},{glow::rvaGetColor,reinterpret_cast<Ptr>(&glowColor)},{glow::rvaGetVector,reinterpret_cast<Ptr>(&glowColor)},
   {glow::rvaGetScaleOffset,reinterpret_cast<Ptr>(&glowZero)},{glow::rvaGetTexture,reinterpret_cast<Ptr>(&glowTexture)},{glow::rvaGetTextureIds,reinterpret_cast<Ptr>(&glowTextureIds)},
   {glow::rvaGetCustomFloat,reinterpret_cast<Ptr>(&glowCustom)},{glow::rvaPropertyToId,reinterpret_cast<Ptr>(&glowPropertyId)}})if(x.first==o)return x.second;
 return 0;}
// Texture-only diagnostics: the engine's Mesh / Texture getters (index ranges per submesh from texRanges, else one
// submesh 0..3); every texture 4x4, RGBA32, one mip. Read only: none of them changes anything.
std::map<Ptr,std::vector<std::pair<uint32_t,uint32_t>>> texRanges;
int32_t meshSubs(Ptr m,Ptr){auto it=texRanges.find(m);return it!=texRanges.end()?int32_t(it->second.size()):int32_t(load<uint32_t>(f->obj.at(m).shared+0xc0));}
int32_t meshVerts(Ptr m,Ptr){return int32_t(load<uint32_t>(f->obj.at(m).shared+0x74));}uint32_t meshReadable(Ptr,Ptr){return 0xffffff00u;}
uint32_t meshStart(Ptr m,int32_t i,Ptr){assert(i>=0&&i<meshSubs(m,0));auto it=texRanges.find(m);return it!=texRanges.end()?it->second[size_t(i)].first:0;}
uint32_t meshCount(Ptr m,int32_t i,Ptr){assert(i>=0&&i<meshSubs(m,0));auto it=texRanges.find(m);return it!=texRanges.end()?it->second[size_t(i)].second:3;}
int32_t texSide(Ptr t,Ptr){assert(t&&load<Ptr>(t+0x10));return 4;}int32_t texFormat(Ptr t,Ptr){assert(load<Ptr>(t)==f->kindClass("texture"));return 4;}int32_t texMips(Ptr t,Ptr){assert(load<Ptr>(t)==f->kindClass("texture"));return 1;}
Ptr resolve(abi::Offset o){if(Ptr g=resolveGlow(o))return g;switch(o){
#define FN(x,y) case abi::x:return reinterpret_cast<Ptr>(&y)
 FN(getMesh,getMesh);FN(getMaterials,getMaterials);FN(getBones,getBones);FN(getName,getName);FN(getParent,parent);FN(clone,clone);FN(destroy,destroy);FN(pin,pin);FN(unpin,unpin);FN(setMesh,setMesh);FN(setMaterials,setMaterials);FN(arrayNew,newArray);FN(getBindposes,getBinds);FN(getPositions,getPos);FN(getNormals,getNorm);FN(getUV,getUV);FN(getTriangles,getIdx);FN(getColors,getColors);FN(getTangents,getTangents);FN(setColors,setChannel);FN(setTangents,setChannel);FN(setUV2,setChannel);FN(setPositions,setPos);FN(setNormals,setAny);FN(setUV,setAny);FN(setTriangles,setAny);FN(clear,op);FN(setIndexFormat,op);FN(setSubmeshCount,submeshCount);FN(meshUpload,op);FN(meshFinish,action);FN(meshDynamic,action);FN(setSubmeshTriangles,sub);FN(textureType,texType);FN(objectNew,objectNew);FN(textureInit,texInit);FN(texturePixels,texPixels);FN(textureApply,action);FN(mainTexture,setMainTexture);FN(newString,newString);FN(nativeAlloc,nativeAlloc);FN(hasProperty,hasProp);FN(setTexture,setTex);FN(classFromType,classFromType);FN(typeObject,typeObject);FN(findObjectsOfType,findObjects);FN(findObjectsNative,findNative);FN(componentsInChildren,children);FN(gameObject,goOf);FN(wrapperFor,wrapperFor);
 FN(blockShared,slotShared);FN(blockCtor,blockCtor);FN(blockClear,blockClear);FN(blockSetTexture,blockSet);FN(blockGetTexture,blockGet);FN(getSlotBlock,slotGet);FN(setSlotBlock,slotSet);
 FN(meshSubmeshes,meshSubs);FN(meshVertices,meshVerts);FN(meshReadable,meshReadable);FN(meshIndexStart,meshStart);FN(meshIndexCount,meshCount);
 FN(textureWidth,texSide);FN(textureHeight,texSide);FN(textureFormat,texFormat);FN(textureMips,texMips);
 FN(allowSlotProps,slotAllowed);FN(setAllowSlotProps,slotAllow);FN(setSlotCustomTexture,slotCustom);
#undef FN
 default:throw std::runtime_error("Unexpected fake engine call: "+std::to_string(o));}}
bool alive(Ptr p){return p&&load<Ptr>(p+0x10);}
}
int main(int argc,char** argv){Fake fake;f=&fake;char temp[]="/tmp/model-runtime-XXXXXX";assert(mkdtemp(temp));std::string root=temp,pkg="com.miHoYo.Yuanshen",media=root+"/media",shared=media+"/"+pkg+"/GIBox",models=shared+"/models",boneRoot=shared+"/skeletons";
 writeModel(models+"/A");writeModel(models+"/B");
 // Model B carries a real 1x1 diffuse and a LightMap, so its material builds two textures.
 {Spec b;b.textures=true;b.channels=true;writeModel(models+"/B",b);}
 // C only knows its target (another character, never controlled): it must still follow that mesh wherever it appears.
 writeModel(models+"/C",Spec().character(10000200).withTarget(fixture::target("OtherBody")));
 // V has two variants: $swap=1 hides the only submesh.
 {Spec v;v.variants="{\"vars\":[{\"name\":\"$swap\",\"values\":[0,1]}],\"combos\":[{\"$swap\":0},{\"$swap\":1}],\"default\":0}";v.draws.push_back("[{\"start\":0,\"count\":0,\"material\":\"Material0\",\"slot\":0}]");writeModel(models+"/V",v);}
 Ptr original=f->mesh();Ptr world=f->renderer(original);f->world(world);
 Ptr ui=f->renderer(original);Ptr uiMaterials=f->obj[ui].materials;                  // detail page: same asset
 Ptr npc=f->renderer(f->mesh("Body",4));                                                // same name, different mesh
 Ptr odd=f->renderer(original,"Other");                                                 // same mesh, other skeleton
 Ptr other=f->renderer(f->mesh("OtherBody"),"BoneA","Body",f->kavehRoot);Ptr otherMesh=f->obj[other].mesh;  // another character (model C, U)
 f->scene={world,ui,npc,odd,other};
 Engine e(f->base);e.resolver=resolve;Runtime runtime(e,pkg,root+"/private",media,false);store<Ptr>(f->base+abi::nativeAllocator,f->alloc(0x10));
 // Optional, local-only integration input: a real exported index-only model, never bundled into the test suite.
 if(argc>=2){bool expectWait=argc==3&&std::string(argv[2])=="--expect-wait";assert(argc==2||expectWait);runtime.shutdown();std::string dest=models+"/Actual";std::filesystem::copy(argv[1],dest,std::filesystem::copy_options::recursive);auto data=loadModelFile(dest,false,true);assert(data->id==10000007&&data->parts.size()==1&&data->parts[0].sourceBones.empty());auto& part=data->parts[0];auto old=readLearned(dest).targets.at(part.spec.name);auto names=readLegacyBoneNames(dest+"/bonemap-"+part.spec.name+".tsv",part.spec.bones);
  assert(part.geo.n==20757&&part.used.size()==101&&old.bones==114);f->expectedVertices=part.geo.n;f->obj[f->skeletonRoot].name="Avatar_Girl_Sword_PlayerGirl";
  Ptr entity=load<Ptr>(load<Ptr>(load<Ptr>(f->manager+0x10)+0x10)+0x20);store<uint32_t>(entity+0x3a4,10000007);
  Ptr low=f->mesh("Body_LOD1",6594),r=f->renderer(low);store<uint32_t>(f->obj[low].shared+0xc0,3);store<uint64_t>(f->obj[low].shared+0xe0,114);std::vector<Ptr> bones;Bytes binds(114*64);
  for(unsigned i=0;i<114;i++){Ptr bone=f->object("bone",names[i].empty()?"Unused"+std::to_string(i):names[i]);f->obj[bone].parent=f->skeletonRoot;bones.push_back(bone);for(unsigned n=0;n<4;n++)store<float>(reinterpret_cast<Ptr>(binds.data())+i*64+n*20,1.f);}
  f->obj[r].bones=f->array(8,114,bones.data());f->obj[low].arrays[abi::getBindposes]=f->array(64,114,binds.data());f->world(r);f->scene={r};write(shared+"/control.json","{\"enabled\":true,\"selected\":[\"Actual\"],\"logLevel\":\"debug\"}");
  {Runtime actual(e,pkg,root+"/actual-private",media,false);for(uint64_t now=1000000;now<=1004000;now+=200)actual.tick(now);Ptr mod=f->obj[r].mesh;auto log=text(shared+"/GIBox Log.txt");
   if(expectWait){assert(mod==low&&f->meshClones==0&&JsonParser(text(shared+"/status.json")).parse().at("state").string=="waiting");actual.shutdown();assert(f->handles.empty());std::filesystem::remove_all(root);std::cout<<"Prior runtime: reproduced waiting on the real export with no skeleton library\n";return 0;}
   assert(mod!=low&&log.find("matched-used=101")!=std::string::npos&&load<uint32_t>(f->obj[mod].shared+0x74)==20757&&!exists(boneRoot+"/10000007/skeleton.json"));
   Ptr skin=load<Ptr>(f->obj[mod].shared+0xf0);for(unsigned v=0;v<part.geo.n;v++){std::map<unsigned,float> expected,observed;for(unsigned k=0;k<4;k++){auto p=part.weights.data()+32ull*v;float w=f32(p+4*k);if(w>0)expected[u32(p+16+4*k)]+=w;auto q=reinterpret_cast<const uint8_t*>(skin)+32ull*v;w=f32(q+4*k);if(w>0)observed[u32(q+16+4*k)]+=w;}assert(expected.size()==observed.size());for(auto& pair:expected)assert(observed.count(pair.first)&&std::abs(pair.second-observed[pair.first])<2e-6);}
   auto recorded=readLegacyBoneNames(dest+"/bonemap-"+part.spec.name+".tsv",part.spec.bones);for(unsigned i=0;i<part.spec.bones;i++)if(!part.used.count(i))assert(recorded[i].empty());
   unsigned clones=f->meshClones;uint64_t switchAt=1005000;for(unsigned mode:{1u,2u,0u}){write(shared+"/control.json","{\"enabled\":true,\"selected\":[\"Actual\"],\"logLevel\":\"debug\",\"experiments\":{\"animationMode\":"+std::to_string(mode)+"}}");actual.tick(switchAt);switchAt+=1000;assert(f->obj[r].mesh==mod&&f->meshClones==clones);}
   actual.shutdown();assert(f->obj[r].mesh==low&&f->handles.empty());}
  unsigned clones=f->meshClones;f->obj[bones[*part.used.begin()]].name="WrongUsedBone";
  {Runtime rejected(e,pkg,root+"/actual-wrong-palette",media,false);for(uint64_t now=1010000;now<=1012000;now+=200)rejected.tick(now);assert(f->obj[r].mesh==low&&f->meshClones==clones);rejected.shutdown();assert(f->handles.empty());}
  std::filesystem::remove_all(root);std::cout<<"Real legacy export: PASS (101 used names checked, 114-bone LOD1, all 20757 vertex influences preserved, wrong palette rejected, original restored)\n";return 0;}
 // rev only makes control.json differ (a control change clears latched failures)
 auto control=[&](bool enabled,int rev,int exportBones,std::vector<std::string> selected,std::string vars="{}"){std::string list="[";for(size_t i=0;i<selected.size();i++)list+=(i?",":"")+quote(selected[i]);list+="]";
  write(shared+"/control.json.tmp","{\"enabled\":"+std::string(enabled?"true":"false")+",\"selected\":"+list+",\"revision\":"+std::to_string(rev)+",\"exportBones\":"+std::to_string(exportBones)+",\"variables\":"+vars+"}");std::filesystem::rename(shared+"/control.json.tmp",shared+"/control.json");};
 auto status=[&]{return JsonParser(text(shared+"/status.json")).parse();};auto state=[&]{return status().at("state").string;};
 auto meshOf=[&](Ptr r){return f->obj[r].mesh;};

 // 0. The bone library is recorded while replacement is still off (one-click import depends on it).
 control(false,0,0,{});runtime.tick(9000);assert(!exists(boneRoot+"/10000107/skeleton.json"));runtime.tick(9500);assert(state()=="disabled"&&exists(boneRoot+"/10000107/skeleton.json")&&meshOf(world)==original&&exists(shared+"/GIBox Log.txt"));
 // 1. Legacy model A: target learned from the world character, then every renderer showing that asset follows.
 control(true,0,0,{"A","B"});runtime.tick(10000);Ptr a1=meshOf(world);
 assert(state()=="active"&&a1!=original&&meshOf(ui)==a1&&f->obj[ui].materials!=uiMaterials&&f->obj[ui].materials!=f->obj[world].materials);
 assert(meshOf(npc)!=a1&&meshOf(odd)==original&&meshOf(other)==otherMesh);
 assert(f->findCalls==1&&f->typeLookups>0&&text(shared+"/GIBox Log.txt").find("byval_arg at class+0x98 this_arg +0x40")!=std::string::npos&&status().at("scan").string=="ok"&&learnedAt(models+"/A").targets.at("Part0").mesh=="Body");
 // Skin weights: a new engine block of exactly the model's size, written and installed in the clone's skin array.
 {Ptr d=f->obj[a1].shared;float w;std::memcpy(&w,reinterpret_cast<void*>(load<Ptr>(d+0xf0)),4);assert(f->weightAllocs==1&&load<Ptr>(d+0xf0)==f->lastWeights&&w==1&&load<uint64_t>(d+0x100)==3&&load<uint64_t>(d+0x108)==3);}
 // Skeleton library for the import: per-renderer bones, bindposes and mesh identity, plus the skeleton's transform chain.
 {auto sk=JsonParser(text(boneRoot+"/10000107/skeleton.json")).parse();auto& r=sk.at("renderers").array.at(0);
  assert(sk.at("id").number==10000107&&sk.at("root").string.find("Avatar_Girl_Catalyst_Citlali")!=std::string::npos&&r.at("name").string=="Body"&&r.at("bones").array.at(0).string=="BoneA");
  assert(r.at("bindposes").array.size()==16&&r.at("bindposes").array[0].number==1&&fingerprintOf(r.at("target")).mesh=="Body"&&r.at("parents").array[0].number==-1);}
 // 2. Asset reloaded: a new original mesh with the same fingerprint (name suffix normalised) reuses the built mesh.
 //    B (selected after A, same mesh) is not built: everything it replaces is shown from A.
 Ptr ui2=f->renderer(f->mesh("Body(Clone)"));f->scene.push_back(ui2);unsigned lookups=f->typeLookups;runtime.tick(10100);assert(meshOf(ui2)!=a1);runtime.tick(10500);
 assert(meshOf(ui2)==a1&&f->meshClones==2&&f->typeLookups==lookups&&f->findCalls==2);
 // 3. Two models of one character: the one switched on first shows. B first: B is built and takes the renderers from A
 //    in the same tick (two textures, LightMap property chosen by name). A first again: A, still built, takes them back.
 control(true,1,0,{"B","A"});runtime.tick(10600);Ptr b1=meshOf(world);
 assert(state()=="active"&&b1!=a1&&meshOf(ui)==b1&&meshOf(ui2)==b1&&f->textures==2&&f->lightProperty=="_LightMapTex"&&f->meshClones==4&&channelSets==3);
 assert(status().at("models").array.size()==1&&status().at("models").array[0].string=="B");
 control(true,2,0,{"A","B"});runtime.tick(10700);assert(meshOf(world)==a1&&meshOf(ui)==a1&&meshOf(ui2)==a1&&f->meshClones==4&&text(shared+"/GIBox Log.txt").find("outranked B/Part0 on Body")!=std::string::npos);
 // 4. Character leaves (renderers destroyed) and returns as new objects: no rebuild.
 for(Ptr r:{world,ui,ui2}){store<Ptr>(r+0x10,0);}f->scene={npc,odd,other};f->world(0);store<uint32_t>(f->manager+0x288,0);runtime.tick(11200);assert(state()=="waiting");
 Ptr world2=f->renderer(original);f->world(world2);f->scene={world2,npc,odd,other};store<uint32_t>(f->manager+0x288,7);runtime.tick(11700);assert(state()=="active"&&meshOf(world2)==a1&&f->meshClones==4);
 // 5. Manual bone export keeps working; renderer names are filtered.
 f->obj[world2].name="Body (1)";control(true,2,1,{"A","B"});runtime.tick(11800);runtime.tick(12300);{bool found=false;for(auto& d:std::filesystem::directory_iterator(boneRoot))if(d.path().filename().string().rfind("10000107-",0)==0&&text(d.path().string()+"/skeleton.json").find("\"name\":\"Body1\"")!=std::string::npos)found=true;assert(found&&state()=="active");}f->obj[world2].name="Body";
 // 6. Hot reload: a new generation is built and takes over in one tick; the old mesh is destroyed.
 writeModel(models+"/A",Spec().gen("2"));runtime.tick(13000);Ptr a2=meshOf(world2);assert(a2!=a1&&!alive(a1)&&f->meshClones==6&&state()=="active");
 // A broken edit keeps the last good generation on screen and reports the error.
 writeModel(models+"/A",Spec().gen("3"));write(models+"/A/model.bin","bad");runtime.tick(14100);assert(meshOf(world2)==a2&&state()=="active"&&!status().at("errors").array.empty());
 writeModel(models+"/A",Spec().gen("4"));runtime.tick(15200);Ptr a4=meshOf(world2);assert(a4!=a2&&status().at("errors").array.empty());
 // 7. A target-only model follows its own character although another one is controlled.
 control(true,2,1,{"A","B","C"});runtime.tick(15300);assert(meshOf(other)!=otherMesh&&meshOf(world2)==a4&&status().at("models").array.size()==2);
 // 8. Variants switch in place: same mesh object, new submesh ranges.
 control(true,2,1,{"V"});runtime.tick(15400);Ptr v=meshOf(world2);assert(v!=a4&&meshOf(other)==otherMesh&&f->triangles.back()==std::make_pair(v,3u));
 control(true,2,1,{"V"},"{\"V\":{\"$swap\":1}}");runtime.tick(15500);assert(meshOf(world2)==v&&f->triangles.back()==std::make_pair(v,0u));
 control(true,2,1,{"V"},"{\"V\":{\"$swap\":0}}");runtime.tick(15600);assert(meshOf(world2)==v&&f->triangles.back()==std::make_pair(v,3u));
 // 8b. Imported without a bone library: found by the character name in the skeleton chain, bones taken from the game,
 //     the ID written back once the character is seen in the world; a name seen only outside the world keeps ID 0.
 for(auto [dir,token]:{std::pair<const char*,const char*>{"T","citlali"},{"U","kaveh"}})writeModel(models+"/"+dir,Spec().character(0,{token}).noSkeleton());
 control(true,2,1,{"T","U"});runtime.tick(15700);runtime.tick(15800);
 assert(meshOf(world2)!=original&&meshOf(world2)!=v&&meshOf(other)!=otherMesh&&learnedAt(models+"/T").targets.count("Part0")&&learnedAt(models+"/T").id==10000107&&learnedAt(models+"/U").id==0&&learnedAt(models+"/U").targets.at("Part0").mesh=="OtherBody"&&status().at("models").array.size()==2);
 // 9. Disable restores every renderer and releases every handle.
 control(false,2,1,{"V"});runtime.tick(16000);assert(state()=="disabled"&&meshOf(world2)==original&&meshOf(other)==otherMesh&&f->handles.empty());
 // 10. Load failure latches per generation; a control change retries.
 write(models+"/A/model.bin","bad");control(true,2,1,{"A"});runtime.tick(16100);assert(state()=="error"&&meshOf(world2)==original&&f->handles.empty());
 unsigned built=f->meshClones;runtime.tick(16700);assert(f->meshClones==built);
 writeModel(models+"/A",Spec().gen("5"));control(true,3,1,{"A"});runtime.tick(16800);assert(state()=="active");
 // 11. Attaching fails after the mesh was set: the renderer gets its original mesh back and the error is reported.
 control(false,3,1,{"A"});runtime.tick(16900);f->failMaterials=true;control(true,3,1,{"A"});runtime.tick(17000);
 assert(meshOf(world2)==original&&!status().at("errors").array.empty());control(true,4,1,{"A"});runtime.tick(17100);assert(state()=="active"&&meshOf(world2)!=original);
 // 12. A failing renderer search only backs off; the world character keeps its model.
 {f->failFind=true;Ptr w=meshOf(world2);runtime.tick(17700);assert(state()=="active"&&meshOf(world2)==w);unsigned calls=f->findCalls;runtime.tick(18300);assert(f->findCalls==calls);f->failFind=false;runtime.tick(22800);assert(f->findCalls==calls+1&&state()=="active");}
 // 12b. A renderer the game copies from one of ours shows our mesh from the start and is adopted; when the source goes
 //      away, its material clones stay alive for the copy.
 Ptr model=meshOf(world2),source=f->renderer(original),sourceMats=f->obj[source].materials;f->scene.push_back(source);runtime.tick(25000);
 assert(model!=original&&meshOf(source)==model&&f->obj[source].materials!=sourceMats);
 Ptr copy=f->instantiate(source);assert(meshOf(copy)==model);f->scene.push_back(copy);runtime.tick(25600);
 assert(text(shared+"/GIBox Log.txt").find("adopted A/Part0 -> Body")!=std::string::npos&&meshOf(copy)==model&&state()=="active");
 Ptr shared0=load<Ptr>(f->obj[copy].materials+0x20);store<Ptr>(source+0x10,0);runtime.tick(26200);assert(alive(shared0)&&meshOf(copy)==model);
 // 12c. Async hand-off: the game passes a new, still inactive model GameObject before activating it. A built model is
 //      attached right there, with no tick in between. D was never in the scene: the tick preloads its files and the
 //      hand-off builds it from the handed renderer. Anything that is not a GameObject is ignored.
 //      The game's own callback then puts its mesh back on the renderer (seen on device): ours goes back right after it.
 {Ptr handed=f->renderer(original),go=f->gameObjectOf({handed}),handedMats=f->obj[handed].materials;unsigned calls=f->childCalls;runtime.spawned(go);
  assert(meshOf(handed)==model);f->obj[handed].mesh=original;runtime.spawnedAfter();
  assert(f->childCalls==calls+1&&meshOf(handed)==model&&text(shared+"/GIBox Log.txt").find("spawn Avatar_Girl_Catalyst_Citlali_Manekin: attached 1, adopted 0, built 0")!=std::string::npos);
  assert(text(shared+"/GIBox Log.txt").find("spawn-after after the game's hand-off callback: kept 0, put back 1, lost 0")!=std::string::npos);
  writeModel(models+"/D",Spec().character(10000300).withTarget(fixture::target("SpawnBody")));
  control(true,4,1,{"A","D"});runtime.tick(26300);
  Ptr spawnMesh=f->mesh("SpawnBody"),dr=f->renderer(spawnMesh,"BoneA","Body",f->kavehRoot),dgo=f->gameObjectOf({dr});unsigned before=f->meshClones;
  runtime.spawned(dr);assert(meshOf(dr)==spawnMesh);
  runtime.spawned(dgo);runtime.spawnedAfter();assert(meshOf(dr)!=spawnMesh&&f->meshClones==before+2&&text(shared+"/GIBox Log.txt").find("attached 1, adopted 0, built 1")!=std::string::npos);
  // 12d. Level-of-detail swap by the game between ticks: ours is back on the next frame; disabling later restores the
  //      LOD0 original (the renderer's bones stay bound for that layout).
  Ptr lod=f->mesh("Body_LOD3",2);f->obj[world2].mesh=lod;runtime.tick(26310);assert(meshOf(world2)==model&&text(shared+"/GIBox Log.txt").find("reclaimed A/Part0 <- Body_LOD3")!=std::string::npos);
  // 12e. The game's own rebinding (native LOD switch / BindMeshBones) asks for its mesh on a renderer we attached: it gets
  //      ours, LODs included (an LOD never seen before is resolved through its managed wrapper). Other meshes, renderers
  //      we did not attach, and a mesh whose bone-name hashes differ from the original's pass unchanged.
  {Ptr wn=load<Ptr>(world2+0x10),ourN=load<Ptr>(model+0x10),origN=load<Ptr>(original+0x10),lod2=f->mesh("Body_LOD2",2),capeN=load<Ptr>(f->mesh("Cape",3)+0x10);
   assert(runtime.bindMesh(wn,origN)==ourN&&runtime.bindMesh(wn,load<Ptr>(lod2+0x10))==ourN&&runtime.bindMesh(wn,capeN)==capeN&&runtime.bindMesh(wn,ourN)==ourN);
   assert(runtime.bindMesh(load<Ptr>(npc+0x10),origN)==origN&&text(shared+"/GIBox Log.txt").find("bind-substituted A/Part0: game asked for Body_LOD2 (2 vertices)")!=std::string::npos);
   // Off the Tick thread nothing is touched: the game's request passes as it is.
   {Ptr got=0;std::thread other([&]{got=runtime.bindMesh(wn,origN);});other.join();assert(got==origN&&text(shared+"/GIBox Log.txt").find("hook-skip mesh binding called on another thread than Tick")!=std::string::npos);}
   Ptr dN=load<Ptr>(dr+0x10),spawnN=load<Ptr>(spawnMesh+0x10);store<uint64_t>(load<Ptr>(meshOf(dr)+0x10)+0x90,0);
   assert(runtime.bindMesh(dN,spawnN)==spawnN&&text(shared+"/GIBox Log.txt").find("bind-incompatible D/Part0")!=std::string::npos);}
  // A different mesh is left to the game, and the renderer gets its original materials back before our clones are
  // destroyed (0.3.5 left destroyed clones on it, which draws magenta).
  Ptr cape=f->mesh("Cape",3),handedClone=load<Ptr>(f->obj[handed].materials+0x20);f->obj[handed].mesh=cape;runtime.tick(26320);assert(meshOf(handed)==cape);
  runtime.tick(26800);assert(meshOf(handed)==cape&&f->obj[handed].materials==handedMats&&!alive(handedClone)&&alive(load<Ptr>(handedMats+0x20)));
  control(false,4,1,{"A","D"});runtime.tick(26900);
  assert(state()=="disabled"&&meshOf(copy)==original&&f->obj[copy].materials==sourceMats&&meshOf(handed)==cape&&meshOf(dr)==spawnMesh&&meshOf(world2)==original&&!alive(shared0)&&f->handles.empty());
  assert(runtime.bindMesh(load<Ptr>(world2+0x10),load<Ptr>(original+0x10))==load<Ptr>(original+0x10));  // disabled: untouched
  for(Ptr r:{copy,handed,dr,world2})assert(alive(load<Ptr>(f->obj[r].materials+0x20)));

  Ptr later=f->renderer(original),lgo=f->gameObjectOf({later});runtime.spawned(lgo);assert(meshOf(later)==original&&f->handles.empty());}
 runtime.shutdown();assert(meshOf(world2)==original&&f->handles.empty());
 // A new session finds the skeleton library unchanged and leaves the file alone.
 {auto file=boneRoot+"/10000107/skeleton.json";auto before=text(file);auto stamp=std::filesystem::last_write_time(file);std::this_thread::sleep_for(std::chrono::milliseconds(30));
  control(false,0,0,{});Runtime again(e,pkg,root+"/private",media,false);again.tick(30000);again.tick(30100);
  assert(text(file)==before&&std::filesystem::last_write_time(file)==stamp&&text(shared+"/GIBox Log.txt").find("bone-library "+boneRoot)!=std::string::npos);again.shutdown();assert(f->handles.empty());}
 // 13. Threaded mode (as in the game): file work on the worker, engine calls only from tick().
 //     The classes lose their this_arg pair here, so the Type comes from the array-class sign alone.
 f->breakPairs();f->linkArrays(true);
 {std::string media2=root+"/media2",shared2=media2+"/"+pkg+"/GIBox",models2=shared2+"/models";writeModel(models2+"/A");std::filesystem::create_directories(shared2);
  write(shared2+"/control.json","{\"enabled\":true,\"selected\":[\"A\"],\"cycle\":0,\"exportBones\":0}");
  {Runtime threaded(e,pkg,root+"/private2",media2,true);bool ok=false;
   for(int i=0;i<300&&!ok;i++){threaded.tick(100000+uint64_t(i)*100);std::this_thread::sleep_for(std::chrono::milliseconds(10));try{ok=JsonParser(text(shared2+"/status.json")).parse().at("state").string=="active";}catch(const std::exception&){}}
   assert(ok&&meshOf(world2)!=original);threaded.tick(200000);threaded.shutdown();assert(meshOf(world2)==original&&f->handles.empty());}
  assert(exists(shared2+"/skeletons/10000107/skeleton.json")&&text(shared2+"/GIBox Log.txt").find("byval_arg at class+0x98 no this_arg pair, array votes 2")!=std::string::npos);}
 // 14. Neither sign: three failed searches turn the scan off, the classes are dumped once, the world character keeps
 //     working and a model of another character says why it waits.
 f->linkArrays(false);
 {std::string media3=root+"/media3",shared3=media3+"/"+pkg+"/GIBox",models3=shared3+"/models";writeModel(models3+"/C",Spec().character(10000200).withTarget(fixture::target("OtherBody")));
  std::filesystem::create_directories(shared3);write(shared3+"/control.json","{\"enabled\":true,\"selected\":[\"C\"],\"cycle\":0,\"exportBones\":0}");
  Runtime blind(e,pkg,root+"/private3",media3,false);unsigned calls=f->findCalls;for(uint64_t t:{300000ull,305000ull,310000ull,315000ull})blind.tick(t);
  auto st=JsonParser(text(shared3+"/status.json")).parse();auto log=text(shared3+"/GIBox Log.txt");
  assert(f->findCalls==calls&&st.at("state").string=="waiting"&&st.at("scan").string=="off"&&st.at("detail").string.find("全场景扫描不可用")!=std::string::npos&&meshOf(other)==otherMesh);
  assert(log.find("renderer-type-dump votes")!=std::string::npos&&log.find("renderer-type-dump votes")==log.rfind("renderer-type-dump votes")&&log.find("find-disabled")!=std::string::npos&&log.find("element2 ")!=std::string::npos);
  blind.shutdown();assert(f->handles.empty());}
 // 15. A game update gave the character's mesh more vertices (same renderer, mesh name and bones): the part follows the new
 //     mesh by itself. Another mesh name (an outfit) is reported instead of guessed.
 {std::string media4=root+"/media4",shared4=media4+"/"+pkg+"/GIBox",models4=shared4+"/models";writeModel(models4+"/A",Spec().withTarget(fixture::target("Body")));
  std::filesystem::create_directories(shared4);write(shared4+"/control.json","{\"enabled\":true,\"selected\":[\"A\"],\"cycle\":0,\"exportBones\":0}");
  Ptr updated=f->mesh("Body",5);Ptr world5=f->renderer(updated);f->world(world5);
  Runtime upd(e,pkg,root+"/private4",media4,false);upd.tick(400000);upd.tick(400300);auto st=JsonParser(text(shared4+"/status.json")).parse();
  assert(st.at("state").string=="active"&&meshOf(world5)!=updated&&learnedAt(models4+"/A").targets.at("Part0").vertices==5&&text(shared4+"/GIBox Log.txt").find("target-updated A/Part0: Body vertices 3->5")!=std::string::npos);
  store<Ptr>(world5+0x10,0);Ptr outfit=f->renderer(f->mesh("BodyCostume",4));f->world(outfit);upd.tick(400600);st=JsonParser(text(shared4+"/status.json")).parse();
  assert(st.at("state").string=="waiting"&&st.at("detail").string.find("和导入时不同")!=std::string::npos&&meshOf(outfit)==f->obj[outfit].mesh&&learnedAt(models4+"/A").targets.at("Part0").vertices==5);
  upd.shutdown();assert(f->handles.empty());}
 // 16. Manual import for a chosen character without its skeleton library: ID plus model name (model.json "character"). The part is learned
 //     from that character's skeleton while another one is on the field (party / detail page) and the ID stays. Imported
 //     again for another character (new generation), what was learned for the old one is forgotten. Log lines follow the
 //     XXMI Launcher format, and control.json "logLevel" filters them.
 f->linkArrays(true);
 {std::string media5=root+"/media5",shared5=media5+"/"+pkg+"/GIBox",models5=shared5+"/models",logFile=shared5+"/GIBox Log.txt";
  writeModel(models5+"/K",Spec().character(10000081,{"kaveh"}).noSkeleton());
  std::filesystem::create_directories(shared5);write(shared5+"/control.json","{\"enabled\":true,\"selected\":[\"K\"],\"cycle\":0,\"exportBones\":0}");
  Ptr world6=f->renderer(original);f->world(world6);Ptr kaveh=f->renderer(f->mesh("KavehBody"),"BoneA","Body",f->kavehRoot),kavehMesh=f->obj[kaveh].mesh;
  // An NPC whose skeleton chain carries the same name but is not a playable character is never learned from.
  Ptr npcRoot=f->object("transform","NPC_Kaveh_Statue");Ptr statue=f->renderer(f->mesh("StatueBody"),"BoneA","Body",npcRoot),statueMesh=f->obj[statue].mesh;
  f->scene={statue,world6,kaveh};
  Runtime idm(e,pkg,root+"/private5",media5,false);idm.tick(500000);idm.tick(500600);
  auto log=text(logFile);
  assert(meshOf(kaveh)!=kavehMesh&&meshOf(statue)==statueMesh&&meshOf(world6)==original&&readSummary(models5+"/K").id==10000081&&learnedAt(models5+"/K").id==0&&learnedAt(models5+"/K").targets.at("Part0").mesh=="KavehBody");
  assert(log.find("runtime INFO target-learned K/Part0 by name kaveh (KavehBody, 3 vertices)")!=std::string::npos);
  {auto first=log.substr(0,log.find('\n'));auto digits=[&](size_t at,size_t n){for(size_t i=at;i<at+n;i++)if(first.size()<=i||!std::isdigit(static_cast<unsigned char>(first[i])))return false;return true;};
   assert(digits(0,4)&&first[4]=='-'&&digits(5,2)&&first[7]=='-'&&digits(8,2)&&first[10]==' '&&digits(11,2)&&first[13]==':'&&digits(14,2)&&first[16]==':'&&digits(17,2)&&first[19]==','&&digits(20,3)&&first[23]==' '&&digits(24,6)&&first.compare(30,9," runtime ")==0);
   for(size_t p=0;(p=log.find('\n',p))!=std::string::npos&&p+1<log.size();p++)if(log[p+1]!=' ')assert(log.compare(p+1+24,6,first,24,6)==0);}  // one instance id per process
  // (the converter drops learned.json when a folder is converted for another character)
  std::filesystem::remove(models5+"/K/learned.json");writeModel(models5+"/K",Spec().character(10000107,{"citlali"}).noSkeleton().gen("2"));
  idm.tick(501700);idm.tick(502300);
  assert(meshOf(kaveh)==kavehMesh&&meshOf(world6)!=original&&learnedAt(models5+"/K").targets.at("Part0").mesh=="Body");
  // logLevel warning: INFO lines (disabled, model-released) are dropped, warnings still pass.
  write(shared5+"/control.json","{\"enabled\":false,\"selected\":[\"K\"],\"cycle\":0,\"exportBones\":0,\"logLevel\":\"warning\"}");auto size=text(logFile).size();idm.tick(503400);
  assert(meshOf(world6)==original&&text(logFile).size()==size);
  {std::thread other([&]{idm.bindMesh(load<Ptr>(world6+0x10),load<Ptr>(original+0x10));});other.join();}
  assert(text(logFile).find("runtime WARNING hook-skip",size)!=std::string::npos);
  idm.shutdown();assert(f->handles.empty());}
 // 17. The skeleton library while the character is still loading (no renderers yet): no error, recorded once they are
 //     there. A renderer whose bindposes do not match its bones is reported with the reason, once.
 {std::string media6=root+"/media6",shared6=media6+"/"+pkg+"/GIBox",logFile=shared6+"/GIBox Log.txt";std::filesystem::create_directories(shared6);
  write(shared6+"/control.json","{\"enabled\":false,\"selected\":[]}");f->world(0);
  Runtime lib(e,pkg,root+"/private6",media6,false);lib.tick(600000);lib.tick(600500);
  assert(!exists(shared6+"/skeletons/10000107/skeleton.json")&&(!exists(logFile)||text(logFile).find("bone-library-error")==std::string::npos));
  Ptr broken=f->renderer(f->mesh());store<uint64_t>(f->obj[f->obj[broken].mesh].arrays[abi::getBindposes]+0x18,2);f->world(broken);
  lib.tick(601200);lib.tick(612000);lib.tick(623000);auto log=text(logFile);auto at=log.find("bone-library-error 10000107: Incomplete skeleton: 1 renderers, 0 still loading, 1 with bone/bindpose counts that differ");
  assert(at!=std::string::npos&&log.find("bone-library-error",at+1)==std::string::npos&&log.find("Body 1 bones / 2 bindposes")!=std::string::npos);
  Ptr good=f->renderer(original);f->world(good);lib.tick(634000);lib.tick(634500);assert(exists(shared6+"/skeletons/10000107/skeleton.json"));lib.shutdown();assert(f->handles.empty());}
 // 18. Why a model waits: before the character is known the scan has not started; then the same mesh at another level of
 //     detail is named with the number that differs (status and log); once LOD0 shows, it attaches.
 {std::string media7=root+"/media7",shared7=media7+"/"+pkg+"/GIBox",models7=shared7+"/models",logFile=shared7+"/GIBox Log.txt";writeModel(models7+"/A",Spec().noSkeleton().withTarget(fixture::target("Body")));
  std::filesystem::create_directories(shared7);write(shared7+"/control.json","{\"enabled\":true,\"selected\":[\"A\"]}");
  Ptr lod=f->renderer(f->mesh("Body_LOD1",2));f->world(lod);f->scene={lod};store<uint32_t>(f->manager+0x288,0);
  Runtime why(e,pkg,root+"/private7",media7,false);why.tick(700000);auto st=JsonParser(text(shared7+"/status.json")).parse();
  assert(st.at("state").string=="waiting"&&st.at("detail").string.find("还没认出当前操控的角色")!=std::string::npos&&st.at("detail").string.find("任一处")==std::string::npos);
  store<uint32_t>(f->manager+0x288,7);why.tick(700600);st=JsonParser(text(shared7+"/status.json")).parse();auto log=text(logFile);
  assert(st.at("state").string=="waiting"&&st.at("detail").string.find("和导入时不同（顶点 3→2）")!=std::string::npos&&meshOf(lod)==f->obj[lod].mesh);
  assert(log.find("runtime INFO character none")!=std::string::npos&&log.find("runtime INFO character 10000107")!=std::string::npos);
  assert(log.find("wait-reason A: 当前角色 10000107，它的渲染器 1 个，全场景 1 个")!=std::string::npos&&log.find("Part0 要 Body / Body（顶点 3，子网格 1，骨骼 1）：同名 Mesh 数字不同 Body / Body_LOD1（顶点 2，子网格 1，骨骼 1）")!=std::string::npos);
  Ptr near=f->renderer(original);f->world(near);f->scene={lod,near};why.tick(701200);why.tick(701800);
  assert(JsonParser(text(shared7+"/status.json")).parse().at("state").string=="active"&&meshOf(near)!=original);why.shutdown();assert(f->handles.empty());}
 // 19. Lower levels of detail. Right after loading the character shows one (Body_LOD3): the skeleton library waits and
 //     records the full mesh. A model converted against a lower LOD is not built, also when that LOD is on screen, and
 //     says it needs re-converting. A bone-export count already in control.json at start-up is not a request.
 {std::string media8=root+"/media8",shared8=media8+"/"+pkg+"/GIBox",models8=shared8+"/models",bones8=shared8+"/skeletons";writeModel(models8+"/L",Spec().withTarget(fixture::target("Body_LOD3")));
  std::filesystem::create_directories(shared8);write(shared8+"/control.json","{\"enabled\":true,\"selected\":[\"L\"],\"exportBones\":3}");
  Ptr low=f->mesh("Body_LOD3");Ptr w=f->renderer(low);f->world(w);f->scene={w};
  {Runtime lod(e,pkg,root+"/private8",media8,false);lod.tick(800000);lod.tick(800600);auto st=JsonParser(text(shared8+"/status.json")).parse();
   assert(!exists(bones8+"/10000107/skeleton.json")&&meshOf(w)==low&&st.at("state").string=="error"&&st.at("detail").string.find("低精度模型 Body_LOD3")!=std::string::npos);
   f->obj[w].mesh=original;lod.tick(802000);lod.tick(802600);auto lib=text(bones8+"/10000107/skeleton.json");
   assert(lib.find("\"mesh\":\"Body\"")!=std::string::npos&&lib.find("LOD")==std::string::npos);lod.shutdown();}
  {Runtime again(e,pkg,root+"/private8b",media8,false);again.tick(803000);again.tick(803600);again.shutdown();}
  for(auto& d:std::filesystem::directory_iterator(bones8))assert(d.path().filename()=="10000107");
  assert(f->handles.empty());}
 // A world character can remain at LOD1. Its active full preview supplies the actual full mesh/palette, with every
 // required part in one avatar instance. Wrong avatars, loaded assets, duplicate groups and split instances do not.
 {auto mediaFull=root+"/full-capture-media",sharedFull=mediaFull+"/"+pkg+"/GIBox",file=sharedFull+"/skeletons/10000107/skeleton.json";
  SkeletonRenderer saved;saved.name="Body";saved.target={"Body","Body",12022,1,1};saved.bones={"BoneA"};saved.parents={-1};saved.bindposes.resize(64);for(int i=0;i<4;i++){float one=1;std::memcpy(saved.bindposes.data()+i*20,&one,4);}auto old=skeletonJson(10000107,"OldCapture",{saved});std::filesystem::create_directories(std::filesystem::path(file).parent_path());write(file,old);
  write(sharedFull+"/control.json","{\"enabled\":false,\"selected\":[],\"refreshBones\":1,\"refreshBonesCharacter\":10000107,\"logLevel\":\"debug\"}");
  Ptr low=f->mesh("Body_LOD1",6594),body=f->renderer(low),face=f->renderer(f->mesh("Face_LOD1"),"BoneA","Face");Ptr worldParts[]={body,face};store<Ptr>(f->owner+0x68,f->array(8,2,worldParts));
  Ptr full=f->mesh("Body",12022),fullFace=f->mesh("Face"),wrong=f->renderer(full,"BoneA","Body",f->kavehRoot);
  Ptr previewRoot=f->object("transform","Avatar_Girl_Catalyst_Citlali_Manekin"),splitRoot=f->object("transform","Avatar_Girl_Catalyst_Citlali_Manekin");
  Ptr previewBody=f->renderer(full,"BoneA","Body",previewRoot),previewFace=f->renderer(fullFace,"BoneA","Face",previewRoot),splitFace=f->renderer(fullFace,"BoneA","Face",splitRoot);
  f->assets={previewBody,previewFace};f->scene={body,face,wrong};auto assetCalls=f->findAllCalls;
  Runtime capture(e,pkg,root+"/privateFullCapture",mediaFull,false);auto st=[&]{return JsonParser(text(sharedFull+"/status.json")).parse();};
  capture.tick(810000);capture.tick(810500);capture.tick(811000);assert(text(file)==old&&st().at("boneRefreshState").string=="waiting-full-mesh");
  assert(st().at("boneRefreshDetail").string.find("Body_LOD1")!=std::string::npos&&st().at("boneRefreshDetail").string.find("详情")!=std::string::npos);
  auto log=text(sharedFull+"/GIBox Log.txt");assert(log.find("bone-library-wait")==log.rfind("bone-library-wait"));
  f->scene={body,face,wrong,previewBody,splitFace};capture.tick(812000);assert(text(file)==old); // no joining roots
  f->scene={body,face,previewBody,previewFace,splitFace,f->renderer(full,"BoneA","Body",splitRoot)};capture.tick(814000);assert(text(file)==old); // ambiguous complete groups
  f->scene={body,face,previewBody,previewFace};Ptr binds=f->obj[full].arrays[abi::getBindposes];float nan=std::numeric_limits<float>::quiet_NaN();store<float>(binds+0x20,nan);capture.tick(816000);assert(text(file)==old);
  store<float>(binds+0x20,1.f);capture.tick(818000);capture.tick(818500);assert(text(file)==old); // two healthy full captures required
  capture.tick(820000);assert(st().at("boneRefreshState").string=="saving"&&text(file)!=old);capture.tick(820100);assert(st().at("boneRefreshState").string=="complete");
  auto records=readSkeleton(file);assert(records.size()==2&&records[0].target.mesh=="Body"&&records[0].target.vertices==12022&&records[1].target.mesh=="Face");
  assert(text(file).find("LOD")==std::string::npos&&text(sharedFull+"/GIBox Log.txt").find("bone-library-source 10000107: active same-avatar full model instead of Body / Body_LOD1")!=std::string::npos);
  auto searches=f->findCalls;capture.tick(822000);capture.tick(830000);assert(f->findCalls==searches&&f->findAllCalls==assetCalls&&meshOf(body)==low);
  capture.shutdown();assert(f->handles.empty());f->assets.clear();f->world(world);f->scene={world};}
 // 20. A version 2 model: draws carry their conditions. The default shows one draw; $halo=3 (with $legs at its default 1)
 //     adds the second in place; a value the model does not list keeps the default. Both draws use Material0 in slot 0,
 //     so the second joins the first's submesh (3 indices -> 6) instead of adding one.
 {std::string media9=root+"/media9",shared9=media9+"/"+pkg+"/GIBox",models9=shared9+"/models";
  {Spec w;w.version=2;w.withTarget(fixture::target("Body"));w.variants="{\"vars\":[{\"name\":\"$halo\",\"values\":[0,1,3]},{\"name\":\"$legs\",\"values\":[0,1]}],\"default\":{\"$halo\":0,\"$legs\":1}}";
   w.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0},{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0,\"when\":{\"vars\":[\"$halo\",\"$legs\"],\"allow\":[[3,1]]}}]"};writeModel(models9+"/W",w);}
  std::filesystem::create_directories(shared9);auto ctl9=[&](const char* vars){write(shared9+"/control.json.tmp",std::string("{\"enabled\":true,\"selected\":[\"W\"],\"variables\":")+vars+"}");std::filesystem::rename(shared9+"/control.json.tmp",shared9+"/control.json");};
  ctl9("{}");Ptr w9=f->renderer(original);f->world(w9);f->scene={w9};
  Runtime v2(e,pkg,root+"/private9",media9,false);v2.tick(900000);v2.tick(900600);Ptr ours=meshOf(w9);auto subs=[&]{return load<uint32_t>(f->obj[ours].shared+0xc0);};
  auto drawn=[&]{return f->submeshIndices.at({ours,0}).size();};
  assert(ours!=original&&subs()==1&&drawn()==3);
  ctl9("{\"W\":{\"$halo\":3}}");v2.tick(901200);v2.tick(901800);assert(meshOf(w9)==ours&&subs()==1&&drawn()==6);
  ctl9("{\"W\":{\"$halo\":7}}");v2.tick(902400);v2.tick(903000);assert(meshOf(w9)==ours&&subs()==1&&drawn()==3);
  // A renderer refusing the new materials puts the whole switch back (the next try goes through).
  f->failMaterials=true;ctl9("{\"W\":{\"$halo\":3}}");for(uint64_t t=903600;f->failMaterials&&t<910000;t+=600)v2.tick(t);
  assert(!f->failMaterials&&subs()==1&&drawn()==3&&meshOf(w9)==ours);v2.tick(911000);assert(subs()==1&&drawn()==6);
  v2.shutdown();assert(meshOf(w9)==original&&f->handles.empty());}
 // 21. A set nobody wants any more stays built for a quick switch back, and is released after a minute unused.
 {std::string media10=root+"/media10",shared10=media10+"/"+pkg+"/GIBox",models10=shared10+"/models",logFile=shared10+"/GIBox Log.txt";writeModel(models10+"/A",Spec().withTarget(fixture::target("Body")));
  std::filesystem::create_directories(shared10);auto ctl10=[&](const char* sel){write(shared10+"/control.json.tmp",std::string("{\"enabled\":true,\"selected\":")+sel+"}");std::filesystem::rename(shared10+"/control.json.tmp",shared10+"/control.json");};
  ctl10("[\"A\"]");Ptr w10=f->renderer(original);f->world(w10);f->scene={w10};
  Runtime idle(e,pkg,root+"/private10",media10,false);idle.tick(1000000);idle.tick(1000600);assert(meshOf(w10)!=original);unsigned built=f->meshClones;
  ctl10("[]");idle.tick(1001200);idle.tick(1001800);assert(meshOf(w10)==original&&text(logFile).find("model-released")==std::string::npos);
  ctl10("[\"A\"]");idle.tick(1002400);idle.tick(1003000);assert(meshOf(w10)!=original&&f->meshClones==built);
  ctl10("[]");idle.tick(1003600);idle.tick(1030000);assert(text(logFile).find("model-released")==std::string::npos);idle.tick(1070000);assert(text(logFile).find("model-released A")!=std::string::npos);
  idle.shutdown();assert(f->handles.empty());}
 // 22. A partially loaded character never produces a partial library/export. A verified library is rechecked after
 //     a new request, and both bad captures and failed writes retry until healthy, then stop all capture reads.
 {std::string media11=root+"/media11",shared11=media11+"/"+pkg+"/GIBox",bones11=shared11+"/skeletons",file=bones11+"/10000107/skeleton.json";
  std::filesystem::create_directories(shared11);auto ctl11=[&](int request){write(shared11+"/control.json","{\"enabled\":false,\"selected\":[],\"exportBones\":"+std::to_string(request)+"}");};ctl11(0);
  Ptr bm=f->mesh(),fm=f->mesh("Face"),body=f->renderer(bm),face=f->renderer(fm,"FaceBone","Face");Ptr rs[]={body,face};store<Ptr>(f->owner+0x68,f->array(8,2,rs));
  Ptr faceBinds=f->obj[fm].arrays[abi::getBindposes];store<uint64_t>(faceBinds+0x18,2);
  Runtime complete(e,pkg,root+"/private11",media11,false);
  for(uint64_t t=1100000;t<=1103000;t+=500)complete.tick(t);
  assert(!exists(file)); // Body is good, but Face is not: no successful subset is published.
  ctl11(1);complete.tick(1103500);assert(!exists(file));
  store<uint64_t>(faceBinds+0x18,1);complete.tick(1104000);assert(!exists(file));complete.tick(1104500);complete.tick(1105000);
  auto healthy=text(file);assert(JsonParser(healthy).parse().at("renderers").array.size()==2);
  auto reads=f->bindposeReads;complete.tick(1106000);complete.tick(1107000);assert(f->bindposeReads==reads);
  bool exported=false;for(auto& d:std::filesystem::directory_iterator(bones11))if(d.path().filename().string().rfind("10000107-",0)==0){assert(JsonParser(text(d.path().string()+"/skeleton.json")).parse().at("renderers").array.size()==2);exported=true;}assert(exported);
  // Same renderer objects, a changed skeleton: two bones instead of one; an invalid matrix must keep the good record.
  Ptr ba=f->obj[body].bones,b1=load<Ptr>(ba+0x20),b2=f->object("bone","BoneB");f->obj[b2].parent=b1;Ptr bs[]={b1,b2};f->obj[body].bones=f->array(8,2,bs);
  Bytes bind(128);for(int n=0;n<2;n++)for(int j=0;j<4;j++){float one=1;std::memcpy(bind.data()+n*64+j*20,&one,4);}
  Ptr bindArray=f->array(64,2,bind.data());f->obj[bm].arrays[abi::getBindposes]=bindArray;store<uint64_t>(f->obj[bm].shared+0xe0,2);
  ctl11(2);store<float>(bindArray+0x20,std::numeric_limits<float>::quiet_NaN());complete.tick(1108000);complete.tick(1108500);assert(text(file)==healthy);
  store<float>(bindArray+0x20,1);complete.tick(1109000);assert(text(file)==healthy);complete.tick(1109500);complete.tick(1110000);
  auto updated=text(file);assert(updated!=healthy&&JsonParser(updated).parse().at("renderers").array[0].at("bones").array.size()==2);
  // A failed write is not marked complete; preserve the old record and recover automatically when storage works.
  ctl11(3);std::filesystem::create_directory(file+".tmp");f->obj[b2].name="UpdatedBoneB";complete.tick(1113000);complete.tick(1113500);complete.tick(1114000);assert(text(file)==updated);
  std::filesystem::remove_all(file+".tmp");complete.tick(1114500);complete.tick(1115000);assert(text(file).find("UpdatedBoneB")!=std::string::npos);
  reads=f->bindposeReads;complete.tick(1116000);complete.tick(1120000);assert(f->bindposeReads==reads);
  complete.shutdown();assert(f->handles.empty());}
 // 23. Preserve the 0.7.5 exit: a persistent structural defect may be excluded after 30 s, with one partial warning.
 {std::string mediaPartial=root+"/mediaPartial",sharedPartial=mediaPartial+"/"+pkg+"/GIBox",bonesPartial=sharedPartial+"/skeletons",logFile=sharedPartial+"/GIBox Log.txt";std::filesystem::create_directories(sharedPartial);
  write(sharedPartial+"/control.json","{\"enabled\":false,\"selected\":[],\"exportBones\":0}");
  Ptr good=f->renderer(original),broken=f->renderer(f->mesh(),"BoneA","Cloak");store<uint64_t>(f->obj[f->obj[broken].mesh].arrays[abi::getBindposes]+0x18,2);
  store<Ptr>(f->owner+0x68,f->array(8,2,std::vector<Ptr>{good,broken}.data()));
  Runtime part(e,pkg,root+"/privatePartial",mediaPartial,false);uint64_t t=1100000;
  for(;t<1100000+20000;t+=500){part.tick(t);}assert(!exists(bonesPartial+"/10000107/skeleton.json"));
  for(;t<1100000+40000;t+=500){part.tick(t);}auto lib=text(bonesPartial+"/10000107/skeleton.json");auto log=text(logFile);
  assert(lib.find("\"name\":\"Body\"")!=std::string::npos&&lib.find("Cloak")==std::string::npos&&log.find("runtime WARNING bone-library-partial 10000107: recording the complete renderers without Cloak")!=std::string::npos);
  assert(log.find("bone-library-partial")==log.rfind("bone-library-partial"));auto reads=f->bindposeReads;part.tick(t+1000);part.tick(t+10000);assert(f->bindposeReads==reads);
  part.shutdown();assert(f->handles.empty());}
 // 24. Default-off geometry leaves the original visible. Enabling builds/attaches it; disabling restores both mesh and
 //     materials. The inactive model never takes precedence over a second enabled model on the same part.
 {std::string mediaGate=root+"/mediaGate",sharedGate=mediaGate+"/"+pkg+"/GIBox",modelsGate=sharedGate+"/models";
  Spec g;g.version=3;g.withTarget(fixture::target("Body"));g.variants="{\"vars\":[{\"name\":\"$vision\",\"values\":[0,1]}],\"default\":{\"$vision\":1}}";
  g.edit=[](std::string& j){fixture::replaceIn(j,"\"blob\"","\"replaceWhen\":{\"vars\":[\"$vision\"],\"allow\":[[0]]},\"blob\"");};writeModel(modelsGate+"/G",g);writeModel(modelsGate+"/Other");
  Ptr w=f->renderer(original),mat=f->obj[w].materials;f->world(w);f->scene={w};auto built=f->meshClones;
  auto gateControl=[&](std::string selected,std::string vars){write(sharedGate+"/control.json","{\"enabled\":true,\"selected\":"+selected+",\"variables\":"+vars+"}");};
  gateControl("[\"G\"]","{}");Runtime gate(e,pkg,root+"/privateGate",mediaGate,false);gate.tick(1300000);gate.tick(1300600);
  assert(meshOf(w)==original&&f->obj[w].materials==mat&&f->meshClones==built);
  assert(JsonParser(text(sharedGate+"/status.json")).parse().at("detail").string.find("显示替换已关闭")!=std::string::npos);
  gateControl("[\"G\"]","{\"G\":{\"$vision\":0}}");gate.tick(1301200);gate.tick(1301800);Ptr replaced=meshOf(w);assert(replaced!=original&&f->obj[w].materials!=mat&&f->meshClones==built+2);
  gateControl("[\"G\"]","{\"G\":{\"$vision\":1}}");gate.tick(1302400);assert(meshOf(w)==original&&f->obj[w].materials==mat);
  gateControl("[\"G\",\"Other\"]","{}");gate.tick(1303000);gate.tick(1303600);assert(meshOf(w)!=original&&meshOf(w)!=replaced&&JsonParser(text(sharedGate+"/status.json")).parse().at("models").array[0].string=="Other");
  gate.shutdown();assert(f->handles.empty());}
 // 25. Index-only MOD needs two bones, but cached targets and this character's template have one. Do not build, latch a
 //     failure, or learn another character's larger Body mesh. The SAME entity can finish loading later and be retried.
 {std::string mediaMin=root+"/mediaMin",sharedMin=mediaMin+"/"+pkg+"/GIBox",modelsMin=sharedMin+"/models",dir=modelsMin+"/Indexed";
  Spec s;s.noSkeleton().withTarget(fixture::target("Body"));s.edit=[](std::string& j){fixture::replaceIn(j,"\"bones\":1,\"vertices\"","\"bones\":2,\"vertices\"");};for(int i=0;i<3;i++){put32(s.weights.data()+32*i+16,1);}writeModel(dir,s);
  Learned stale;stale.targets["Part0"]={"Body","Body",3,1,1};write(dir+"/learned.json",learnedJson(stale));
  auto twoBones=[&](Ptr r){auto mesh=f->obj[r].mesh;Ptr b1=load<Ptr>(f->obj[r].bones+0x20),b2=f->object("bone","BoneB");f->obj[b2].parent=b1;Ptr bs[]={b1,b2};f->obj[r].bones=f->array(8,2,bs);Bytes binds(128);for(int n=0;n<2;n++)for(int j=0;j<4;j++)store<float>(reinterpret_cast<Ptr>(binds.data())+n*64+j*20,1);f->obj[mesh].arrays[abi::getBindposes]=f->array(64,2,binds.data());store<uint64_t>(f->obj[mesh].shared+0xe0,2);};
  Ptr mesh=f->mesh(),w=f->renderer(mesh),foreign=f->renderer(f->mesh("Body",4),"BoneA","Body",f->kavehRoot);twoBones(foreign);f->world(w);f->scene={w,foreign};auto foreignMesh=meshOf(foreign);auto built=f->meshClones;
  write(sharedMin+"/control.json","{\"enabled\":true,\"selected\":[\"Indexed\"]}");Runtime min(e,pkg,root+"/privateMin",mediaMin,false);
  for(uint64_t t=1400000;t<=1403000;t+=500){min.tick(t);}auto st=JsonParser(text(sharedMin+"/status.json")).parse();
  assert(st.at("state").string=="waiting"&&st.at("detail").string.find("至少 2 根骨骼")!=std::string::npos&&meshOf(w)==mesh&&meshOf(foreign)==foreignMesh&&f->meshClones==built);
  auto log=text(sharedMin+"/GIBox Log.txt");assert(log.find("build-retry")==std::string::npos&&log.find("spawn-build-error")==std::string::npos);
  twoBones(w);for(uint64_t t=1403500;t<=1406000;t+=500){min.tick(t);}
  assert(meshOf(w)!=mesh&&meshOf(foreign)==foreignMesh&&learnedAt(dir).targets.at("Part0").bones==2&&f->meshClones==built+2);
  min.shutdown();assert(f->handles.empty());}
 // 26. A part whose renderer is not on the character is looked for again every scan, but logged once.
 {std::string mediaMiss=root+"/mediaMiss",sharedMiss=mediaMiss+"/"+pkg+"/GIBox";Spec s;s.edit=[](std::string& j){fixture::replaceIn(j,"\"renderer\":\"Body\"","\"renderer\":\"Cloak\"");};writeModel(sharedMiss+"/models/Miss",s);
  Ptr w=f->renderer(original);f->world(w);f->scene={w};write(sharedMiss+"/control.json","{\"enabled\":true,\"selected\":[\"Miss\"]}");
  Runtime miss(e,pkg,root+"/privateMiss",mediaMiss,false);for(uint64_t t=1500000;t<=1505000;t+=500){miss.tick(t);}
  auto log=text(sharedMiss+"/GIBox Log.txt");auto at=log.find("target-missing Miss/Part0: renderer match missing: Cloak");assert(at!=std::string::npos&&log.find("target-missing",at+1)==std::string::npos&&meshOf(w)==original);
  miss.shutdown();assert(f->handles.empty());}
 // 27. Phone mesh with fewer materials than the mod's parts (Shenhe: 4 parts, 3 submeshes): the extra slot borrows the
 //     last material's shader. Clones are taken again after the switch-in effect (twice), adopted copies follow.
 //     A built model goes onto the controlled character's lower-LOD mesh of the same asset when the bones are the same.
 {std::string mediaSlot=root+"/mediaSlot",sharedSlot=mediaSlot+"/"+pkg+"/GIBox";Spec s;s.withTarget(fixture::target("Body"));s.textures=true;
  s.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0},{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":1}]"};writeModel(sharedSlot+"/models/Slot",s);
  Ptr full=f->mesh(),w=f->renderer(full);f->world(w);f->scene={w};write(sharedSlot+"/control.json","{\"enabled\":true,\"selected\":[\"Slot\"]}");
  Runtime slot(e,pkg,root+"/privateSlot",mediaSlot,false);slot.tick(1600000);slot.tick(1600600);slot.tick(1601200);
  assert(meshOf(w)!=full);Ptr first=f->obj[w].materials;assert(load<uint64_t>(first+0x18)==2);Ptr c0=load<Ptr>(first+0x20),c1=load<Ptr>(first+0x28);assert(c0!=c1&&alive(c0)&&alive(c1));
  Ptr copy=f->instantiate(w);f->scene={w,copy};slot.tick(1601800);assert(meshOf(copy)==meshOf(w)&&f->obj[copy].materials==first);
  slot.tick(1602800);Ptr second=f->obj[w].materials;assert(second!=first&&load<Ptr>(second+0x20)!=c0&&load<Ptr>(second+0x28)!=c1&&alive(c0));
  slot.tick(1605400);assert(f->obj[w].materials!=second);auto third=f->obj[w].materials;assert(load<Ptr>(f->obj[copy].materials+0x20)==load<Ptr>(third+0x20));
  slot.tick(1609000);assert(f->obj[w].materials==third);
  auto log=text(sharedSlot+"/GIBox Log.txt");assert(log.find("material-slot-borrowed Slot/Part0: slot 1 has no game material")!=std::string::npos&&log.find("material-slot-borrowed")==log.rfind("material-slot-borrowed"));
  // The same character now shows only Body_LOD1 (same bone names): ours goes on, and the LOD comes back on restore.
  Ptr lodMesh=f->mesh("Body_LOD1",2),lod=f->renderer(lodMesh);f->world(lod);f->scene={w,copy,lod};slot.tick(1609600);slot.tick(1610200);
  assert(meshOf(lod)==meshOf(w)&&text(sharedSlot+"/GIBox Log.txt").find("attached-lod Slot/Part0 over Body_LOD1")!=std::string::npos);
  Ptr other=f->renderer(f->mesh("Body_LOD1",2),"BoneZ");f->scene={w,copy,lod,other};f->world(other);slot.tick(1610800);slot.tick(1611400);assert(meshOf(other)!=meshOf(w));
  write(sharedSlot+"/control.json","{\"enabled\":false,\"selected\":[\"Slot\"]}");slot.tick(1612000);assert(meshOf(lod)==lodMesh&&meshOf(w)==full);
  slot.shutdown();assert(f->handles.empty());}
 // 28. Only Body_LOD1 in sight from the start: built on it when its bones and bindposes are exactly the bone library's
 //     record of the full Body; without that record it waits as before.
 {std::string mediaLod=root+"/mediaLod",sharedLod=mediaLod+"/"+pkg+"/GIBox";Spec s;s.noSkeleton().withTarget(fixture::target("Body"));writeModel(sharedLod+"/models/Lod",s);
  Ptr lodMesh=f->mesh("Body_LOD1",2),lod=f->renderer(lodMesh);f->world(lod);f->scene={lod};write(sharedLod+"/control.json","{\"enabled\":true,\"selected\":[\"Lod\"]}");
  Runtime first(e,pkg,root+"/privateLod",mediaLod,false);for(uint64_t t=1700000;t<=1702000;t+=500){first.tick(t);}assert(meshOf(lod)==lodMesh);first.shutdown();
  SkeletonRenderer rec;rec.name="Body";rec.target=fingerprintOf(JsonParser(fixture::target("Body")).parse());rec.bones={"BoneA"};rec.parents={-1};
  rec.bindposes.assign(reinterpret_cast<const uint8_t*>(f->obj[lodMesh].arrays[abi::getBindposes]+0x20),reinterpret_cast<const uint8_t*>(f->obj[lodMesh].arrays[abi::getBindposes]+0x20+64));
  std::filesystem::create_directories(sharedLod+"/skeletons/10000107");write(sharedLod+"/skeletons/10000107/skeleton.json",skeletonJson(10000107,"Scene/Avatar_Girl_Catalyst_Citlali",{rec}));
  Runtime second(e,pkg,root+"/privateLod2",mediaLod,false);for(uint64_t t=1710000;t<=1713000;t+=500){second.tick(t);}
  auto log=text(sharedLod+"/GIBox Log.txt");assert(meshOf(lod)!=lodMesh&&log.find("template-lod Lod: Body only shows Body_LOD1")!=std::string::npos&&log.find("attached-lod Lod/Part0 over Body_LOD1")!=std::string::npos);
  second.shutdown();assert(meshOf(lod)==lodMesh&&f->handles.empty());}
 // A converted legacy mod already contains the same full skeleton proof as the library. The checked preload is
 // enough when that separate file is missing; a same-sized palette with different names or bindposes is not.
 {auto mediaE=root+"/embedded-lod-media",sharedE=mediaE+"/"+pkg+"/GIBox";Spec source;source.withTarget(fixture::target("Body"));writeModel(sharedE+"/models/Legacy",source);
  write(sharedE+"/control.json","{\"enabled\":true,\"selected\":[\"Legacy\"],\"experiments\":{\"animationMode\":2}}");
  Ptr low=f->mesh("Body_LOD1",2),renderer=f->renderer(low);f->world(renderer);f->scene={renderer};
  Runtime saved(e,pkg,root+"/embedded-lod-private",mediaE,false);for(uint64_t t=1720000;t<=1723000;t+=500)saved.tick(t);
  assert(meshOf(renderer)!=low&&!exists(sharedE+"/skeletons/10000107/skeleton.json")&&text(sharedE+"/GIBox Log.txt").find("bones and bindposes equal the model's saved Body")!=std::string::npos);
  saved.shutdown();assert(meshOf(renderer)==low&&f->handles.empty());
  Ptr wrong=f->renderer(f->mesh("Body_LOD1",2),"BoneZ");f->world(wrong);f->scene={wrong};auto wrongMesh=meshOf(wrong);unsigned clones=f->meshClones;
  {Runtime rejected(e,pkg,root+"/embedded-lod-wrong-name",mediaE,false);for(uint64_t t=1730000;t<=1732000;t+=500)rejected.tick(t);assert(meshOf(wrong)==wrongMesh&&f->meshClones==clones);rejected.shutdown();}
  Ptr movedMesh=f->mesh("Body_LOD1",2),moved=f->renderer(movedMesh);auto bind=f->obj[movedMesh].arrays[abi::getBindposes];store<float>(bind+0x20,2.f);f->world(moved);f->scene={moved};
  {Runtime rejected(e,pkg,root+"/embedded-lod-wrong-binds",mediaE,false);for(uint64_t t=1740000;t<=1742000;t+=500)rejected.tick(t);assert(meshOf(moved)==movedMesh&&f->meshClones==clones);rejected.shutdown();}assert(f->handles.empty());}
 // Historical index-only builds left a partial successful bonemap, not an embedded skeleton. Check all USED rows,
 // allow unmapped UNUSED rows, wait for a stable live palette, and never apply this recovery to an animation.
 {auto mediaL=root+"/legacy-map-media",sharedL=mediaL+"/"+pkg+"/GIBox",dir=sharedL+"/models/Legacy";Spec source;source.noSkeleton().character(10000107,{"citlali"});auto target=fixture::target("Body");fixture::replaceIn(target,"\"bones\":1","\"bones\":2");source.withTarget(target);
  source.edit=[](std::string& j){fixture::replaceIn(j,"\"bones\":1,\"vertices\"","\"bones\":2,\"vertices\"");};writeModel(dir,source);auto file=dir+"/bonemap-Part0.tsv";
  std::string header="source_index\ttarget_index\tsource_name\ttarget_name\n",good=header+"0\t0\t#0\tBoneA\n1\t-1\t#1\t\n";write(file,good);auto names=readLegacyBoneNames(file,2);assert(names.size()==2&&names[0]=="BoneA"&&names[1].empty());
  Ptr low=f->mesh("Body_LOD1",2),r=f->renderer(low),bone0=load<Ptr>(f->obj[r].bones+0x20),unused=f->object("bone","UnusedBone");f->obj[unused].parent=f->skeletonRoot;Ptr bones[]={bone0,unused};f->obj[r].bones=f->array(8,2,bones);Bytes matrices(128);for(unsigned n=0;n<2;n++)for(unsigned k=0;k<4;k++)store<float>(reinterpret_cast<Ptr>(matrices.data())+n*64+k*20,1.f);
  auto binds=f->array(64,2,matrices.data());f->obj[low].arrays[abi::getBindposes]=binds;store<uint64_t>(f->obj[low].shared+0xe0,2);f->world(r);f->scene={r};write(sharedL+"/control.json","{\"enabled\":true,\"selected\":[\"Legacy\"]}");
  {Runtime recovered(e,pkg,root+"/legacy-map-private",mediaL,false);recovered.tick(1800000);recovered.tick(1800200);assert(meshOf(r)==low);store<float>(binds+0x20,2.f);recovered.tick(1800400);recovered.tick(1800600);assert(meshOf(r)==low);recovered.tick(1801000);
   assert(meshOf(r)!=low&&text(sharedL+"/GIBox Log.txt").find("matched-used=1")!=std::string::npos&&!exists(sharedL+"/skeletons/10000107/skeleton.json"));recovered.shutdown();assert(meshOf(r)==low&&f->handles.empty());}
  auto rejects=[&](const std::string& priv){unsigned clones=f->meshClones;Runtime rejected(e,pkg,root+priv,mediaL,false);for(uint64_t now=1810000;now<=1812000;now+=200)rejected.tick(now);assert(meshOf(r)==low&&f->meshClones==clones);rejected.shutdown();assert(f->handles.empty());};
  f->obj[bone0].name="WrongUsedBone";rejects("/legacy-map-name-mismatch");f->obj[bone0].name="BoneA";
  write(file,header+"0\t-1\t#0\t\n1\t-1\t#1\t\n");rejects("/legacy-map-used-missing");
  write(file,header+"0\t1\t#0\tBoneA\n1\t-1\t#1\t\n");rejects("/legacy-map-reindexed");
  write(file,good+"0\t0\t#0\tBoneA\n");rejects("/legacy-map-duplicate");
  auto anim=sharedL+"/models/Animated";writeModel(anim,fixture::animated(source));write(anim+"/bonemap-Part0.tsv",good);write(sharedL+"/control.json","{\"enabled\":true,\"selected\":[\"Animated\"]}");rejects("/legacy-map-animation");}
 // 29. A preview's game code instantiates our materials (new pointers, same mod texture). Every kind of off switch
 //     must restore the saved original mesh AND texture. A reclaim must not overwrite the clean material backup.
 for(int mode=0;mode<3;mode++){
  std::string mediaRestore=root+"/mediaRestore"+std::to_string(mode),sharedRestore=mediaRestore+"/"+pkg+"/GIBox";Spec s;s.version=3;s.textures=true;s.withTarget(fixture::target("Body"));
  s.variants="{\"vars\":[{\"name\":\"$show\",\"values\":[0,1]}],\"default\":{\"$show\":1}}";s.edit=[](std::string& j){fixture::replaceIn(j,"\"blob\"","\"replaceWhen\":{\"vars\":[\"$show\"],\"allow\":[[1]]},\"blob\"");};writeModel(sharedRestore+"/models/R",s);
  Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh),preview=f->renderer(originalMesh),saved=f->obj[preview].materials,baseMat=load<Ptr>(saved+0x20),gameTexture=f->object("texture","Game diffuse");f->diffuse[baseMat]=gameTexture;f->world(w);f->scene={w,preview};
  auto ctlRestore=[&](bool enabled,bool selected,int show){write(sharedRestore+"/control.json","{\"enabled\":"+std::string(enabled?"true":"false")+",\"selected\":"+(selected?"[\"R\"]":"[]")+",\"variables\":{\"R\":{\"$show\":"+std::to_string(show)+"}}}");};
  ctlRestore(true,true,1);Runtime paired(e,pkg,root+"/privateRestore"+std::to_string(mode),mediaRestore,false);paired.tick(1800000);paired.tick(1800600);assert(meshOf(preview)!=originalMesh);
  Ptr modMat=load<Ptr>(f->obj[preview].materials+0x20),gameCopy=clone(modMat,0);assert(f->diffuse[gameCopy]!=gameTexture);f->obj[preview].materials=f->typed("material",gameCopy);
  if(mode>0)f->obj[preview].mesh=originalMesh; // game has already put the original geometry back
  if(mode==2){paired.tick(1801200);assert(meshOf(preview)!=originalMesh);} // reclaim sees unfamiliar mod-textured materials
  ctlRestore(mode!=0,mode!=1,mode==2?0:1);paired.tick(1801800);
  assert(meshOf(preview)==originalMesh&&f->obj[preview].materials==saved&&f->diffuse[load<Ptr>(f->obj[preview].materials+0x20)]==gameTexture);
  assert(text(sharedRestore+"/GIBox Log.txt").find("restored R/Part0")!=std::string::npos);paired.shutdown();assert(f->handles.empty());}
 // 30. The game copies our material array while supplying an original mesh. Resolve known clone sources before
 //     taking the backup, so switching off cannot restore mod textures or a clone that its old owner will destroy.
 {std::string mediaAlias=root+"/mediaAlias",sharedAlias=mediaAlias+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));s.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0},{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":1}]"};writeModel(sharedAlias+"/models/Alias",s);
  Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh),saved=f->obj[w].materials,baseMat=load<Ptr>(saved+0x20),gameTexture=f->object("texture","Original diffuse");f->diffuse[baseMat]=gameTexture;f->world(w);f->scene={w};write(sharedAlias+"/control.json","{\"enabled\":true,\"selected\":[\"Alias\"]}");
  Runtime alias(e,pkg,root+"/privateAlias",mediaAlias,false);alias.tick(1900000);alias.tick(1900600);Ptr preview=f->renderer(originalMesh);f->obj[preview].materials=f->obj[w].materials;alias.tick(1901800);f->scene.push_back(preview);Ptr go=f->gameObjectOf({preview});alias.spawned(go);alias.spawnedAfter();
  assert(meshOf(preview)==meshOf(w)&&text(sharedAlias+"/GIBox Log.txt").find("material-backup-cleaned Alias/Part0")!=std::string::npos);
  write(sharedAlias+"/control.json","{\"enabled\":false,\"selected\":[\"Alias\"]}");alias.tick(1902400);assert(meshOf(preview)==originalMesh&&load<uint64_t>(f->obj[preview].materials+0x18)==1&&load<Ptr>(f->obj[preview].materials+0x20)==baseMat&&f->diffuse[baseMat]==gameTexture);alias.shutdown();assert(f->handles.empty());}
 // 31. Destroy the source before the first delayed refresh. Its material heir (and a second descendant) must finish
 //     both refreshes and shed a simulated temporary material parameter, not only keep the old clones alive.
 {std::string mediaHeir=root+"/mediaHeir",sharedHeir=mediaHeir+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedHeir+"/models/Heir",s);
  Ptr originalMesh=f->mesh(),source=f->renderer(originalMesh),saved=f->obj[source].materials,baseMat=load<Ptr>(saved+0x20);f->glow[baseMat]=1;f->world(source);f->scene={source};write(sharedHeir+"/control.json","{\"enabled\":true,\"selected\":[\"Heir\"]}");
  Runtime heir(e,pkg,root+"/privateHeir",mediaHeir,false);heir.tick(2000000);Ptr firstMat=load<Ptr>(f->obj[source].materials+0x20);assert(f->glow[firstMat]==1);
  Ptr child=f->instantiate(source);f->world(child);f->scene={source,child};heir.tick(2000600);store<Ptr>(source+0x10,0);f->scene={child};f->glow[baseMat]=0;heir.tick(2001000);
  Ptr grandchild=f->instantiate(child);f->scene.push_back(grandchild);heir.tick(2001200);assert(load<Ptr>(f->obj[child].materials+0x20)==firstMat);
  heir.tick(2001800);Ptr secondMat=load<Ptr>(f->obj[child].materials+0x20);assert(secondMat!=firstMat&&f->glow[secondMat]==0&&load<Ptr>(f->obj[grandchild].materials+0x20)==secondMat);
  heir.tick(2004400);Ptr thirdMat=load<Ptr>(f->obj[child].materials+0x20);assert(thirdMat!=secondMat&&f->glow[thirdMat]==0&&load<Ptr>(f->obj[grandchild].materials+0x20)==thirdMat);heir.tick(2009000);assert(load<Ptr>(f->obj[child].materials+0x20)==thirdMat);
  auto heirLog=text(sharedHeir+"/GIBox Log.txt");assert(heirLog.find("material-state Heir/Part0 early-sample")!=std::string::npos&&heirLog.find("material-refresh Heir: attempt 1 applied")!=std::string::npos&&heirLog.find("material-refresh Heir: attempt 2 applied")!=std::string::npos);
  heir.shutdown();assert(meshOf(child)==originalMesh&&meshOf(grandchild)==originalMesh&&f->handles.empty());}
 // 32. A present but destroyed game material is a loading/lifetime problem, not an extra PC part. Do not borrow a
 //     different slot's shader for it; wait without changing the original pair, then retry on the SAME renderer.
 {std::string mediaDead=root+"/mediaDead",sharedDead=mediaDead+"/"+pkg+"/GIBox";Spec s;s.withTarget(fixture::target("Body"));s.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0},{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":1}]"};writeModel(sharedDead+"/models/Dead",s);
  Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh),first=load<Ptr>(f->obj[w].materials+0x20),dead=f->object("material","Not ready");store<Ptr>(dead+0x10,0);Ptr ms[]={first,dead};Ptr saved=f->array(8,2,ms);f->obj[w].materials=saved;f->world(w);f->scene={w};write(sharedDead+"/control.json","{\"enabled\":true,\"selected\":[\"Dead\"]}");
  Runtime missing(e,pkg,root+"/privateDead",mediaDead,false);missing.tick(2100000);missing.tick(2100600);auto log=text(sharedDead+"/GIBox Log.txt");assert(meshOf(w)==originalMesh&&f->obj[w].materials==saved&&log.find("material-wait Dead:")!=std::string::npos&&log.find("material-wait")==log.rfind("material-wait")&&log.find("material-slot-borrowed")==std::string::npos);
  assert(JsonParser(text(sharedDead+"/status.json")).parse().at("detail").string.find("原材质槽 1 尚未就绪")!=std::string::npos);
  Ptr ready=f->object("material","Ready");ms[1]=ready;Ptr complete=f->array(8,2,ms);f->obj[w].materials=complete;missing.tick(2101200);assert(meshOf(w)!=originalMesh);
  missing.shutdown();assert(meshOf(w)==originalMesh&&f->obj[w].materials==complete&&f->handles.empty());}
 // 33. A party-effect material copy has a new identity. Bounded observations must report that identity and a skipped
 //     refresh without altering it; the trace cannot claim to observe (or fix) shader parameters/property blocks.
 {std::string mediaTrace=root+"/mediaTrace",sharedTrace=mediaTrace+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedTrace+"/models/Trace",s);
  Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh),saved=f->obj[w].materials;f->world(w);f->scene={w};write(sharedTrace+"/control.json","{\"enabled\":true,\"selected\":[\"Trace\"]}");
  Runtime trace(e,pkg,root+"/privateTrace",mediaTrace,false);trace.tick(2200000);Ptr copy=clone(load<Ptr>(f->obj[w].materials+0x20),0);Ptr copied=f->array(8,1,&copy);f->obj[w].materials=copied;
  trace.tick(2200600);assert(f->obj[w].materials==copied);trace.tick(2201800);assert(f->obj[w].materials==copied);
  auto traceLog=text(sharedTrace+"/GIBox Log.txt");assert(traceLog.find("material-state Trace/Part0 early-sample")!=std::string::npos&&traceLog.find("current=[0=untracked:")!=std::string::npos&&traceLog.find("attempt 1 skipped (current materials are not the owned clones)")!=std::string::npos);
  trace.shutdown();assert(meshOf(w)==originalMesh&&f->obj[w].materials==saved&&f->handles.empty());}
 // 34. Party hand-off instantiates each draw's material (two draws sharing one base slot become two instances).
 //     Repair that recognized palette early, cap early attempts, retain the delayed recovery and original palette.
 //     The two draws are neighbours with the same material, which would share one submesh: separateDraws keeps two.
 {std::string mediaCopy=root+"/mediaCopy",sharedCopy=mediaCopy+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));s.indices={0,1,2,2,1,0};s.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0},{\"start\":3,\"count\":3,\"material\":\"Material0\",\"slot\":0}]"};writeModel(sharedCopy+"/models/Copy",s);
  Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh),saved=f->obj[w].materials,baseMat=load<Ptr>(saved+0x20);f->obj[baseMat].name="Avatar_Mat_Body";f->glow[baseMat]=0;f->world(w);f->scene={w};write(sharedCopy+"/control.json","{\"enabled\":true,\"selected\":[\"Copy\"],\"experiments\":{\"separateDraws\":true}}");
  Runtime copy(e,pkg,root+"/privateCopy",mediaCopy,false);copy.tick(2300000);assert(load<uint64_t>(f->obj[w].materials+0x18)==2);Ptr ourMesh=meshOf(w),texture=f->diffuse[load<Ptr>(f->obj[w].materials+0x20)];std::vector<Ptr> gameCopies;
  auto instantiatePalette=[&]{Ptr list[2];for(unsigned i=0;i<2;i++){list[i]=clone(load<Ptr>(f->obj[w].materials+0x20+8*i),0);f->obj[list[i]].name="Avatar_Mat_Body(Clone) (Instance) #"+std::to_string(i);f->glow[list[i]]=1;gameCopies.push_back(list[i]);}Ptr arr=f->array(8,2,list);f->obj[w].materials=arr;return arr;};
  for(uint64_t now:{2300050ull,2300100ull}){Ptr gameArray=instantiatePalette();copy.tick(now);assert(f->obj[w].materials!=gameArray&&meshOf(w)==ourMesh);for(Ptr m:{load<Ptr>(f->obj[w].materials+0x20),load<Ptr>(f->obj[w].materials+0x28)}){assert(f->glow[m]==0&&f->diffuse[m]==texture);}}
  Ptr third=instantiatePalette();copy.tick(2300150);assert(f->obj[w].materials==third);copy.tick(2301800);assert(f->obj[w].materials!=third&&f->glow[load<Ptr>(f->obj[w].materials+0x20)]==0);
  auto copyLog=text(sharedCopy+"/GIBox Log.txt");assert(copyLog.find("material-reclaimed Copy/Part0")!=std::string::npos&&copyLog.find("attempt 1 applied")!=std::string::npos&&copyLog.find("skipped (current materials are not the owned clones)")==std::string::npos);
  copy.shutdown();assert(meshOf(w)==originalMesh&&f->obj[w].materials==saved&&f->handles.empty());for(Ptr m:gameCopies){assert(alive(m));}}
 // 35. Similar names by themselves are insufficient: wrong row count, another material stem and an unrelated mesh
 //     must all remain untouched by the early guard.
 for(int mode=0;mode<3;mode++){
  std::string mediaGuard=root+"/mediaGuard"+std::to_string(mode),sharedGuard=mediaGuard+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedGuard+"/models/Guard",s);
  Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh),baseMat=load<Ptr>(f->obj[w].materials+0x20);f->obj[baseMat].name="Avatar_Mat_Body";f->world(w);f->scene={w};write(sharedGuard+"/control.json","{\"enabled\":true,\"selected\":[\"Guard\"]}");
  Runtime guard(e,pkg,root+"/privateGuard"+std::to_string(mode),mediaGuard,false);guard.tick(2400000);Ptr gameCopy=clone(load<Ptr>(f->obj[w].materials+0x20),0);f->obj[gameCopy].name=(mode==1?"Different_Mat":"Avatar_Mat_Body")+std::string("(Clone) (Instance) #0");Ptr pair[]={gameCopy,gameCopy};Ptr array=f->array(8,mode==0?2:1,pair);f->obj[w].materials=array;
  Ptr current=mode==2?f->mesh("UnrelatedBody"):meshOf(w);f->obj[w].mesh=current;guard.tick(2400050);assert(f->obj[w].materials==array&&meshOf(w)==current);guard.shutdown();assert(alive(gameCopy)&&f->handles.empty());}
 // 36. A character-scoped request survives waiting, retries incomplete captures without overwriting the last good
 //     record, completes only after worker read-back, rejects stale completions and is acknowledged across restart.
 {std::string mediaRefresh=root+"/mediaRefresh",sharedRefresh=mediaRefresh+"/"+pkg+"/GIBox",bonesRefresh=sharedRefresh+"/skeletons";Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh),completeBones=f->obj[w].bones;f->world(w);f->scene={w};
  Ptr entity=load<Ptr>(load<Ptr>(load<Ptr>(f->manager+0x10)+0x10)+0x20);store<uint32_t>(entity+0x3a4,10000200);
  SkeletonRenderer rec;rec.name="Body";rec.target={"Body","Body",3,1,1};rec.bones={"BoneA"};rec.parents={-1};rec.bindposes.resize(64);for(int i=0;i<4;i++){float one=1;std::memcpy(rec.bindposes.data()+i*20,&one,4);}
  auto old=skeletonJson(10000107,"OldCapture",{rec});std::string file=bonesRefresh+"/10000107/skeleton.json";std::filesystem::create_directories(bonesRefresh+"/10000107");write(file,old);
  auto request=[&](long long n){write(sharedRefresh+"/control.json","{\"enabled\":false,\"selected\":[],\"refreshBones\":"+std::to_string(n)+",\"refreshBonesCharacter\":10000107}");};
  request(901);
  {Runtime refresh(e,pkg,root+"/privateRefresh",mediaRefresh,false);auto st=[&]{return JsonParser(text(sharedRefresh+"/status.json")).parse();};
   refresh.tick(2500000);assert(st().at("boneRefreshState").string=="waiting-character"&&text(file)==old);
   store<uint32_t>(entity+0x3a4,10000107);f->obj[w].bones=f->array(8,0);refresh.tick(2500600);refresh.tick(2501200);assert(st().at("boneRefreshState").string!="complete"&&text(file)==old);
   f->obj[w].bones=completeBones;refresh.tick(2501800);refresh.tick(2502400);assert(st().at("boneRefreshState").string=="saving");refresh.tick(2502500);assert(st().at("boneRefreshState").string=="complete"&&text(file)!=old);
   auto reads=f->bindposeReads;refresh.tick(2503100);assert(f->bindposeReads==reads);
   request(902);refresh.tick(2503700);refresh.tick(2504300);request(903);refresh.tick(2504400);assert(st().at("boneRefreshRequest").number==903&&st().at("boneRefreshState").string!="complete");
   refresh.tick(2505000);refresh.tick(2505100);assert(st().at("boneRefreshState").string=="complete");refresh.shutdown();}
  {Runtime restarted(e,pkg,root+"/privateRefreshRestart",mediaRefresh,false);restarted.tick(2510000);auto st=JsonParser(text(sharedRefresh+"/status.json")).parse();assert(st.at("boneRefreshRequest").number==903&&st.at("boneRefreshState").string=="complete");restarted.shutdown();}
  // A request queued while the game is not running is NOT discarded on first control load.
  request(904);{Runtime queued(e,pkg,root+"/privateRefreshQueued",mediaRefresh,false);queued.tick(2520000);queued.tick(2520600);queued.tick(2520700);auto st=JsonParser(text(sharedRefresh+"/status.json")).parse();assert(st.at("boneRefreshRequest").number==904&&st.at("boneRefreshState").string=="complete");queued.shutdown();}
  // Rewriting the library later (including automatic captures after an update) must not replay a completed request.
  // Also accept a legacy 0.7.10 acknowledgement carrying an obsolete CRC: the operation token remains authoritative.
  write(file,old);write(sharedRefresh+"/bone-refresh-result.json","{\"request\":904,\"character\":10000107,\"crc32\":0}");
  {Runtime rewritten(e,pkg,root+"/privateRefreshRewritten",mediaRefresh,false);rewritten.tick(2530000);auto done=JsonParser(text(sharedRefresh+"/status.json")).parse();assert(done.at("boneRefreshRequest").number==904&&done.at("boneRefreshState").string=="complete");rewritten.shutdown();}
  // An acknowledgement belonging to another character must not suppress a real request with the same token.
  write(sharedRefresh+"/bone-refresh-result.json","{\"request\":904,\"character\":10000200}");
  {Runtime wrongCharacter(e,pkg,root+"/privateRefreshWrongCharacter",mediaRefresh,false);wrongCharacter.tick(2540000);auto pending=JsonParser(text(sharedRefresh+"/status.json")).parse();assert(pending.at("boneRefreshState").string!="complete");wrongCharacter.tick(2540600);wrongCharacter.tick(2540700);auto done=JsonParser(text(sharedRefresh+"/status.json")).parse();assert(done.at("boneRefreshState").string=="complete");wrongCharacter.shutdown();}
  assert(f->handles.empty());}
 // 37. Unity Destroy of the saved game mesh/materials cannot poison disable. The independent original snapshot
 //     remains alive after set release and another on/off cycle. Even an untracked game copy keeps valid assets.
 {std::string mediaExpired=root+"/mediaExpired",sharedExpired=mediaExpired+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedExpired+"/models/Expired",s);
  Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh),preview=f->renderer(originalMesh),worldMat=load<Ptr>(f->obj[w].materials+0x20),previewMat=load<Ptr>(f->obj[preview].materials+0x20),texture=f->object("texture","Original game colors");f->diffuse[worldMat]=texture;f->diffuse[previewMat]=texture;f->world(w);f->scene={w,preview};
  auto request=[&](bool on){write(sharedExpired+"/control.json",std::string("{\"enabled\":")+(on?"true":"false")+",\"selected\":[\"Expired\"]}");};
  request(true);Runtime expired(e,pkg,root+"/privateExpired",mediaExpired,false);expired.tick(2600000);expired.tick(2600600);Ptr modMesh=meshOf(w);assert(modMesh!=originalMesh&&meshOf(preview)==modMesh);
  destroy(originalMesh,0);destroy(worldMat,0);destroy(previewMat,0);request(false);expired.tick(2601200);
  auto st=JsonParser(text(sharedExpired+"/status.json")).parse();Ptr restored=meshOf(w),restoredMat=load<Ptr>(f->obj[w].materials+0x20);
  assert(st.at("state").string=="disabled"&&alive(restored)&&restored!=modMesh&&restored!=originalMesh&&meshOf(preview)==restored&&f->diffuse[restoredMat]==texture&&!alive(modMesh));
  request(true);expired.tick(2601800);expired.tick(2602400);Ptr again=meshOf(w);assert(again!=restored&&alive(again));request(false);expired.tick(2603000);
  assert(meshOf(w)==restored&&meshOf(preview)==restored&&alive(restored)&&alive(restoredMat)&&!alive(again));
  auto log=text(sharedExpired+"/GIBox Log.txt");assert(log.find("restore-backup Expired")!=std::string::npos&&log.find("Original mesh expired before restore")==std::string::npos&&log.find("tick-error")==std::string::npos);
  Ptr untracked=f->instantiate(w);destroy(w,0);destroy(preview,0);f->world(0);f->scene={untracked};expired.tick(2603600);
  assert(meshOf(untracked)==restored&&alive(restored)&&alive(restoredMat)&&f->handles.empty());expired.shutdown();destroy(untracked,0);}
 // 38. A transient setter failure belongs to one renderer. Other restores complete, and shared clone ownership is
 //     retained for the still-attached source/follower until its own retry succeeds.
 {std::string mediaRetry=root+"/mediaRestoreRetry",sharedRetry=mediaRetry+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedRetry+"/models/RetryRestore",s);
  Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh);f->world(w);f->scene={w};write(sharedRetry+"/control.json","{\"enabled\":true,\"selected\":[\"RetryRestore\"]}");
  Runtime retry(e,pkg,root+"/privateRestoreRetry",mediaRetry,false);retry.tick(2700000);retry.tick(2700600);Ptr modMesh=meshOf(w),child=f->instantiate(w),go=f->gameObjectOf({child});retry.spawned(go);retry.spawnedAfter();assert(meshOf(child)==modMesh);
  f->failMaterials=true;write(sharedRetry+"/control.json","{\"enabled\":false,\"selected\":[\"RetryRestore\"]}");retry.tick(2701200);
  unsigned pending=0;for(Ptr r:{w,child}){if(meshOf(r)==modMesh)pending++;for(Ptr m:{load<Ptr>(f->obj[r].materials+0x20)})assert(alive(m));}
  assert(pending==1&&alive(modMesh)&&JsonParser(text(sharedRetry+"/status.json")).parse().at("state").string=="waiting");
  retry.tick(2701800);assert(meshOf(w)==originalMesh&&meshOf(child)==originalMesh&&!alive(modMesh)&&f->handles.empty());
  assert(text(sharedRetry+"/GIBox Log.txt").find("tick-error")==std::string::npos);retry.shutdown();}
 // 39. Pre-binding interception retains a fresh incoming original and checks THAT mesh's bone-name hash count, so unloading
 //     the predecessor does not change the restore target and an incompatible new asset cannot be substituted.
 {std::string mediaBind=root+"/mediaFreshBind",sharedBind=mediaBind+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedBind+"/models/FreshBind",s);
  Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh);f->world(w);f->scene={w};write(sharedBind+"/control.json","{\"enabled\":true,\"selected\":[\"FreshBind\"]}");
  Runtime bind(e,pkg,root+"/privateFreshBind",mediaBind,false);bind.tick(2800000);bind.tick(2800600);Ptr modMesh=meshOf(w),native=load<Ptr>(w+0x10),fresh=f->mesh();
  Ptr intercepted=bind.bindMesh(native,load<Ptr>(fresh+0x10));assert(intercepted==load<Ptr>(modMesh+0x10));destroy(originalMesh,0);bind.meshBound(native);
  Ptr incompatible=f->mesh();store<uint64_t>(load<Ptr>(incompatible+0x10)+0x90,0x1234);assert(bind.bindMesh(native,load<Ptr>(incompatible+0x10))==load<Ptr>(incompatible+0x10));
  write(sharedBind+"/control.json","{\"enabled\":false,\"selected\":[\"FreshBind\"]}");bind.tick(2801200);assert(meshOf(w)==fresh&&f->handles.empty());bind.shutdown();}
 // 40. A first-time renderer is taken over by the post-binding callback without waiting for another tick/scan.
 //     Different avatars and off-thread callbacks remain untouched; shutdown does not destroy a displayed fallback.
 {std::string mediaPost=root+"/mediaPostBind",sharedPost=mediaPost+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedPost+"/models/PostBind",s);
  Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh);f->world(w);f->scene={w};write(sharedPost+"/control.json","{\"enabled\":true,\"selected\":[\"PostBind\"]}");
  Runtime post(e,pkg,root+"/privatePostBind",mediaPost,false);post.tick(2900000);post.tick(2900600);Ptr modMesh=meshOf(w),fresh=f->renderer(originalMesh),native=load<Ptr>(fresh+0x10);
  assert(post.bindMesh(native,load<Ptr>(originalMesh+0x10))==load<Ptr>(originalMesh+0x10));post.meshBound(native);assert(meshOf(fresh)==modMesh&&text(sharedPost+"/GIBox Log.txt").find("bind-attached PostBind")!=std::string::npos);
  Ptr copy=f->instantiate(fresh);post.meshBound(load<Ptr>(copy+0x10));assert(meshOf(copy)==modMesh);
  Ptr foreign=f->renderer(originalMesh,"BoneA","Body",f->kavehRoot);post.meshBound(load<Ptr>(foreign+0x10));assert(meshOf(foreign)==originalMesh);
  Ptr other=f->renderer(originalMesh);std::thread off([&]{post.meshBound(load<Ptr>(other+0x10));});off.join();assert(meshOf(other)==originalMesh);
  destroy(originalMesh,0);post.shutdown();Ptr restored=meshOf(w);assert(alive(restored)&&restored!=modMesh&&meshOf(fresh)==restored&&meshOf(copy)==restored&&f->handles.empty());}
 // 41. Disabling one model (global enabled stays true) does not let binding/fast guards revive its pending attachment.
 {std::string mediaOne=root+"/mediaOneOff",sharedOne=mediaOne+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedOne+"/models/OneOff",s);
  Ptr originalMesh=f->mesh(),w=f->renderer(originalMesh);f->world(w);f->scene={w};write(sharedOne+"/control.json","{\"enabled\":true,\"selected\":[\"OneOff\"]}");
  Runtime one(e,pkg,root+"/privateOneOff",mediaOne,false);one.tick(3000000);one.tick(3000600);Ptr modMesh=meshOf(w),native=load<Ptr>(w+0x10),originalNative=load<Ptr>(originalMesh+0x10);assert(modMesh!=originalMesh);
  f->failMaterials=true;write(sharedOne+"/control.json","{\"enabled\":true,\"selected\":[]}");one.tick(3001200);assert(meshOf(w)==modMesh&&JsonParser(text(sharedOne+"/status.json")).parse().at("state").string=="waiting");
  assert(one.bindMesh(native,originalNative)==originalNative);f->obj[w].mesh=originalMesh;one.meshBound(native);one.tick(3001210);assert(meshOf(w)==originalMesh);
  one.tick(3001800);assert(meshOf(w)==originalMesh);one.shutdown();assert(f->handles.empty());}
 // 42. Repeated unrelated bindings fingerprint once per cache lifetime and never walk the skeleton. Geometry,
 //     renderer/mesh instance identity, mesh replacement and Tick-time expiry all invalidate the relevant lookup.
 {std::string mediaFilter=root+"/mediaFilter",sharedFilter=mediaFilter+"/"+pkg+"/GIBox";Spec s;s.withTarget(fixture::target("Body"));writeModel(sharedFilter+"/models/Filter",s);
  Ptr full=f->mesh(),w=f->renderer(full);f->world(w);f->scene={w};write(sharedFilter+"/control.json","{\"enabled\":true,\"selected\":[\"Filter\"],\"logLevel\":\"debug\"}");
  Runtime filter(e,pkg,root+"/privateFilter",mediaFilter,false);filter.tick(3100000);filter.tick(3100600);Ptr mod=meshOf(w);assert(mod!=full);
  Ptr mobMesh=f->mesh("MonsterBody",4),mob=f->renderer(mobMesh),native=load<Ptr>(mob+0x10),mn=load<Ptr>(mobMesh+0x10);unsigned names=f->nameReads[mobMesh],bones=f->boneReads[mob];
  for(unsigned i=0;i<1000;i++)filter.meshBound(native);
  assert(meshOf(mob)==mobMesh&&f->nameReads[mobMesh]==names+1&&f->boneReads[mob]==bones);
  store<uint32_t>(native+8,f->nextId++);filter.meshBound(native);assert(f->nameReads[mobMesh]==names+2);
  store<uint32_t>(mn+8,f->nextId++);filter.meshBound(native);assert(f->nameReads[mobMesh]==names+3);
  store<uint32_t>(f->obj[mobMesh].shared+0x74,5);filter.meshBound(native);assert(f->nameReads[mobMesh]==names+4&&f->boneReads[mob]==bones);
  Ptr ttl=f->renderer(mobMesh);filter.meshBound(load<Ptr>(ttl+0x10));unsigned beforeExpiry=f->nameReads[mobMesh];filter.tick(3102700);
  filter.meshBound(load<Ptr>(ttl+0x10));filter.meshBound(load<Ptr>(ttl+0x10));assert(f->nameReads[mobMesh]==beforeExpiry+1);
  assert(text(sharedFilter+"/GIBox Log.txt").find("runtime DEBUG bind-filter-stats checks ")!=std::string::npos);
  Ptr incoming=f->mesh();f->obj[mob].mesh=incoming;filter.meshBound(native);assert(meshOf(mob)==mod);filter.shutdown();assert(meshOf(mob)==incoming&&f->handles.empty());}
 // 43. Incomplete headers, bones, parent chains and materials stay retryable on the SAME object. A matching
 //     fingerprint on the wrong avatar must still be rejected; reparenting a pooled candidate is observed afresh.
 {std::string mediaLoading=root+"/mediaBindLoading",sharedLoading=mediaLoading+"/"+pkg+"/GIBox";Spec s;s.withTarget(fixture::target("Body"));writeModel(sharedLoading+"/models/Loading",s);
  Ptr full=f->mesh(),w=f->renderer(full);f->world(w);f->scene={w};write(sharedLoading+"/control.json","{\"enabled\":true,\"selected\":[\"Loading\"]}");
  Runtime loading(e,pkg,root+"/privateBindLoading",mediaLoading,false);loading.tick(3200000);loading.tick(3200600);Ptr mod=meshOf(w);assert(mod!=full);
  Ptr preview=f->renderer(full),savedBones=f->obj[preview].bones;f->obj[preview].bones=f->array(8,0);loading.meshBound(load<Ptr>(preview+0x10));assert(meshOf(preview)==full);
  f->obj[preview].bones=savedBones;loading.meshBound(load<Ptr>(preview+0x10));assert(meshOf(preview)==mod);
  Ptr worldPending=f->renderer(full),worldBones=f->obj[worldPending].bones;f->obj[worldPending].bones=0;f->world(worldPending);loading.meshBound(load<Ptr>(worldPending+0x10));assert(meshOf(worldPending)==full);
  f->obj[worldPending].bones=worldBones;loading.meshBound(load<Ptr>(worldPending+0x10));assert(meshOf(worldPending)==mod);f->world(w);
  Ptr missing=f->renderer(full),mats=f->obj[missing].materials;f->obj[missing].materials=0;loading.meshBound(load<Ptr>(missing+0x10));assert(meshOf(missing)==full);
  f->obj[missing].materials=mats;loading.meshBound(load<Ptr>(missing+0x10));assert(meshOf(missing)==mod);
  Ptr empty=f->mesh("",3),unnamed=f->renderer(empty);loading.meshBound(load<Ptr>(unnamed+0x10));assert(meshOf(unnamed)==empty);f->obj[empty].name="Body";loading.meshBound(load<Ptr>(unnamed+0x10));assert(meshOf(unnamed)==mod);
  Ptr zero=f->mesh("Body",0),partial=f->renderer(zero);loading.meshBound(load<Ptr>(partial+0x10));assert(meshOf(partial)==zero);store<uint32_t>(f->obj[zero].shared+0x74,3);loading.meshBound(load<Ptr>(partial+0x10));assert(meshOf(partial)==mod);
  Ptr foreign=f->renderer(full,"BoneA","Body",f->kavehRoot);loading.meshBound(load<Ptr>(foreign+0x10));assert(meshOf(foreign)==full);
  Ptr bone=load<Ptr>(f->obj[foreign].bones+0x20);f->obj[bone].parent=f->skeletonRoot;loading.meshBound(load<Ptr>(foreign+0x10));assert(meshOf(foreign)==mod);
  loading.shutdown();assert(f->handles.empty());}
 // 44. LOD candidates pass the coarse filter even when vertex counts differ; the existing world/bone checks decide.
 {std::string mediaGate=root+"/mediaBindLodGate",sharedGate=mediaGate+"/"+pkg+"/GIBox";Spec s;s.withTarget(fixture::target("Body"));writeModel(sharedGate+"/models/LodGate",s);
  Ptr full=f->mesh(),w=f->renderer(full);f->world(w);f->scene={w};write(sharedGate+"/control.json","{\"enabled\":true,\"selected\":[\"LodGate\"]}");
  Runtime gate(e,pkg,root+"/privateBindLodGate",mediaGate,false);gate.tick(3300000);gate.tick(3300600);Ptr mod=meshOf(w),lm=f->mesh("Body_LOD1",2),lr=f->renderer(lm);f->world(lr);gate.meshBound(load<Ptr>(lr+0x10));
  assert(meshOf(lr)==mod&&text(sharedGate+"/GIBox Log.txt").find("attached-lod LodGate/Part0 over Body_LOD1")!=std::string::npos);
  gate.shutdown();assert(meshOf(lr)==lm&&f->handles.empty());}
 // 45. Enabling a new model, hot-reloading an existing target and learning a moved target clear prior rejections.
 {std::string mediaEpoch=root+"/mediaBindEpoch",sharedEpoch=mediaEpoch+"/"+pkg+"/GIBox";Spec a;a.withTarget(fixture::target("Body"));writeModel(sharedEpoch+"/models/Epoch",a);
  Spec b;b.character(10000107,{"citlali"}).withTarget(fixture::target("Cape"));writeModel(sharedEpoch+"/models/New",b);Ptr full=f->mesh(),w=f->renderer(full);f->world(w);f->scene={w};
  write(sharedEpoch+"/control.json","{\"enabled\":true,\"selected\":[\"Epoch\"]}");Runtime epoch(e,pkg,root+"/privateBindEpoch",mediaEpoch,false);epoch.tick(3400000);epoch.tick(3400600);
  Ptr cape=f->mesh("Cape"),candidate=f->renderer(cape),cn=load<Ptr>(candidate+0x10);epoch.meshBound(cn);assert(meshOf(candidate)==cape);
  write(sharedEpoch+"/control.json","{\"enabled\":true,\"selected\":[\"Epoch\",\"New\"]}");epoch.tick(3401200);assert(meshOf(candidate)==cape);epoch.meshBound(cn);assert(meshOf(candidate)!=cape);epoch.shutdown();assert(f->handles.empty());
  // An edit changes the target with no control edit. The rejected renderer is not in the scan, so only meshBound sees it.
  f->world(w);f->scene={w};write(sharedEpoch+"/control.json","{\"enabled\":true,\"selected\":[\"Epoch\"]}");Runtime reload(e,pkg,root+"/privateBindEpochReload",mediaEpoch,false);reload.tick(3410000);reload.tick(3410600);
  Ptr pm=f->mesh("NewBody"),pr=f->renderer(pm),pn=load<Ptr>(pr+0x10);reload.meshBound(pn);assert(meshOf(pr)==pm);Spec updated;updated.gen("2").withTarget(fixture::target("NewBody"));writeModel(sharedEpoch+"/models/Epoch",updated);
  reload.tick(3411700);reload.tick(3412300);unsigned reads=f->nameReads[pm];reload.meshBound(pn);assert(f->nameReads[pm]==reads+1&&meshOf(pr)==pm); // prior rejection was cleared, but the new template is not built yet
  f->world(pr);reload.tick(3412900);assert(meshOf(pr)!=pm);reload.shutdown();assert(f->handles.empty());
  // checkStale learns a new vertex count from the world without changing model.json/control.json.
  writeModel(sharedEpoch+"/models/Epoch",a);f->world(w);f->scene={w};Runtime learned(e,pkg,root+"/privateBindEpochLearned",mediaEpoch,false);learned.tick(3420000);learned.tick(3420600);
  Ptr moved=f->mesh("Body",4),mr=f->renderer(moved),mn=load<Ptr>(mr+0x10),hidden=f->renderer(moved),hn=load<Ptr>(hidden+0x10);learned.meshBound(mn);learned.meshBound(hn);assert(meshOf(mr)==moved&&meshOf(hidden)==moved);
  f->world(mr);f->scene={mr};learned.tick(3421200);assert(meshOf(mr)!=moved);learned.meshBound(hn);assert(meshOf(hidden)==meshOf(mr));learned.shutdown();assert(f->handles.empty());}
 // 46. Unknown targets do not produce negative entries. Once the world renderer teaches the target, an existing
 //     preview can be taken over immediately without waiting for rejection expiry or recreating its object.
 {std::string mediaUnknown=root+"/mediaBindUnknown",sharedUnknown=mediaUnknown+"/"+pkg+"/GIBox";writeModel(sharedUnknown+"/models/Unknown");f->world(0);f->scene.clear();f->assets.clear();
  write(sharedUnknown+"/control.json","{\"enabled\":true,\"selected\":[\"Unknown\"]}");Runtime unknown(e,pkg,root+"/privateBindUnknown",mediaUnknown,false);unknown.tick(3500000);unknown.tick(3500600);
  Ptr full=f->mesh(),preview=f->renderer(full),native=load<Ptr>(preview+0x10);unsigned reads=f->nameReads[full];unknown.meshBound(native);unknown.meshBound(native);assert(meshOf(preview)==full&&f->nameReads[full]==reads+2);
  Ptr w=f->renderer(full);f->world(w);f->scene={w};unknown.tick(3501200);assert(meshOf(w)!=full);unknown.meshBound(native);assert(meshOf(preview)==meshOf(w));unknown.shutdown();assert(f->handles.empty());}
 // 37. Preload and lazy textures: a switched-on model's files are read before its character appears; only the textures
 //     the current variant draws are made, and a switch to another variant reads the rest in the background first.
 {std::string mediaLazy=root+"/mediaLazy",sharedLazy=mediaLazy+"/"+pkg+"/GIBox";Spec s;s.version=2;s.textures=true;s.extraMaterials=2;s.withTarget(fixture::target("Body"));
  s.variants="{\"vars\":[{\"name\":\"$look\",\"values\":[0,1,2]}],\"default\":{\"$look\":0}}";
  s.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[0]]}},{\"start\":0,\"count\":3,\"material\":\"Material1\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[1]]}},{\"start\":0,\"count\":3,\"material\":\"Material2\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[2]]}}]"};
  writeModel(sharedLazy+"/models/Lazy",s);auto textures=[&]{unsigned n=0;for(auto& kv:f->obj)if(kv.second.kind=="texture"&&alive(kv.first))n++;return n;};
  f->world(0);f->scene={};write(sharedLazy+"/control.json","{\"enabled\":true,\"logLevel\":\"debug\",\"selected\":[\"Lazy\"]}");
  Runtime lazy(e,pkg,root+"/privateLazy",mediaLazy,false);unsigned before=textures();lazy.tick(2700000);lazy.tick(2700600);
  assert(text(sharedLazy+"/GIBox Log.txt").find("preload Lazy g:")!=std::string::npos&&textures()==before);
  Ptr full=f->mesh(),w=f->renderer(full);f->world(w);f->scene={w};lazy.tick(2701200);lazy.tick(2701800);
  assert(meshOf(w)!=full&&textures()==before+2);   // Material0: main + lightmap; Material1/2 not made
  write(sharedLazy+"/control.json","{\"enabled\":true,\"logLevel\":\"debug\",\"selected\":[\"Lazy\"],\"variables\":{\"Lazy\":{\"$look\":2}}}");lazy.tick(2702400);
  auto log=text(sharedLazy+"/GIBox Log.txt");assert(log.find("variant-textures-loading Lazy $look=2")!=std::string::npos&&log.find("variant Lazy $look=2")==std::string::npos&&textures()==before+2);
  lazy.tick(2703000);assert(text(sharedLazy+"/GIBox Log.txt").find("variant Lazy $look=2")!=std::string::npos&&textures()==before+3);
  write(sharedLazy+"/control.json","{\"enabled\":true,\"logLevel\":\"debug\",\"selected\":[\"Lazy\"],\"variables\":{\"Lazy\":{\"$look\":0}}}");lazy.tick(2703600);
  assert(text(sharedLazy+"/GIBox Log.txt").find("variant Lazy $look=0")!=std::string::npos&&textures()==before+3);   // already made: switches at once
  lazy.shutdown();assert(meshOf(w)==full&&textures()==before&&f->handles.empty());}
 // 43. A party member that is loaded but not switched in (not the controlled character's renderer): its identity comes
 //     from the bone library's avatar before any set of the model exists, so it is built and taken over while hidden.
 //     Without a library record it still waits, as before.
 {std::string mediaParty=root+"/mediaParty",sharedParty=mediaParty+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedParty+"/models/Party",s);
  Ptr entity=load<Ptr>(load<Ptr>(load<Ptr>(f->manager+0x10)+0x10)+0x20);store<uint32_t>(entity+0x3a4,10000200);
  Ptr kaveh=f->renderer(f->mesh("KavehBody"),"BoneA","Body",f->kavehRoot);f->world(kaveh);f->scene={kaveh};
  write(sharedParty+"/control.json","{\"enabled\":true,\"selected\":[\"Party\"]}");
  Ptr original=f->mesh(),hidden=f->renderer(original);
  {Runtime party(e,pkg,root+"/privateParty",mediaParty,false);party.tick(3100000);party.tick(3100600);party.meshBound(load<Ptr>(hidden+0x10));
   assert(meshOf(hidden)==original&&text(sharedParty+"/GIBox Log.txt").find("avatar identity is not verified")!=std::string::npos);party.shutdown();}
  SkeletonRenderer rec;rec.name="Body";rec.target=fingerprintOf(JsonParser(fixture::target("Body")).parse());rec.bones={"BoneA"};rec.parents={-1};rec.bindposes.resize(64);for(int i=0;i<4;i++){float one=1;std::memcpy(rec.bindposes.data()+i*20,&one,4);}
  std::filesystem::create_directories(sharedParty+"/skeletons/10000107");write(sharedParty+"/skeletons/10000107/skeleton.json",skeletonJson(10000107,"Scene/EntityRoot/Avatar_Girl_Catalyst_Citlali(Clone)/OffsetDummy/BoneA",{rec}));
  Ptr foreignMesh=f->mesh(),foreignHidden=f->renderer(foreignMesh,"BoneA","Body",f->kavehRoot);
  {Runtime party(e,pkg,root+"/privateParty2",mediaParty,false);party.tick(3200000);party.tick(3200600);
   party.meshBound(load<Ptr>(foreignHidden+0x10));assert(meshOf(foreignHidden)==foreignMesh);   // same mesh, another avatar
   party.meshBound(load<Ptr>(hidden+0x10));assert(meshOf(hidden)!=original&&text(sharedParty+"/GIBox Log.txt").find("bind-attached Party")!=std::string::npos);
   party.shutdown();assert(meshOf(hidden)==original&&f->handles.empty());}
  store<uint32_t>(entity+0x3a4,10000107);}
 // 44. model.bin is checked whole once; the stamp is remembered, and a file changed afterwards is checked again.
 {std::string mediaCrc=root+"/mediaCrc",sharedCrc=mediaCrc+"/"+pkg+"/GIBox",dir=sharedCrc+"/models/Crc";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(dir,s);
  Ptr original=f->mesh(),w=f->renderer(original);f->world(w);f->scene={w};write(sharedCrc+"/control.json","{\"enabled\":true,\"selected\":[\"Crc\"]}");
  {Runtime crc(e,pkg,root+"/privateCrc",mediaCrc,false);crc.tick(3300000);crc.tick(3300600);assert(meshOf(w)!=original);crc.shutdown();}
  assert(text(root+"/privateCrc/verified-models.txt").find(dir+"/model.bin\t")!=std::string::npos);
  auto bin=text(dir+"/model.bin");bin.back()=char(bin.back()^0x5a);write(dir+"/model.bin.new",bin);std::filesystem::rename(dir+"/model.bin.new",dir+"/model.bin");
  {Runtime crc(e,pkg,root+"/privateCrc",mediaCrc,false);crc.tick(3400000);crc.tick(3400600);
   assert(meshOf(w)==original&&text(sharedCrc+"/GIBox Log.txt").find("model-error Crc: model.bin: checksum mismatch")!=std::string::npos);crc.shutdown();}}
 // 45. Glow experiments (control.json "experiments", debug only): the game's own materials on our mesh, an earlier first
 //     material re-copy, vertex colours set to the game mesh's usual value and UV2 zeroed (models rebuilt when switched).
 {std::string mediaExp=root+"/mediaExp",sharedExp=mediaExp+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.channels=true;s.withTarget(fixture::target("Body"));writeModel(sharedExp+"/models/Exp",s);
  Ptr original=f->mesh(),w=f->renderer(original),gameMat=load<Ptr>(f->obj[w].materials+0x20);f->world(w);f->scene={w};
  auto control=[&](std::string x){write(sharedExp+"/control.json","{\"enabled\":true,\"logLevel\":\"debug\",\"selected\":[\"Exp\"],\"experiments\":{"+x+"}}");};
  control("\"gameMaterials\":true,\"firstRefreshMs\":300");channelData.clear();
  Runtime exp(e,pkg,root+"/privateExp",mediaExp,false);exp.tick(3500000);exp.tick(3500600);
  assert(meshOf(w)!=original&&load<Ptr>(f->obj[w].materials+0x20)==gameMat);   // our mesh, the game's own material
  bool white=false;for(auto& d:channelData)white|=d==Bytes(12,255);assert(white);   // the mod's vertex colours as converted
  control("\"firstRefreshMs\":300");exp.tick(3501200);Ptr clone0=load<Ptr>(f->obj[w].materials+0x20);assert(clone0!=gameMat&&f->obj[clone0].kind=="material");
  auto log=text(sharedExp+"/GIBox Log.txt");assert(log.find("experiments gameMaterials=off firstRefreshMs=300")!=std::string::npos);
  control("\"neutralColors\":true,\"noUv2\":true");channelData.clear();exp.tick(3501800);exp.tick(3502400);exp.tick(3503000);
  log=text(sharedExp+"/GIBox Log.txt");assert(log.find("experiment-rebuild Exp")!=std::string::npos&&log.find("experiment-mesh Exp/Part0 colours=(0,0,0,0) uv2=0")!=std::string::npos&&meshOf(w)!=original);
  bool zero=false;for(auto& d:channelData)zero|=d==Bytes(12,0);assert(zero);
  exp.shutdown();assert(meshOf(w)==original&&f->handles.empty());}
 // The first re-copy follows firstRefreshMs: 300 ms after attaching instead of 1.5 s.
 {std::string mediaRef=root+"/mediaRef",sharedRef=mediaRef+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedRef+"/models/Ref",s);
  Ptr original=f->mesh(),w=f->renderer(original);f->world(w);f->scene={w};
  for(int run=0;run<2;run++){std::string x=run?",\"experiments\":{\"firstRefreshMs\":300}":"";std::filesystem::remove(sharedRef+"/GIBox Log.txt");
   write(sharedRef+"/control.json","{\"enabled\":true,\"selected\":[\"Ref\"]"+x+"}");
   Runtime ref(e,pkg,root+"/privateRef"+std::to_string(run),mediaRef,false);uint64_t t=3600000+run*100000;ref.tick(t);ref.tick(t+600);ref.tick(t+1200);
   bool refreshed=text(sharedRef+"/GIBox Log.txt").find("material-refresh Ref: attempt 1 applied")!=std::string::npos;assert(meshOf(w)!=original&&refreshed==(run==1));ref.shutdown();}}
 // 46. Appearance timeline (debug): hand-offs and binds of a part's mesh are logged also before replacement is ready.
 {std::string mediaTl=root+"/mediaTimeline",sharedTl=mediaTl+"/"+pkg+"/GIBox";Spec s;s.withTarget(fixture::target("Body"));writeModel(sharedTl+"/models/Tl",s);
  Ptr original=f->mesh(),w=f->renderer(original),go=f->gameObjectOf({w});f->world(w);f->scene={w};
  write(sharedTl+"/control.json","{\"enabled\":false,\"logLevel\":\"debug\",\"selected\":[\"Tl\"]}");
  Runtime tl(e,pkg,root+"/privateTimeline",mediaTl,false);tl.tick(3700000);tl.spawned(go);tl.meshBound(load<Ptr>(w+0x10));
  Ptr mob=f->renderer(f->mesh("MonsterBody"),"BoneA","Mob",f->kavehRoot);tl.meshBound(load<Ptr>(mob+0x10));
  auto log=text(sharedTl+"/GIBox Log.txt");
  assert(log.find("appearance handoff gameobject Avatar_Girl_Catalyst_Citlali_Manekin")!=std::string::npos);
  assert(log.find("appearance bind-early renderer")!=std::string::npos&&log.find("avatar avatar_girl_catalyst_citlali mesh Body (3 vertices) controlled")!=std::string::npos);
  assert(log.find("MonsterBody")==std::string::npos&&meshOf(w)==original);tl.shutdown();}
 // 47. Glow trace (debug + experiments.glowTrace): engine entries hooked once, every write to this model's materials kept
 //     with its frame and caller, a switch-in window summarised, and the clone read back against its original. Here the
 //     game turns a colour on for the clone and the original, and off for the original only: that is what must show.
 //     A game started without the trace hooks nothing; switching the trace off stops recording but unhooks nothing.
 {std::string mediaGlow=root+"/mediaGlow",sharedGlow=mediaGlow+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedGlow+"/models/Glow",s);
  Ptr original=f->mesh(),w=f->renderer(original),gameMat=load<Ptr>(f->obj[w].materials+0x20);f->world(w);f->scene={w};f->keywords[gameMat]={"_NORMALMAP"};
  auto ctlGlow=[&](const char* json){write(sharedGlow+"/control.json.tmp",json);std::filesystem::rename(sharedGlow+"/control.json.tmp",sharedGlow+"/control.json");};
  ctlGlow("{\"enabled\":true,\"selected\":[\"Glow\"]}");
  auto rvaOf=[](const char* n){for(auto& h:glow::kHooks)if(std::string(h.name)==n)return h.rva;std::abort();};
  static std::map<uintptr_t,void*> hooked;static int dummy;
  struct G {static void setColor(Ptr m,int32_t id,const float* v,Ptr){std::array<float,4> c;std::memcpy(c.data(),v,16);f->props[m][id]=c;}static int32_t toId(Ptr,Ptr){return 7;}};
  Engine ge=e;ge.size=0x16000000;ge.hook=[&](uintptr_t rva,void* replacement,void** original){hooked[rva]=replacement;
   *original=rva==rvaOf("Material.INTERNAL_CALL_SetColorImpl(Material,int,Color*)")?reinterpret_cast<void*>(&G::setColor):rva==rvaOf("Shader.PropertyToID(string)")?reinterpret_cast<void*>(&G::toId):static_cast<void*>(&dummy);return true;};
  auto setColor=[&](Ptr m,std::array<float,4> c){reinterpret_cast<void(*)(Ptr,int32_t,const float*,Ptr)>(hooked.at(rvaOf("Material.INTERNAL_CALL_SetColorImpl(Material,int,Color*)")))(m,7,c.data(),0);};
  {Runtime cold(ge,pkg,root+"/privateGlowCold",mediaGlow,false);cold.tick(3790000);cold.tick(3790600);   // the model on, no trace: no hooks
   assert(meshOf(w)!=original&&hooked.empty()&&glow::trace().hooked==0&&!glow::trace().everything);cold.shutdown();assert(meshOf(w)==original&&f->handles.empty());}
  ctlGlow("{\"enabled\":true,\"logLevel\":\"debug\",\"selected\":[\"Glow\"],\"experiments\":{\"glowTrace\":true}}");
  Runtime gr(ge,pkg,root+"/privateGlow",mediaGlow,false);gr.tick(3800000);gr.tick(3800600);assert(meshOf(w)!=original);
  for(uint64_t t=3801200;t<=3806000;t+=600)gr.tick(t);                         // both material refreshes, attach window ends
  Ptr clone=load<Ptr>(f->obj[w].materials+0x20);assert(clone!=gameMat);
  {Ptr name=f->alloc(0x40);std::string n="_ElementRimColor";store<int32_t>(name+0x10,int32_t(n.size()));for(size_t i=0;i<n.size();i++)store<uint16_t>(name+0x14+2*i,uint16_t(n[i]));
   assert(reinterpret_cast<int32_t(*)(Ptr,Ptr)>(hooked.at(rvaOf("Shader.PropertyToID(string)")))(name,0)==7);}
  Ptr entity=load<Ptr>(load<Ptr>(load<Ptr>(f->manager+0x10)+0x10)+0x20);store<uint32_t>(entity+0x3a4,10000200);f->world(0);gr.tick(3806600);
  store<uint32_t>(entity+0x3a4,10000107);f->world(w);gr.tick(3807200);       // switch back: window opens
  setColor(clone,{0.2f,0.9f,1,1});setColor(gameMat,{0.2f,0.9f,1,1});gr.tick(3807800);setColor(gameMat,{0,0,0,0});
  for(uint64_t t=3808400;t<=3811800;t+=600)gr.tick(t);
  auto log=text(sharedGlow+"/GIBox Log.txt");
  assert(log.find("glow-hooks ")!=std::string::npos&&log.find(" entries hooked")!=std::string::npos&&log.find("glow-note trace on:")!=std::string::npos);
  assert(log.find("glow-window #1 attach Glow/Part0")!=std::string::npos&&log.find("glow-window #2 switch-in 10000107 at frame")!=std::string::npos&&log.find("switch-in 10000200")==std::string::npos);
  assert(log.find("glow-write #2 +0ms f")!=std::string::npos&&log.find(" game x1 SetColor _ElementRimColor#7 on Glow/Part0 clone row 0 Material: (0.2,0.9,1,1) by 0x")!=std::string::npos);
  assert(log.find("game x2 SetColor _ElementRimColor#7 on Glow/Part0 original slot 0 Material: (0.2,0.9,1,1) -> (0,0,0,0) (last +600ms")!=std::string::npos);
  assert(log.find("glow-diff #2 +4000ms Glow/Part0 row 0 Glow/Part0 clone row 0 Material vs original slot 0: _ElementRimColor#7 (0.2,0.9,1,1) vs (0,0,0,0) (3 equal, 1 GIBox textures, 1 parameters the game wrote, 1 keywords)")!=std::string::npos);
  assert(log.find("glow-diff #2 +0ms Glow/Part0 row 0 Glow/Part0 clone row 0 Material vs original slot 0: no difference")!=std::string::npos);
  assert(log.find("glow-diff refresh-before 2 Glow/Part0 row 0")!=std::string::npos&&log.find("glow-window #2 end: 3 by the game, 0 by GIBox")!=std::string::npos);
  assert(log.find("effect-mirror")==std::string::npos);   // no effect mirror any more
  unsigned installed=glow::trace().hooked;size_t entries=hooked.size();assert(installed>0&&entries>0);
  ctlGlow("{\"enabled\":true,\"logLevel\":\"debug\",\"selected\":[\"Glow\"]}");gr.tick(3812400);   // trace off: the hooks stay
  assert(!glow::trace().armed.load()&&glow::trace().hooked==installed&&hooked.size()==entries&&glow::trace().everything);
  assert(text(sharedGlow+"/GIBox Log.txt").find("glow-note trace off: nothing is recorded; the engine entries stay hooked until the game restarts")!=std::string::npos);
  gr.shutdown();assert(meshOf(w)==original&&f->handles.empty()&&!glow::trace().armed.load());}
 // 49. Slot textures (debug experiment): the game's own material stays on our mesh and the mod's textures go into draw
 //     slot 0 as an override, through its per-material property block (1) or its custom texture (2). A block the game
 //     dropped is written again, materials the game swaps into the slot are kept, and a mode change or turning the model
 //     off takes the overrides away (mode 2 has no remover: the shown material's own textures are written into them).
 {std::string mediaSlot=root+"/mediaSlot",sharedSlot=mediaSlot+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedSlot+"/models/Slot",s);
  Ptr original=f->mesh(),w=f->renderer(original),gameArray=f->obj[w].materials,gameMat=load<Ptr>(gameArray+0x20);f->world(w);f->scene={w};
  auto control=[&](int mode){write(sharedSlot+"/control.json","{\"enabled\":true,\"logLevel\":\"debug\",\"selected\":[\"Slot\"],\"experiments\":{\"slotTextures\":"+std::to_string(mode)+"}}");};
  auto shown=[&]{return load<Ptr>(f->obj[w].materials+0x20);};int32_t lightId=100+int32_t(std::hash<std::string>()("_LightMapTex")%1000);
  auto log=[&]{return text(sharedSlot+"/GIBox Log.txt");};auto has=[&](const std::string& x){return log().find(x)!=std::string::npos;};
  control(1);Runtime sr(e,pkg,root+"/privateSlot",mediaSlot,false);sr.tick(4000000);sr.tick(4000600);
  assert(meshOf(w)!=original&&shown()==gameMat&&slotFake.ctors==1&&slotFake.allow[w]==1);   // the game's material, our block
  Ptr main=slotFake.slots.at({w,0}).at(5),light=slotFake.slots.at({w,0}).at(lightId);assert(alive(main)&&alive(light)&&main!=light&&slotFake.slots.at({w,0}).size()==2);
  assert(has("slot-textures Slot/Part0 on ")&&has(": per-material property block, 1 of 1 draw slots, 2 textures, read back 2 of 2; per-material properties were off, switched on"));
  assert(has("experiments gameMaterials=off firstRefreshMs=default neutralColors=off noUv2=off glowTrace=off slotTextures=block"));
  slotFake.slots.erase({w,0});sr.tick(4002600);                                                  // the game drops the block
  assert(slotFake.slots.at({w,0}).at(5)==main&&has("slot-textures-lost Slot/Part0: 2 of 2 textures were gone from the blocks, written again (1 times so far)"));
  Ptr instance=f->object("material","Material (Instance)");f->obj[w].materials=f->typed("material",instance);sr.tick(4004800);   // the game's own instance
  assert(shown()==instance&&slotFake.slots.at({w,0}).at(5)==main&&has("slot-textures Slot/Part0: the game put other materials on the renderer, 1 of 1 slots (1 ours), slot 0: Material (Instance)"));
  assert(log().find("(2 times so far)")==std::string::npos);
  control(2);sr.tick(4005400);                                                                     // block -> custom texture
  assert(shown()==gameMat&&!slotFake.slots.count({w,0})&&slotFake.custom.at({w,0}).at(5)==main&&slotFake.custom.at({w,0}).at(lightId)==light&&slotFake.allow[w]==1);
  assert(has(": per-material custom texture, 1 of 1 draw slots, 2 textures, no getter of its own; the slots' property blocks show 0 of 2; per-material properties were off, switched on"));
  control(0);sr.tick(4006000);Ptr clone=shown();                                                  // custom texture -> copies
  assert(clone!=gameMat&&f->obj[clone].kind=="material"&&slotFake.custom.at({w,0}).at(5)==f->diffuse[clone]&&slotFake.custom.at({w,0}).at(lightId)==f->named[clone][lightId]&&alive(f->named[clone][lightId])&&slotFake.allow[w]==0);
  assert(has("slot-textures Slot/Part0: per-material custom textures cannot be removed; the textures of the materials now shown were written into them"));
  control(1);sr.tick(4006600);assert(shown()==gameMat&&slotFake.slots.at({w,0}).at(5)==main&&slotFake.allow[w]==1);
  sr.shutdown();                                                                                    // restore: no block left, flag back off
  assert(meshOf(w)==original&&f->obj[w].materials==gameArray&&!slotFake.slots.count({w,0})&&slotFake.allow[w]==0&&f->handles.empty());}
 // 50. Merged draws: neighbours with the same material row share a submesh, their index ranges joined in draw order.
 //     A draw hidden by the variant between two of them is not drawn, ranges that continue each other become one, a
 //     neighbour with another material keeps its own submesh and slot, and switching back reads the model's indices
 //     again. experiments.separateDraws rebuilds the model with one submesh per draw, for comparison.
 {std::string mediaMerge=root+"/mediaMerge",sharedMerge=mediaMerge+"/"+pkg+"/GIBox";Spec s;s.version=2;s.textures=true;s.extraMaterials=1;s.withTarget(fixture::target("Body"));
  s.variants="{\"vars\":[{\"name\":\"$mid\",\"values\":[0,1]}],\"default\":{\"$mid\":0}}";s.indices={0,1,2,2,1,0,1,2,0,0,2,1};
  s.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0},{\"start\":3,\"count\":3,\"material\":\"Material0\",\"slot\":0,\"when\":{\"vars\":[\"$mid\"],\"allow\":[[1]]}},"
   "{\"start\":6,\"count\":3,\"material\":\"Material0\",\"slot\":0},{\"start\":9,\"count\":3,\"material\":\"Material1\",\"slot\":0}]"};writeModel(sharedMerge+"/models/Merge",s);
  Ptr original=f->mesh(),w=f->renderer(original);f->world(w);f->scene={w};
  auto control=[&](int mid,bool separate){write(sharedMerge+"/control.json.tmp","{\"enabled\":true,\"logLevel\":\"debug\",\"selected\":[\"Merge\"],\"variables\":{\"Merge\":{\"$mid\":"+std::to_string(mid)+"}}"
   +(separate?",\"experiments\":{\"separateDraws\":true}":"")+"}");std::filesystem::rename(sharedMerge+"/control.json.tmp",sharedMerge+"/control.json");};
  auto subs=[&]{return load<uint32_t>(f->obj[meshOf(w)].shared+0xc0);};auto indices=[&](int i){return f->submeshIndices.at({meshOf(w),i});};
  auto slots=[&]{return load<uint64_t>(f->obj[w].materials+0x18);};auto has=[&](const std::string& x){return text(sharedMerge+"/GIBox Log.txt").find(x)!=std::string::npos;};
  using V=std::vector<uint32_t>;control(0,false);Runtime mr(e,pkg,root+"/privateMerge",mediaMerge,false);mr.tick(4100000);mr.tick(4100600);Ptr ours=meshOf(w);
  assert(ours!=original&&subs()==2&&slots()==2&&indices(0)==(V{0,1,2,1,2,0})&&indices(1)==(V{0,2,1}));      // hidden middle draw skipped
  auto texture=[&](int i){return f->diffuse[load<Ptr>(f->obj[w].materials+0x20+8*i)];};Ptr tex0=texture(0),tex1=texture(1);assert(alive(tex0)&&alive(tex1)&&tex0!=tex1);
  assert(has("submeshes Merge/Part0: 3 draws in effect, 2 submeshes"));
  control(1,false);mr.tick(4101200);mr.tick(4101800);                                               // three neighbours, one range
  assert(meshOf(w)==ours&&subs()==2&&slots()==2&&indices(0)==(V{0,1,2,2,1,0,1,2,0})&&indices(1)==(V{0,2,1})&&has("submeshes Merge/Part0: 4 draws in effect, 2 submeshes"));
  control(0,false);mr.tick(4102400);mr.tick(4103000);
  assert(meshOf(w)==ours&&subs()==2&&slots()==2&&indices(0)==(V{0,1,2,1,2,0})&&texture(0)==tex0&&texture(1)==tex1);   // re-copies keep each slot's texture
  control(0,true);for(uint64_t t=4103600;t<4110000&&!(meshOf(w)!=original&&meshOf(w)!=ours&&subs()==3);t+=600)mr.tick(t);   // one submesh per draw
  assert(has("experiment-rebuild Merge")&&subs()==3&&slots()==3&&indices(0)==(V{0,1,2})&&indices(1)==(V{1,2,0})&&indices(2)==(V{0,2,1}));
  assert(f->diffuse[load<Ptr>(f->obj[w].materials+0x20)]==f->diffuse[load<Ptr>(f->obj[w].materials+0x28)]&&f->diffuse[load<Ptr>(f->obj[w].materials+0x30)]!=f->diffuse[load<Ptr>(f->obj[w].materials+0x20)]);
  assert(has("submeshes Merge/Part0: 3 draws in effect, 3 submeshes (not merged: experiments.separateDraws)"));
  mr.shutdown();assert(meshOf(w)==original&&f->handles.empty());
  // flags.originalMaterials puts the game's palette on by position (submesh i, material i): its draws stay apart.
  Spec o;o.version=2;o.withTarget(fixture::target("Body"));o.indices={0,1,2,2,1,0};o.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0},{\"start\":3,\"count\":3,\"material\":\"Material0\",\"slot\":0}]"};
  o.edit=[](std::string& j){fixture::replaceIn(j,"\"parts\":[","\"flags\":{\"originalMaterials\":true},\"parts\":[");};writeModel(sharedMerge+"/models/Orig",o);
  Ptr ow=f->renderer(original);f->world(ow);f->scene={ow};write(sharedMerge+"/control.json","{\"enabled\":true,\"selected\":[\"Orig\"]}");
  Runtime orr(e,pkg,root+"/privateOrig",mediaMerge,false);orr.tick(4200000);orr.tick(4200600);
  assert(meshOf(ow)!=original&&load<uint32_t>(f->obj[meshOf(ow)].shared+0xc0)==2&&has("submeshes Orig/Part0: 2 draws in effect, 2 submeshes (not merged: original materials by position)"));
  orr.shutdown();assert(meshOf(ow)==original&&f->handles.empty());}
 // 51. Direct textures: the mod's textures go into the game's own material of a slot when it was made at runtime and no
 //     other renderer shows it; what it held goes back on every re-apply (here: the gameMaterials experiment), when the
 //     model is switched off, the renderer dies or attaching fails. A main texture the game puts back is replaced again; a game copy of the
 //     written material is tracked and gets its own textures back too. A loaded asset, or a runtime material another
 //     renderer shows, is copied as before.
 {std::string mediaDirect=root+"/mediaDirect",sharedDirect=mediaDirect+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));writeModel(sharedDirect+"/models/Direct",s);
  auto control=[&](bool enabled,bool games=false){write(sharedDirect+"/control.json.tmp",std::string("{\"enabled\":")+(enabled?"true":"false")+",\"logLevel\":\"debug\",\"selected\":[\"Direct\"]"+(games?",\"experiments\":{\"gameMaterials\":true}":"")+"}");
   std::filesystem::rename(sharedDirect+"/control.json.tmp",sharedDirect+"/control.json");};
  auto has=[&](const std::string& x){return text(sharedDirect+"/GIBox Log.txt").find(x)!=std::string::npos;};int32_t lightId=100+int32_t(std::hash<std::string>()("_LightMapTex")%1000);
  auto runtimeMade=[&](Ptr m){store<uint32_t>(load<Ptr>(m+0x10)+8,uint32_t(-7));};
  Ptr original=f->mesh(),w=f->renderer(original),gameArray=f->obj[w].materials,gameMat=load<Ptr>(gameArray+0x20),gameTex=f->object("texture","Original diffuse");
  f->diffuse[gameMat]=gameTex;runtimeMade(gameMat);f->world(w);f->scene={w};auto shown=[&]{return load<Ptr>(f->obj[w].materials+0x20);};
  control(true);Runtime dr(e,pkg,root+"/privateDirect",mediaDirect,false);dr.tick(4300000);dr.tick(4300600);
  Ptr ours=f->diffuse[gameMat],light=f->named[gameMat][lightId];
  assert(meshOf(w)!=original&&shown()==gameMat&&alive(ours)&&ours!=gameTex&&alive(light));                   // no copy: the game's material, our textures
  assert(has("direct-textures Direct/Part0 on Body: slot 0 Material: written (instance id -7; "));
  f->diffuse[gameMat]=gameTex;dr.tick(4301200);dr.tick(4301800);                                              // the game puts its own back
  assert(f->diffuse[gameMat]==ours&&has("direct-textures-lost Direct/Part0: the game put its own main texture on Material, ours written again (1 times so far)"));
  Ptr copy=clone(gameMat,0);f->obj[copy].name="Material (Instance)";f->obj[w].materials=f->typed("material",copy);dr.tick(4302400);dr.tick(4303000);   // the game's own instance
  assert(shown()==copy&&f->diffuse[copy]==ours&&has("direct-textures Direct/Part0: the game's copy Material (Instance) of Material carries our textures"));
  control(true,true);dr.tick(4303600);                                                                         // re-applied as the game's own materials
  assert(shown()==gameMat&&f->diffuse[gameMat]==gameTex&&f->named[gameMat][lightId]==0&&f->diffuse[copy]==gameTex&&f->named[copy][lightId]==0);
  control(true);dr.tick(4304200);assert(shown()==gameMat&&f->diffuse[gameMat]==ours&&f->named[gameMat][lightId]==light);   // and back
  control(false);dr.tick(4304800);dr.tick(4305400);                                                            // the model off
  assert(meshOf(w)==original&&f->obj[w].materials==gameArray&&f->diffuse[gameMat]==gameTex&&f->named[gameMat][lightId]==0);
  assert(has("direct-textures Direct/Part0 (restore): own textures written back into 1 game materials"));dr.shutdown();assert(f->handles.empty());
  // A loaded asset (positive instance id) and a runtime material on another renderer as well: copied, the game's untouched.
  Ptr w2=f->renderer(original),asset=load<Ptr>(f->obj[w2].materials+0x20);f->diffuse[asset]=gameTex;f->world(w2);f->scene={w2};control(true);
  {Runtime d2(e,pkg,root+"/privateDirect2",mediaDirect,false);d2.tick(4400000);d2.tick(4400600);Ptr on=load<Ptr>(f->obj[w2].materials+0x20);
   assert(meshOf(w2)!=original&&on!=asset&&f->diffuse[on]!=gameTex&&f->diffuse[asset]==gameTex&&has("direct-textures-copied Direct/Part0 on Body: slot 0 Material: a loaded asset (instance id "));d2.shutdown();assert(f->handles.empty());}
  Ptr w3=f->renderer(original),sharedMat=load<Ptr>(f->obj[w3].materials+0x20),npc=f->renderer(f->mesh("Other"));runtimeMade(sharedMat);f->diffuse[sharedMat]=gameTex;
  f->obj[npc].materials=f->typed("material",sharedMat);f->world(w3);f->scene={w3,npc};
  {Runtime d3(e,pkg,root+"/privateDirect3",mediaDirect,false);d3.tick(4500000);d3.tick(4500600);Ptr on=load<Ptr>(f->obj[w3].materials+0x20);
   assert(meshOf(w3)!=original&&on!=sharedMat&&f->diffuse[sharedMat]==gameTex&&has("slot 0 Material: also on "));d3.shutdown();assert(f->handles.empty());}
  // The renderer destroyed by the game, nothing adopted from it: the material it showed (still alive) gets its own back.
  Ptr w4=f->renderer(original),mat4=load<Ptr>(f->obj[w4].materials+0x20);runtimeMade(mat4);f->diffuse[mat4]=gameTex;f->world(w4);f->scene={w4};
  {Runtime d4(e,pkg,root+"/privateDirect4",mediaDirect,false);d4.tick(4600000);d4.tick(4600600);assert(meshOf(w4)!=original&&load<Ptr>(f->obj[w4].materials+0x20)==mat4&&f->diffuse[mat4]!=gameTex);
   destroy(w4,0);f->world(0);f->scene={};d4.tick(4601200);d4.tick(4601800);
   assert(f->diffuse[mat4]==gameTex&&f->named[mat4][lightId]==0&&has("direct-textures Direct/Part0 (detach): own textures written back into 1 game materials"));d4.shutdown();assert(f->handles.empty());}
  // The renderer refusing the material array while attaching: what was written into its material goes back first.
  Ptr w5=f->renderer(original),mat5=load<Ptr>(f->obj[w5].materials+0x20);runtimeMade(mat5);f->diffuse[mat5]=gameTex;f->world(w5);f->scene={w5};
  {Runtime d5(e,pkg,root+"/privateDirect5",mediaDirect,false);f->failMaterials=true;d5.tick(4700000);d5.tick(4700600);
   assert(!f->failMaterials&&has("direct-textures Direct/Part0 (attach failed): own textures written back into 1 game materials"));
   assert(meshOf(w5)==original&&load<Ptr>(f->obj[w5].materials+0x20)==mat5&&f->diffuse[mat5]==gameTex&&f->named[mat5][lightId]==0);   // left as the game had it
   d5.shutdown();assert(f->handles.empty());}}
 // Both animation modes loop through full frames, restore on switch/off and reuse dynamic weights/arrays.
 {auto mediaA=root+"/animation-media",sharedA=mediaA+"/"+pkg+"/GIBox";Spec source;source.positions={0,0,0,1,0,0,0,1,0};source.channels=true;fixture::writeModel(sharedA+"/models/Animated",fixture::animated(source));
  Ptr renderer=f->renderer(original);f->world(renderer);f->scene={renderer};int revision=0;
  auto control=[&](int mode,bool on=true){write(sharedA+"/control.json","{\"enabled\":"+std::string(on?"true":"false")+",\"selected\":[\"Animated\"],\"logLevel\":\"debug\",\"revision\":"+std::to_string(++revision)+",\"experiments\":{\"animationMode\":"+std::to_string(mode)+"}}");};
  auto log=[&]{return text(sharedA+"/GIBox Log.txt");};Runtime animated(e,pkg,root+"/animation-private",mediaA,false);control(0);uint64_t now=5000000;
  // The character's renderer gone while the frames are being built (a switch, loading): preparing waits, then goes on
  // on the renderer the character gets.
  for(int n=0;n<2;n++){animated.tick(now);now+=120;}destroy(renderer,0);
  for(int n=0;n<6;n++){animated.tick(now);now+=120;}assert(log().find("animation-wait Animated mode=cached prepare waits: the character's renderer for part 0 is not there")!=std::string::npos&&log().find("animation-error")==std::string::npos);
  renderer=f->renderer(original);f->world(renderer);f->scene={renderer};
  for(int n=0;n<25;n++){animated.tick(now);now+=120;}assert(log().find("animation-ready Animated mode=cached")!=std::string::npos&&log().find("animation-error")==std::string::npos&&log().find("prepare goes on: the character is back")!=std::string::npos);
  std::set<Ptr> cached;for(int n=0;n<15;n++){animated.tick(now++);cached.insert(meshOf(renderer));}assert(cached.size()==3);unsigned allocations=f->weightAllocs;
  for(int n=0;n<15;n++){animated.tick(now++);}assert(f->weightAllocs==allocations);
  control(1);now+=200;for(int n=0;n<25;n++){animated.tick(now);now+=120;}assert(log().find("animation-ready Animated mode=dynamic")!=std::string::npos&&log().find("animation-error")==std::string::npos);
  allocations=f->weightAllocs;unsigned handles=f->handles.size();std::set<Ptr> dynamic;for(int n=0;n<30;n++){animated.tick(now++);dynamic.insert(meshOf(renderer));}assert(dynamic.size()==2&&f->weightAllocs==allocations&&f->handles.size()==handles);
  Ptr front=meshOf(renderer);f->failPosition=true;animated.tick(now++);assert(meshOf(renderer)==front&&log().find("fake-animation-channel-error")!=std::string::npos);
  control(2);now+=200;for(int n=0;n<10;n++){animated.tick(now);now+=120;}Ptr paused=meshOf(renderer);for(int n=0;n<8;n++)animated.tick(now++);assert(meshOf(renderer)==paused);
  control(0,false);now+=200;animated.tick(now);animated.shutdown();assert(meshOf(renderer)==original&&f->handles.empty());}
 // experiments.logicCore: the model's own INI runs in the game and the frame shown is the prebuilt one whose draws
 // match what its draw selects; a selection no frame has keeps the frame shown; no match at the start keeps FrameLoop.
 {auto mediaL=root+"/logic-media",sharedLg=mediaL+"/"+pkg+"/GIBox";Spec source;source.positions={0,0,0,1,0,0,0,1,0};
  auto model=[&](const std::string& name,const std::vector<std::string>& ibs){fixture::writeLogicModel(sharedLg+"/models/"+name,fixture::withLogic(fixture::animated(source),fixture::logicJson(ibs)));};
  model("Logic",{"file:a.ib","file:b.ib","file:c.ib"});fixture::writeModel(sharedLg+"/models/Plain",fixture::animated(source));model("Miss",{"file:a.ib","file:b.ib","file:x.ib"});model("Early",{"file:x.ib","file:b.ib","file:c.ib"});
  Ptr renderer=f->renderer(original);f->world(renderer);f->scene={renderer};int revision=0;
  auto control=[&](const std::string& selected,bool logic){write(sharedLg+"/control.json","{\"enabled\":true,\"selected\":[\""+selected+"\"],\"logLevel\":\"debug\",\"revision\":"+std::to_string(++revision)+",\"experiments\":{\"animationMode\":0,\"logicCore\":"+(logic?"true":"false")+"}}");};
  auto log=[&]{return text(sharedLg+"/GIBox Log.txt");};
  // The frame values the log shows for one model and driver, in order.
  auto values=[&](const std::string& name,const std::string& driver){std::vector<int> out;std::string l=log(),key="animation-frame "+name+" mode=cached driver="+driver;
   for(size_t at=l.find(key);at!=std::string::npos;at=l.find(key,at+1)){auto v=l.find(" value=",at);out.push_back(std::stoi(l.substr(v+7)));}return out;};
  Runtime logic(e,pkg,root+"/logic-private",mediaL,false);uint64_t now=6000000;control("Logic",true);
  for(int n=0;n<25;n++){logic.tick(now);now+=120;}for(int n=0;n<9;n++)logic.tick(now++);
  assert(log().find("logic-start Logic: 1 INI")!=std::string::npos&&log().find("logic-error")==std::string::npos);
  auto seen=values("Logic","logic");assert(seen.size()>=6);for(size_t i=0;i<seen.size();i++)assert(seen[i]==int((i+1)%3));   // as FrameLoop: 1, 2, 0, ...
  control("Logic",false);now+=200;for(int n=0;n<6;n++)logic.tick(now++);assert(log().find("logic-off Logic: switched off; FrameLoop from here (frame value")!=std::string::npos&&log().find("logicCore=off")!=std::string::npos&&!values("Logic","frameloop").empty());   // the frame log stops after 32 updates
  control("Miss",true);now+=200;for(int n=0;n<25;n++){logic.tick(now);now+=120;}for(int n=0;n<8;n++)logic.tick(now++);
  auto missed=values("Miss","logic");assert(missed.size()>=4);for(int v:missed)assert(v!=2);   // frame 2's resources are not the ones selected: 1 stays
  assert(log().find("logic-miss Miss: the draws select what nothing prebuilt has; frame value 1 stays. closest prebuilt frame index 0 (0 of 1 parts equal); Part: prebuilt "+fixture::logicSignature("file:a.ib")+" selected "+fixture::logicSignature("file:c.ib"))!=std::string::npos);
  control("Early",true);now+=200;for(int n=0;n<25;n++){logic.tick(now);now+=120;}for(int n=0;n<6;n++)logic.tick(now++);
  assert(log().find("logic-error Early: what the draws select at the start matches no prebuilt frame; FrameLoop kept.")!=std::string::npos&&values("Early","frameloop").size()>=4&&values("Early","logic").empty());
  control("Plain",true);now+=200;for(int n=0;n<25;n++){logic.tick(now);now+=120;}for(int n=0;n<6;n++)logic.tick(now++);
  assert(log().find("logic-unavailable Plain: converted without logic data")!=std::string::npos&&values("Plain","frameloop").size()>=4);
  logic.shutdown();assert(meshOf(renderer)==original);}
 // experiments.logicCore for an outfit model: the App's outfit value goes into the INI logic, and the converted outfit
 // whose draws match what the logic selects is shown; here the mod's own Present turns $look = 1 into 2. Off: the
 // App's value again. Not shown to be finite, or no such variable in the INI: the App's value, with the reason.
 {auto mediaO=root+"/outfit-media",sharedO=mediaO+"/"+pkg+"/GIBox";
  auto outfit=[&](const std::string& name,const std::string& ini){std::string dir=sharedO+"/models/"+name;std::filesystem::create_directories(dir+"/logic");write(dir+"/logic/0.ini",ini);
   Spec s;s.version=2;s.textures=true;s.extraMaterials=2;s.withTarget(fixture::target("Body"));s.variants="{\"vars\":[{\"name\":\"$look\",\"values\":[0,1,2]}],\"default\":{\"$look\":0}}";
   s.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[0]]}},{\"start\":0,\"count\":3,\"material\":\"Material1\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[1]]}},{\"start\":0,\"count\":3,\"material\":\"Material2\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[2]]}}]"};
   std::string variants;for(int i=0;i<3;i++)variants+=(i?",":"")+std::string("{\"assignment\":{\"$look\":")+std::to_string(i)+"},\"draws\":{\"Part\":"+quote(fixture::logicDraw("file:l"+std::to_string(i)+".ib"))+"}}";
   std::string fields;for(std::string f:{"ib","position","blend","texcoord","ps-t0","ps-t1","ps-t2"})fields+=(fields.empty()?"":",")+("\""+f+"\":[\"Part\",\""+(f=="ib"||f.rfind("ps-",0)==0?f:std::string("vb0"))+"\"]");
   std::string logic="{\"signature\":2,\"main\":\"Mods\\\\Outfit\\\\a.ini\",\"files\":[{\"namespace\":\"Mods\\\\Outfit\\\\a.ini\",\"file\":\"logic/0.ini\"}],\"resources\":{\"resource\\\\mods\\\\outfit\\\\a.ini\\\\l0\":\"file:l0.ib\",\"resource\\\\mods\\\\outfit\\\\a.ini\\\\l1\":\"file:l1.ib\",\"resource\\\\mods\\\\outfit\\\\a.ini\\\\l2\":\"file:l2.ib\"},"
    "\"passes\":[{\"name\":\"Part\",\"bindings\":{\"ib\":\"aaaa0001\"},\"checks\":[\"ib\"]}],\"fields\":{\"Part\":{"+fields+"}},\"variants\":["+variants+"]}";
   s.edit=[logic](std::string& t){t.insert(t.rfind('}'),",\"logic\":"+logic);};writeModel(dir,s);};
  const std::string body="[TextureOverridePart]\nhash = aaaa0001\nhandling = skip\nif $look == 0\n ib = ResourceL0\nelif $look == 1\n ib = ResourceL1\nelse\n ib = ResourceL2\nendif\ndrawindexed = auto\n[ResourceL0]\nfilename = l0.ib\n[ResourceL1]\nfilename = l1.ib\n[ResourceL2]\nfilename = l2.ib\n";
  outfit("Outfit","[Constants]\nglobal $look = 0\n[Present]\nif $look == 1\n $look = 2\nendif\n"+body);
  outfit("Counter","[Constants]\nglobal $look = 0\n[Present]\n$look = $look + 1\n"+body);
  outfit("Missing","[Constants]\nglobal $other = 0\n[TextureOverridePart]\nhash = aaaa0001\nhandling = skip\nib = ResourceL0\ndrawindexed = auto\n[ResourceL0]\nfilename = l0.ib\n");
  Ptr full=f->mesh(),w=f->renderer(full);f->world(w);f->scene={w};int revision=0;
  auto control=[&](const std::string& selected,bool logic,int look){write(sharedO+"/control.json","{\"enabled\":true,\"logLevel\":\"debug\",\"revision\":"+std::to_string(++revision)+",\"selected\":[\""+selected+"\"],\"variables\":{\""+selected+"\":{\"$look\":"+std::to_string(look)+"}},\"experiments\":{\"logicCore\":"+(logic?"true":"false")+"}}");};
  auto log=[&]{return text(sharedO+"/GIBox Log.txt");};
  Runtime outfits(e,pkg,root+"/outfit-private",mediaO,false);uint64_t now=7000000;control("Outfit",true,0);for(int n=0;n<12;n++){outfits.tick(now);now+=600;}
  assert(log().find("logic-start Outfit: 1 INI")!=std::string::npos&&log().find("draw variables finite")!=std::string::npos&&log().find("first outfit $look=0")!=std::string::npos);
  control("Outfit",true,1);for(int n=0;n<8;n++){outfits.tick(now);now+=600;}
  assert(log().find("variant Outfit $look=2")!=std::string::npos&&log().find("logic-outfit Outfit")!=std::string::npos);   // the App asked for 1; the mod's Present made it 2
  control("Outfit",false,1);for(int n=0;n<8;n++){outfits.tick(now);now+=600;}
  assert(log().find("logic-off Outfit: switched off; the App's outfit choice from here")!=std::string::npos&&log().rfind("variant Outfit $look=1")>log().find("variant Outfit $look=2"));
  control("Counter",true,0);for(int n=0;n<12;n++){outfits.tick(now);now+=600;}
  assert(log().find("logic-unavailable Counter: the outfit logic is not shown to be finite (")!=std::string::npos&&log().find("a counter?")!=std::string::npos);
  control("Missing",true,0);for(int n=0;n<12;n++){outfits.tick(now);now+=600;}
  assert(log().find("logic-error Missing: the outfit variable $look is not in the INI logic; the App's outfit choice stays")!=std::string::npos);
  outfits.shutdown();assert(meshOf(w)==full);}
 // App keys and persist (logicCore): a press is a key event of the core (cycle, toggle with its saved value), taken once by
 // its number; presses already in control.json when the game starts are not replayed. The persist variable is saved
 // to logic-persist/<model>.json (temporary file, renamed) and restored when the logic starts again, over the App's
 // table; values saved for another INI are not restored.
 {auto mediaK=root+"/keys-media",sharedK=mediaK+"/"+pkg+"/GIBox",dir=sharedK+"/models/Keys";std::filesystem::create_directories(dir+"/logic");
  write(dir+"/logic/0.ini","[Constants]\nglobal persist $look = 0\n[KeyLook]\nkey = VK_UP\ntype = cycle\n$look = 0,1,2\n[KeyFlip]\nkey = VK_DOWN\ntype = toggle\n$look = 2\n[KeyHold]\nkey = VK_LEFT\ntype = hold\n$look = 2\n"
   "[TextureOverridePart]\nhash = aaaa0001\nhandling = skip\nif $look == 0\n ib = ResourceL0\nelif $look == 1\n ib = ResourceL1\nelse\n ib = ResourceL2\nendif\ndrawindexed = auto\n[ResourceL0]\nfilename = l0.ib\n[ResourceL1]\nfilename = l1.ib\n[ResourceL2]\nfilename = l2.ib\n");
  Spec sp;sp.version=2;sp.textures=true;sp.extraMaterials=2;sp.withTarget(fixture::target("Body"));sp.variants="{\"vars\":[{\"name\":\"$look\",\"values\":[0,1,2]}],\"default\":{\"$look\":0}}";
  sp.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[0]]}},{\"start\":0,\"count\":3,\"material\":\"Material1\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[1]]}},{\"start\":0,\"count\":3,\"material\":\"Material2\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[2]]}}]"};
  std::string variants;for(int i=0;i<3;i++)variants+=(i?",":"")+std::string("{\"assignment\":{\"$look\":")+std::to_string(i)+"},\"draws\":{\"Part\":"+quote(fixture::logicDraw("file:l"+std::to_string(i)+".ib"))+"}}";
  std::string fields;for(std::string f:{"ib","position","blend","texcoord","ps-t0","ps-t1","ps-t2"})fields+=(fields.empty()?"":",")+("\""+f+"\":[\"Part\",\""+(f=="ib"||f.rfind("ps-",0)==0?f:std::string("vb0"))+"\"]");
  std::string logic="{\"signature\":2,\"main\":\"Mods\\\\Keys\\\\a.ini\",\"files\":[{\"namespace\":\"Mods\\\\Keys\\\\a.ini\",\"file\":\"logic/0.ini\"}],\"resources\":{\"resource\\\\mods\\\\keys\\\\a.ini\\\\l0\":\"file:l0.ib\",\"resource\\\\mods\\\\keys\\\\a.ini\\\\l1\":\"file:l1.ib\",\"resource\\\\mods\\\\keys\\\\a.ini\\\\l2\":\"file:l2.ib\"},"
   "\"passes\":[{\"name\":\"Part\",\"bindings\":{\"ib\":\"aaaa0001\"},\"checks\":[\"ib\"]}],\"fields\":{\"Part\":{"+fields+"}},\"variants\":["+variants+"],"
   "\"keys\":[{\"file\":0,\"section\":\"KeyLook\",\"type\":\"cycle\"},{\"file\":0,\"section\":\"KeyFlip\",\"type\":\"toggle\"}]}";
  sp.edit=[logic](std::string& t){t.insert(t.rfind('}'),",\"logic\":"+logic);};writeModel(dir,sp);
  Ptr full=f->mesh(),w=f->renderer(full);f->world(w);f->scene={w};int revision=0;std::string presses;int seq=0;
  auto control=[&](bool logic){write(sharedK+"/control.json","{\"enabled\":true,\"logLevel\":\"debug\",\"revision\":"+std::to_string(++revision)+",\"selected\":[\"Keys\"],\"variables\":{\"Keys\":{\"$look\":0}},\"keys\":{\"Keys\":["+presses+"]},\"experiments\":{\"logicCore\":"+(logic?"true":"false")+"}}");};
  auto press=[&](const std::string& section){presses+=std::string(presses.empty()?"":",")+"{\"seq\":"+std::to_string(++seq)+",\"file\":0,\"section\":\""+section+"\"}";};
  auto log=[&]{return text(sharedK+"/GIBox Log.txt");};auto persisted=sharedK+"/logic-persist/Keys.json";
  Runtime keys(e,pkg,root+"/keys-private",mediaK,false);uint64_t now=7500000;
  auto run=[&](int n){for(int i=0;i<n;i++){keys.tick(now);now+=600;}};
  press("KeyLook");control(true);run(12);   // a press made before this game start
  assert(log().find("logic-start Keys: 1 INI")!=std::string::npos&&log().find("first outfit $look=0")!=std::string::npos&&log().find("logic-key")==std::string::npos);
  press("KeyLook");control(true);run(6);
  assert(log().find("logic-key Keys: KeyLook (cycle) pressed, press 2")!=std::string::npos&&log().find("variant Keys $look=1")!=std::string::npos);
  run(4);assert(exists(persisted)&&!exists(persisted+".tmp"));
  {Json p=JsonParser(text(persisted)).parse();assert(p.at("main").string=="Mods\\Keys\\a.ini"&&p.at("values").object.size()==1&&p.at("values").at("$\\mods\\keys\\a.ini\\look").number==1);}
  press("KeyFlip");control(true);run(6);assert(log().find("logic-key Keys: KeyFlip (toggle) pressed, press 3")!=std::string::npos&&log().find("variant Keys $look=2")!=std::string::npos);
  press("KeyFlip");control(true);run(6);assert(log().rfind("variant Keys $look=1")>log().find("variant Keys $look=2"));   // the toggle puts back what it saved
  press("KeyHold");press("KeyNone");control(true);run(6);
  assert(log().find("logic-key Keys: KeyHold is not a cycle or toggle key of this model; not pressed")!=std::string::npos&&log().find("logic-key Keys: KeyNone is not a cycle or toggle key")!=std::string::npos);
  control(false);run(6);assert(log().find("logic-off Keys: switched off; the App's outfit choice from here")!=std::string::npos&&log().rfind("variant Keys $look=0")>log().rfind("variant Keys $look=1"));
  press("KeyLook");control(false);run(4);assert(log().find("logic-key Keys: KeyLook not pressed: the logic core does not run this model")!=std::string::npos);
  // Again on: the saved $look = 1 stands over the App's 0.
  auto before=log().size();control(true);run(12);auto again=log().substr(before);
  assert(again.find("logic-persist Keys: 1 of 1 saved values restored")!=std::string::npos&&again.find("first outfit $look=1")!=std::string::npos&&again.find("variant Keys $look=1")!=std::string::npos);
  // Saved for another INI under this model name: not restored.
  control(false);run(6);{std::string t=text(persisted);t.replace(t.find("Mods\\\\Keys"),10,"Mods\\\\Else");write(persisted,t);}
  before=log().size();control(true);run(12);again=log().substr(before);
  assert(again.find("logic-persist Keys: the saved values belong to another INI (Mods\\Else\\a.ini); not restored")!=std::string::npos&&again.find("first outfit $look=0")!=std::string::npos);
  keys.shutdown();assert(meshOf(w)==full);}
 // A driven outfit model (the converter: its Present program follows the clock): the program picks the outfit, the
 // App's table is not pushed, ] / [ move the selection (back = is the App's "previous"), and Q shows outfit 2 for 3 s
 // of game time, then the selection again by itself.
 {auto mediaD=root+"/driven-media",sharedD=mediaD+"/"+pkg+"/GIBox",dir=sharedD+"/models/Driven";std::filesystem::create_directories(dir+"/logic");
  write(dir+"/logic/0.ini","[Constants]\nglobal persist $sel = 0\nglobal $look = 0\nglobal $press = 0\nglobal $start = -100\n[KeySel]\nkey = ]\nback = [\ntype = cycle\n$sel = 0,1\n[KeyQ]\nkey = q\ntype = cycle\n$press = 1\n"
   "[Present]\nif $press == 1\n $start = time\n $press = 0\nendif\nif time - $start < 3\n $look = 2\nelse\n $look = $sel\nendif\n"
   "[TextureOverridePart]\nhash = aaaa0001\nhandling = skip\nif $look == 0\n ib = ResourceL0\nelif $look == 1\n ib = ResourceL1\nelse\n ib = ResourceL2\nendif\ndrawindexed = auto\n[ResourceL0]\nfilename = l0.ib\n[ResourceL1]\nfilename = l1.ib\n[ResourceL2]\nfilename = l2.ib\n");
  Spec sp;sp.version=2;sp.textures=true;sp.extraMaterials=2;sp.withTarget(fixture::target("Body"));sp.variants="{\"vars\":[{\"name\":\"$look\",\"values\":[0,1,2]}],\"default\":{\"$look\":0}}";
  sp.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[0]]}},{\"start\":0,\"count\":3,\"material\":\"Material1\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[1]]}},{\"start\":0,\"count\":3,\"material\":\"Material2\",\"slot\":0,\"when\":{\"vars\":[\"$look\"],\"allow\":[[2]]}}]"};
  std::string variants;for(int i=0;i<3;i++)variants+=(i?",":"")+std::string("{\"assignment\":{\"$look\":")+std::to_string(i)+"},\"draws\":{\"Part\":"+quote(fixture::logicDraw("file:l"+std::to_string(i)+".ib"))+"}}";
  std::string fields;for(std::string f:{"ib","position","blend","texcoord","ps-t0","ps-t1","ps-t2"})fields+=(fields.empty()?"":",")+("\""+f+"\":[\"Part\",\""+(f=="ib"||f.rfind("ps-",0)==0?f:std::string("vb0"))+"\"]");
  std::string logic="{\"signature\":2,\"driven\":true,\"main\":\"Mods\\\\Driven\\\\a.ini\",\"files\":[{\"namespace\":\"Mods\\\\Driven\\\\a.ini\",\"file\":\"logic/0.ini\"}],\"resources\":{\"resource\\\\mods\\\\driven\\\\a.ini\\\\l0\":\"file:l0.ib\",\"resource\\\\mods\\\\driven\\\\a.ini\\\\l1\":\"file:l1.ib\",\"resource\\\\mods\\\\driven\\\\a.ini\\\\l2\":\"file:l2.ib\"},"
   "\"passes\":[{\"name\":\"Part\",\"bindings\":{\"ib\":\"aaaa0001\"},\"checks\":[\"ib\"]}],\"fields\":{\"Part\":{"+fields+"}},\"variants\":["+variants+"],"
   "\"keys\":[{\"file\":0,\"section\":\"KeySel\",\"type\":\"cycle\",\"back\":true},{\"file\":0,\"section\":\"KeyQ\",\"type\":\"cycle\"}]}";
  sp.edit=[logic](std::string& t){t.insert(t.rfind('}'),",\"logic\":"+logic);};writeModel(dir,sp);
  Ptr full=f->mesh(),w=f->renderer(full);f->world(w);f->scene={w};int revision=0;std::string presses;int seq=0;
  auto control=[&]{write(sharedD+"/control.json","{\"enabled\":true,\"logLevel\":\"debug\",\"revision\":"+std::to_string(++revision)+",\"selected\":[\"Driven\"],\"variables\":{\"Driven\":{\"$look\":1}},\"keys\":{\"Driven\":["+presses+"]},\"experiments\":{\"logicCore\":true}}");};
  auto press=[&](const std::string& section,bool back){presses+=std::string(presses.empty()?"":",")+"{\"seq\":"+std::to_string(++seq)+",\"file\":0,\"section\":\""+section+"\""+(back?",\"back\":true":"")+"}";control();};
  auto log=[&]{return text(sharedD+"/GIBox Log.txt");};
  Runtime driven(e,pkg,root+"/driven-private",mediaD,false);uint64_t now=8000000;auto run=[&](int n){for(int i=0;i<n;i++){driven.tick(now);now+=600;}};
  control();run(12);
  assert(log().find("logic-start Driven: 1 INI")!=std::string::npos&&log().find("driven by its own program")!=std::string::npos&&log().find("first outfit $look=0")!=std::string::npos);
  assert(log().find("variant Driven $look=1")==std::string::npos);   // the App's table says 1; the program says $sel = 0
  press("KeySel",false);run(4);assert(log().find("logic-key Driven: KeySel (cycle) pressed")!=std::string::npos&&log().find("variant Driven $look=1")!=std::string::npos);
  press("KeySel",true);run(4);assert(log().find("logic-key Driven: KeySel (cycle, back) pressed")!=std::string::npos&&log().rfind("variant Driven $look=0")>log().find("variant Driven $look=1"));
  press("KeyQ",false);run(3);auto at=log().size();assert(log().find("variant Driven $look=2")!=std::string::npos);
  run(8);assert(log().substr(at).find("variant Driven $look=0")!=std::string::npos);   // 3 s later: the selection again
  press("KeyQ",true);run(2);assert(log().find("logic-key Driven: KeyQ back is not a cycle or toggle key of this model with back =; not pressed")!=std::string::npos);
  driven.shutdown();assert(meshOf(w)==full);}
 // Missing indexed targets stop before either playback backend. Report LOD and required palette together,
 // keep another character's active mod from masking the wait, and still reject an undersized full mesh.
 {auto mediaA=root+"/animation-gate-media",sharedA=mediaA+"/"+pkg+"/GIBox";Spec source;source.noSkeleton().character(10000107,{"citlali"});
  source.edit=[](std::string& j){fixture::replaceIn(j,"\"bones\":1,\"vertices\"","\"bones\":2,\"vertices\"");};for(int i=0;i<3;i++)put32(source.weights.data()+32*i+16,1);
  fixture::writeModel(sharedA+"/models/Animated",fixture::animated(source));Spec otherSource;otherSource.character(10000106).withTarget(fixture::target("Else",4));writeModel(sharedA+"/models/Other",otherSource);
  Ptr low=f->mesh("Body_LOD1",2),renderer=f->renderer(low),otherMesh=f->mesh("Else",4),other=f->renderer(otherMesh,"BoneA","Body",f->kavehRoot);f->world(renderer);f->scene={renderer,other};
  Runtime animated(e,pkg,root+"/animation-gate-private",mediaA,false);uint64_t now=5100000;
  auto control=[&](int mode){write(sharedA+"/control.json","{\"enabled\":true,\"selected\":[\"Other\",\"Animated\"],\"logLevel\":\"debug\",\"experiments\":{\"animationMode\":"+std::to_string(mode)+"}}");};
  auto log=[&]{return text(sharedA+"/GIBox Log.txt");};control(0);for(int n=0;n<8;n++){animated.tick(now);now+=600;}
  auto st=JsonParser(text(sharedA+"/status.json")).parse();auto note=log();
  assert(st.at("state").string=="waiting"&&st.at("models").array.size()==1&&st.at("models").array[0].string=="Other"&&meshOf(other)!=otherMesh&&meshOf(renderer)==low);
  assert(st.at("detail").string.find("按索引绑定至少 2 根骨骼")!=std::string::npos&&st.at("detail").string.find("不能据此认定游戏必须有同样多的骨骼")!=std::string::npos&&note.find("animation-wait Animated mode=cached stage=target-not-learned playback=not-started")!=std::string::npos&&note.find("Body_LOD1（顶点 2，子网格 1，骨骼 1）")!=std::string::npos&&note.find("animation-ready Animated")==std::string::npos);
  auto once=note.find("animation-wait Animated");assert(note.find("animation-wait Animated",once+1)==std::string::npos);unsigned clones=f->meshClones;
  control(1);animated.tick(now);now+=600;assert(log().find("animation-wait Animated mode=dynamic")!=std::string::npos&&f->meshClones==clones);
  control(2);animated.tick(now);now+=600;assert(log().find("animation-wait Animated mode=paused")!=std::string::npos&&f->meshClones==clones);
  Ptr full=f->mesh();f->obj[renderer].mesh=full;control(0);for(int n=0;n<3;n++){animated.tick(now);now+=600;}
  st=JsonParser(text(sharedA+"/status.json")).parse();assert(meshOf(renderer)==full&&st.at("detail").string.find("当前完整网格只有 1 根")!=std::string::npos&&f->meshClones==clones&&log().find("animation-ready Animated")==std::string::npos);
  auto addBone=[&](Ptr r,Ptr mesh){Ptr b1=load<Ptr>(f->obj[r].bones+0x20),b2=f->object("bone","BoneB");f->obj[b2].parent=b1;Ptr bs[]={b1,b2};f->obj[r].bones=f->array(8,2,bs);Bytes binds(128);for(int n=0;n<2;n++)for(int j=0;j<4;j++)store<float>(reinterpret_cast<Ptr>(binds.data())+n*64+j*20,1);f->obj[mesh].arrays[abi::getBindposes]=f->array(64,2,binds.data());store<uint64_t>(f->obj[mesh].shared+0xe0,2);};
  f->obj[renderer].mesh=low;Ptr previewMesh=f->mesh(),preview=f->renderer(previewMesh);addBone(preview,previewMesh);f->scene.push_back(preview);
  // The world mesh has an incompatible one-bone palette; the same avatar's full preview can establish the target.
  for(int n=0;n<25;n++){animated.tick(now);now+=120;}assert(meshOf(renderer)==low&&meshOf(preview)!=previewMesh&&log().find("target-learned Animated/Part0 by name citlali")!=std::string::npos&&log().find("animation-ready Animated mode=cached")!=std::string::npos);
  f->obj[renderer].mesh=full;addBone(renderer,full);for(int n=0;n<8;n++){animated.tick(now);now+=120;}assert(meshOf(renderer)==meshOf(preview));
  animated.shutdown();assert(meshOf(renderer)==full&&meshOf(preview)==previewMesh&&meshOf(other)==otherMesh&&f->handles.empty());}
 // 52. Texture swaps (model.json "textures"): the game's Face renderer under the attached Body's character root keeps its
 //     mesh and shows the mod's face texture, written into a runtime material, and so does a Brow showing the same
 //     game texture (as on PC, where the texture is replaced by its hash). A game copy of ours and a fresh material
 //     the game puts on are handled on the next scan; switching the model off puts every original back. A Face under
 //     another character is left alone. A loaded asset gets a copy, which goes with the renderer.
 {std::string mediaFace=root+"/mediaFace",sharedFace=mediaFace+"/"+pkg+"/GIBox";Spec s;s.textures=true;s.extraMaterials=1;s.withTarget(fixture::target("Body"));
  s.edit=[](std::string& j){fixture::replaceIn(j,",\"parts\":[",",\"textures\":[{\"renderer\":\"Face\",\"material\":\"Material1\"}],\"parts\":[");};writeModel(sharedFace+"/models/FaceMod",s);
  auto control=[&](bool on){write(sharedFace+"/control.json.tmp",std::string("{\"enabled\":true,\"selected\":[")+(on?"\"FaceMod\"":"")+"]}");std::filesystem::rename(sharedFace+"/control.json.tmp",sharedFace+"/control.json");};
  auto has=[&](const std::string& x){return text(sharedFace+"/GIBox Log.txt").find(x)!=std::string::npos;};auto runtimeMade=[&](Ptr m){store<uint32_t>(load<Ptr>(m+0x10)+8,uint32_t(-7));};
  Ptr original=f->mesh(),body=f->renderer(original),faceMesh=f->mesh("Face"),face=f->renderer(faceMesh,"BoneA","Face"),other=f->renderer(f->mesh("Face"),"BoneA","Face",f->kavehRoot);
  Ptr brow=f->renderer(f->mesh("Brow"),"BoneA","Brow"),otherBrow=f->renderer(f->mesh("Brow"),"BoneA","Brow",f->kavehRoot);   // they show the face texture too
  Ptr faceArray=f->obj[face].materials,faceMat=load<Ptr>(faceArray+0x20),otherMat=load<Ptr>(f->obj[other].materials+0x20),gameTex=f->object("texture","Face diffuse");
  Ptr browMat=load<Ptr>(f->obj[brow].materials+0x20),otherBrowMat=load<Ptr>(f->obj[otherBrow].materials+0x20);f->diffuse[browMat]=gameTex;f->diffuse[otherBrowMat]=gameTex;runtimeMade(browMat);runtimeMade(otherBrowMat);
  f->diffuse[faceMat]=gameTex;f->diffuse[otherMat]=gameTex;runtimeMade(faceMat);runtimeMade(otherMat);f->world(body);f->scene={body,face,other,brow,otherBrow};
  control(true);
  {Runtime fr(e,pkg,root+"/privateFace",mediaFace,false);fr.tick(4800000);fr.tick(4800600);Ptr ours=f->diffuse[faceMat];
   assert(meshOf(body)!=original&&meshOf(face)==faceMesh&&f->obj[face].materials==faceArray&&alive(ours)&&ours!=gameTex&&f->diffuse[otherMat]==gameTex);   // written, mesh kept
   assert(has("texture-swap FaceMod/Face on Face: slot 0 Material: written (instance id -7)"));
   assert(f->diffuse[browMat]==ours&&f->diffuse[otherBrowMat]==gameTex&&has("texture-swap FaceMod/Face: Brow shows the game's face texture too; it takes ours, as on PC"));   // as on PC; not under another character
   Ptr copy=clone(faceMat,0);f->obj[copy].name="Material (Instance)";f->obj[face].materials=f->typed("material",copy);fr.tick(4801200);fr.tick(4801800);   // the game's copy of ours
   assert(f->diffuse[copy]==ours&&has("texture-swap FaceMod/Face: the game's copy Material (Instance) carries our texture"));
   Ptr fresh=f->object("material","Material");f->diffuse[fresh]=gameTex;runtimeMade(fresh);f->obj[face].materials=f->typed("material",fresh);fr.tick(4802400);fr.tick(4803000);   // a fresh one
   assert(f->diffuse[fresh]==ours);
   control(false);fr.tick(4803600);fr.tick(4804200);                                                             // the model no longer selected
   assert(meshOf(body)==original&&f->diffuse[faceMat]==gameTex&&f->diffuse[copy]==gameTex&&f->diffuse[fresh]==gameTex&&f->diffuse[otherMat]==gameTex&&f->diffuse[browMat]==gameTex);
   assert(has("texture-swap FaceMod/Face (model off): original texture back in 3 game materials, 0 copies taken off"));fr.shutdown();assert(f->handles.empty());}
  Ptr body2=f->renderer(original),face2=f->renderer(f->mesh("Face"),"BoneA","Face"),asset=load<Ptr>(f->obj[face2].materials+0x20);f->diffuse[asset]=gameTex;f->world(body2);f->scene={body2,face2};
  control(true);
  {Runtime f2(e,pkg,root+"/privateFace2",mediaFace,false);f2.tick(4900000);f2.tick(4900600);Ptr on=load<Ptr>(f->obj[face2].materials+0x20);
   assert(meshOf(body2)!=original&&on!=asset&&alive(f->diffuse[on])&&f->diffuse[on]!=gameTex&&f->diffuse[asset]==gameTex&&has("slot 0 Material: copied (a loaded asset (instance id "));
   f2.shutdown();assert(load<Ptr>(f->obj[face2].materials+0x20)==asset&&!alive(on)&&f->diffuse[asset]==gameTex&&has("original texture back in 0 game materials, 1 copies taken off")&&f->handles.empty());}}
 // 40. Texture-only mods, phase 1: a texture-diag file for the controlled character gives one renderer / material /
 //     match line each (here without IL2CPP exports: the per-submesh layout is reported unavailable and the rule is
 //     matched by name and counts, never confirmed), nothing is written to the game, and nothing repeats while nothing
 //     changes; another material array is read and logged again; a file for another character only waits.
 {std::string mediaDiag=root+"/mediaDiag",sharedDiag=mediaDiag+"/"+pkg+"/GIBox",logFile=sharedDiag+"/GIBox Log.txt";std::filesystem::create_directories(sharedDiag+"/texture-diag");
  write(sharedDiag+"/control.json","{\"enabled\":true,\"selected\":[]}");
  Ptr diagEntity=load<Ptr>(load<Ptr>(load<Ptr>(f->manager+0x10)+0x10)+0x20);store<uint32_t>(diagEntity+0x3a4,10000107);
  Ptr body=f->renderer(f->mesh("Body",3)),mat=load<Ptr>(f->obj[body].materials+0x20),tex=f->object("texture","Avatar_Tex_Body_Diffuse");f->diffuse[mat]=tex;f->world(body);
  write(sharedDiag+"/texture-diag/Mod.json",R"J({"format":"gibox-texture-diag","version":1,"name":"Mod","ini":"Mod.ini","character":10000107,"rules":[
   {"section":"TextureOverrideModBodyA","part":"ModBodyA","hint":"body","slotLetter":0,"hash":"d5ae71a3","first":0,"kind":"segment","textures":[{"slot":"resource\\gimi\\diffuse","resource":"rbody","role":"diffuse","condition":"","via":""}]},
   {"section":"TextureOverrideModPos","part":"ModPos","hint":"pos","hash":"0e33fe05","first":-1,"kind":"control","textures":[]}],"resources":[],"variables":[],"otherInis":[],"notes":[]})J");
  write(sharedDiag+"/texture-diag/Other.json",R"J({"format":"gibox-texture-diag","version":1,"name":"Other","character":10000200,"rules":[]})J");
  write(sharedDiag+"/texture-diag/Broken.json","{");
  unsigned writes=gameWrites,clones=f->clones;
  Runtime td(e,pkg,root+"/privateDiag",mediaDiag,false);for(uint64_t now=5000000;now<=5004000;now+=500)td.tick(now);auto log=text(logFile);
  auto count=[&](const std::string& x){size_t n=0;for(size_t p=log.find(x);p!=std::string::npos;p=log.find(x,p+1))n++;return n;};
  assert(gameWrites==writes&&f->clones==clones&&f->diffuse[mat]==tex&&load<Ptr>(f->obj[body].materials+0x20)==mat);
  assert(count("texture-diag-ini Mod: TextureOverrideModBodyA hash=d5ae71a3 match_first_index=0 priority=0 kind=segment part=body letter=A")==1);
  // No IL2CPP exports here, as in the game: the engine's own Mesh / Texture getters give the layout and the sizes.
  assert(count("texture-diag-renderer character=10000107 renderer=Body#")==1&&count("lod=full (by name) vertices=3 submeshes(native)=1 subMeshCount=1 vertexCount=3 bones=1 readable=false indexFormat=未采集: this build has no Mesh.get_indexFormat")==1);
  assert(count("layout-source=engine Mesh getters (dump.cs RVAs)")==1&&count("texture-diag-unsupported Body#")==0);
  assert(count("texture-diag-submesh Body#")==1&&count("submesh=0 indexStart=0 indexCount=3 baseVertex=未采集: this build has no Mesh.GetBaseVertex topology=未采集: this build has no Mesh.GetTopology material-slot-by-position=0 (Material)")==1);
  assert(count("texture-diag-material Body#")==1&&count("shader=miHoYo/Character/Body textures=[_MainTex=Avatar_Tex_Body_Diffuse 4x4 4 (RGBA32) mips 1]")==1);
  assert(count("texture-diag-match Mod: TextureOverrideModBodyA hash=d5ae71a3 match_first_index=0 -> 确定匹配 renderer=Body#")==1&&count("-> _MainTex (material has it; current Avatar_Tex_Body_Diffuse 4x4 4 (RGBA32))")==1);
  assert(count("texture-diag-match Mod: 1 renderers of 10000107: 1 确定匹配, 0 高可信候选, 0 存在歧义, 0 无法匹配, 1 without textures; nothing was replaced (phase 1)")==1&&count("TextureOverrideModPos ->")==0);
  assert(count("texture-diag-unsupported "+sharedDiag+"/texture-diag/Broken.json: ")==1&&count("texture-diag-match Other")==0);
  // The game gives the renderer other materials: read and matched again, still nothing written.
  Ptr mat2=f->object("material","Material2");f->diffuse[mat2]=tex;f->obj[body].materials=f->typed("material",mat2);td.tick(5005000);td.tick(5006000);log=text(logFile);
  assert(count("texture-diag-material Body#")==2&&count("material=Material2#")==1&&gameWrites==writes);
  // Another character controlled: only the file for it is considered.
  // The field shows a lower LOD (Body_LOD1, as on the device); the party page has the full mesh of the same avatar:
  // that twin is read and matched in its place, the LOD one only logged. No game writes either way.
  Ptr lodBody=f->renderer(f->mesh("Body_LOD1",2)),fullBody=f->renderer(f->mesh("Body",3));
  for(Ptr r:{lodBody,fullBody}){Ptr m=load<Ptr>(f->obj[r].materials+0x20);f->diffuse[m]=tex;}
  f->world(lodBody);f->scene={lodBody,fullBody};for(uint64_t now=5006500;now<=5009000;now+=500)td.tick(now);log=text(logFile);
  assert(count("mesh=Body_LOD1#")==1&&count("lod=lower (by name) vertices=2")==1&&count("full-LOD twin of Body#"+texdiag::hexOf(load<Ptr>(lodBody+0x10))+" (elsewhere in the scene), matched in its place")==1);
  assert(count("texture-diag-match Mod: TextureOverrideModBodyA hash=d5ae71a3 match_first_index=0 -> 确定匹配 renderer=Body#"+texdiag::hexOf(load<Ptr>(fullBody+0x10))+" mesh=Body ")==1&&gameWrites==writes);
  f->scene.clear();f->world(body);
  store<uint32_t>(diagEntity+0x3a4,10000200);td.tick(5017000);td.tick(5018000);log=text(logFile);
  assert(count("texture-diag-match Other: 1 renderers of 10000200: 0 确定匹配")==1&&count("texture-diag-match Mod: TextureOverrideModBodyA")==3&&count("mesh=Body_LOD1 submesh")==0);
  store<uint32_t>(diagEntity+0x3a4,10000107);td.shutdown();assert(f->handles.empty());f->world(0);}
 // 41. Texture-only mods (texture.json): with the strategy "direct" the confirmed slot's game material gets our texture,
 //     read back; the game overwriting it is logged and written again; a variant change writes the other texture; the
 //     strategy "clone" first puts the original back (verified), then a copy takes the slot; switching the mod off puts
 //     the material back and destroys the copy; "diagnose" writes nothing; the test rounds save, apply, read back,
 //     observe, restore and verify each strategy, and stop when a restore fails.
 {std::string mediaTex=root+"/mediaTex",sharedTex=mediaTex+"/"+pkg+"/GIBox",modDir=sharedTex+"/models/TexMod",logFile=sharedTex+"/GIBox Log.txt";std::filesystem::create_directories(modDir+"/textures");
  Ptr entity=load<Ptr>(load<Ptr>(load<Ptr>(f->manager+0x10)+0x10)+0x20);store<uint32_t>(entity+0x3a4,10000107);
  Ptr body=f->renderer(f->mesh("Body",3)),mat=load<Ptr>(f->obj[body].materials+0x20),gameTex=f->object("texture","Avatar_Tex_Body_Diffuse");f->diffuse[mat]=gameTex;f->world(body);f->scene={};
  store<uint32_t>(load<Ptr>(mat+0x10)+8,uint32_t(-3));   // the field's materials are the game's instances ("direct" leaves a loaded asset alone)
  Bytes px(16,0x7f);write(modDir+"/textures/0.rgba",px.data(),px.size());write(modDir+"/textures/1.rgba",px.data(),px.size());
  write(modDir+"/texture.json",R"J({"format":"gibox-texture-mod","version":1,"name":"TexMod","generation":"t1","character":{"id":10000107},
   "variants":{"vars":[{"name":"$colour","values":[0,1]}],"default":{"$colour":0}},
   "textures":[{"resource":"rbody","file":"textures/0.rgba","width":2,"height":2},{"resource":"rbody2","file":"textures/1.rgba","width":2,"height":2}],
   "diag":{"format":"gibox-texture-diag","version":1,"name":"TexMod","character":10000107,"rules":[{"section":"TextureOverrideModBodyA","part":"ModBodyA","hint":"body","hash":"d5ae71a3","first":0,"kind":"segment","textures":[
    {"slot":"resource\\gimi\\diffuse","resource":"rbody","role":"diffuse","condition":"$colour == 0","via":""},{"slot":"resource\\gimi\\diffuse","resource":"rbody2","role":"diffuse","condition":"!($colour == 0) && $colour == 1","via":""},
    {"slot":"ps-t3","resource":"rramp","role":"ramp","condition":"","via":""}]}],"resources":[]}})J");
  auto control=[&](const std::string& strategy,bool on,const std::string& extra=""){write(sharedTex+"/control.json","{\"enabled\":true,\"selected\":["+std::string(on?"\"TexMod\"":"")+"],\"textureMods\":{\"strategy\":\""+strategy+"\""+extra+"}"+(extra.find("colour")!=std::string::npos?"":"")+"}");};
  control("direct",true);
  Runtime tm(e,pkg,root+"/privateTex",mediaTex,false);uint64_t t=6000000;auto run=[&](uint64_t ms){for(uint64_t end=t+ms;t<end;t+=250)tm.tick(t);};auto log=[&]{return text(logFile);};
  auto count=[&](const std::string& x){auto l=log();size_t n=0;for(size_t p=l.find(x);p!=std::string::npos;p=l.find(x,p+1))n++;return n;};
  run(3000);Ptr ours=f->diffuse[mat];
  if(std::getenv("TEXMOD_PRINT")){auto l=log();size_t p=0;while((p=l.find("texture-",p))!=std::string::npos){auto e2=l.find('\n',p);std::cerr<<l.substr(p,std::min<size_t>(e2-p,330))<<"\n";p=e2;}}
  assert(ours&&ours!=gameTex&&count("texture-apply -/direct TexMod: TextureOverrideModBodyA -> character=10000107 Body#")==1&&count("evidence=完整 Mesh 特征与 Body#")==1&&count("texture-map TexMod: 1 confirmed mesh(es) saved to")==1&&count("texture-ready TexMod: 2 Texture2D made on the game thread")==1&&count("texture-timing -/direct TexMod: Body#")==1&&count("_MainTex (resource\\gimi\\diffuse rbody, sRGB): Avatar_Tex_Body_Diffuse#")==1&&count("readback=ok")==1);
  assert(count("skipped ps-t3 rramp [ramp]: no confirmed game property (待诊断)")==1&&count("texture-load -/direct TexMod: 2 textures")==1&&JsonParser(text(sharedTex+"/status.json")).parse().at("detail").string=="TexMod：直接改材质，1 / 1 个槽位已写入");
  f->diffuse[mat]=gameTex;run(1000);assert(f->diffuse[mat]==ours&&count("_MainTex overwritten by the game (now Avatar_Tex_Body_Diffuse#")==1&&count("noticed by the per-tick check")==1&&count("ms (1)")==1);
  // Another character in control: the slot stays (this character's own renderer, hidden meanwhile); back again, the
  // texture is already there and nothing is written again.
  size_t restores=count("texture-restore");store<uint32_t>(entity+0x3a4,10000126);run(1000);size_t applies=count("texture-apply");assert(f->diffuse[mat]==ours&&count("(another character)")==0&&count("texture-restore")==restores);
  store<uint32_t>(entity+0x3a4,10000107);run(1000);assert(f->diffuse[mat]==ours&&count("texture-apply")==applies&&count("texture-restore")==restores);
  // $colour = 1: the other texture on the same slot.
  write(sharedTex+"/control.json","{\"enabled\":true,\"selected\":[\"TexMod\"],\"variables\":{\"TexMod\":{\"$colour\":1}},\"textureMods\":{\"strategy\":\"direct\"}}");run(1500);
  Ptr ours2=f->diffuse[mat];assert(ours2&&ours2!=ours&&ours2!=gameTex&&count("(variant changed)")==1&&count("_MainTex (resource\\gimi\\diffuse rbody2, sRGB)")>=1);
  // clone: the original back (verified), then our copy on the slot; the game's material untouched.
  control("clone",true);run(1500);Ptr slot=load<Ptr>(f->obj[body].materials+0x20);if(false){auto l=log();size_t p=0;while((p=l.find("texture-",p))!=std::string::npos){auto e2=l.find('\n',p);std::cerr<<l.substr(p,std::min<size_t>(e2-p,400))<<"\n";p=e2;}}
  assert(f->diffuse[mat]==gameTex&&count("(strategy clone) | _MainTex: back to Avatar_Tex_Body_Diffuse#")==1&&count("(verified)")>=1&&slot!=mat&&alive(slot)&&f->diffuse[slot]&&f->diffuse[slot]!=gameTex&&count("texture-apply -/clone TexMod")==1&&count("slot readback=ok (our copy)")==1);
  // Off: the material back, our copy destroyed.
  control("clone",false);run(1000);assert(load<Ptr>(f->obj[body].materials+0x20)==mat&&!alive(slot)&&f->diffuse[mat]==gameTex&&count("(mod switched off): source material back (verified); copy destroyed")==1);
  // The game makes its own instance from our copy (as the details page's entrance does): it carries our texture, so it
  // stays on the slot and is not copied again (the game goes on driving it). Off: its texture back, the slot left to
  // the game, our copy kept alive while the renderer lives.
  control("clone",true);run(1500);slot=load<Ptr>(f->obj[body].materials+0x20);assert(slot!=mat&&f->diffuse[slot]!=gameTex);
  unsigned made=f->clones;Ptr inst=clone(slot,0);f->obj[inst].name="Material(Clone) (Instance)";store<uint32_t>(load<Ptr>(inst+0x10)+8,uint32_t(-9));f->obj[body].materials=f->typed("material",inst);run(1000);
  assert(load<Ptr>(f->obj[body].materials+0x20)==inst&&f->diffuse[inst]==f->diffuse[slot]&&f->clones==made+1&&count("; it was made from our copy ")==1&&alive(slot));
  f->diffuse[inst]=gameTex;run(500);assert(f->diffuse[inst]!=gameTex&&f->clones==made+1&&count("written again in the game's instance made from ours (not copied)")==1);
  control("clone",false);run(1000);
  assert(load<Ptr>(f->obj[body].materials+0x20)==inst&&f->diffuse[inst]==gameTex&&f->diffuse[slot]==gameTex&&alive(slot)&&count("our earlier copy kept alive")==1&&count("RESTORE FAILED")==0);
  f->obj[body].materials=f->typed("material",mat);
  // LOD migration: the field shows Body_LOD1, the party page the full Body. The slot confirmed on the full mesh is also
  // written on the checked LOD; once the full one is gone it goes back and the LOD keeps the confirmed mapping.
  Ptr lodBody=f->renderer(f->mesh("Body_LOD1",2)),lodMat=load<Ptr>(f->obj[lodBody].materials+0x20);f->diffuse[lodMat]=gameTex;store<uint32_t>(load<Ptr>(lodMat+0x10)+8,uint32_t(-4));f->world(lodBody);f->scene={lodBody,body};
  control("direct",true,",\"lod\":\"migrate\"");run(4000);
  assert(f->diffuse[lodMat]!=gameTex&&f->diffuse[mat]!=gameTex&&count("evidence=迁移自 Body#")==1&&count("（核对：渲染器名、网格 body、avatar、1 子网格 / 1 材质、各槽位材质名）")==1);
  // The full one gets another mesh (a page closed, the renderer reused): no longer a target, it goes back; the LOD keeps it.
  Ptr fullMesh=f->obj[body].mesh;f->obj[body].mesh=f->mesh("Other",3);texRanges[f->obj[body].mesh]={{5,3}};f->scene={lodBody};run(3000);
  assert(f->diffuse[mat]==gameTex&&f->diffuse[lodMat]!=gameTex&&count("(no longer a target) | _MainTex: back to")==1);f->obj[body].mesh=fullMesh;
  control("direct",false,",\"lod\":\"migrate\"");run(1000);assert(f->diffuse[lodMat]==gameTex);f->world(body);f->scene={};run(1000);
  // diagnose: no game writes at all.
  // auto: the game's own instance (instance id < 0) is written in place; a loaded asset is copied first.
  control("auto",true);run(1500);assert(load<Ptr>(f->obj[body].materials+0x20)==mat&&f->diffuse[mat]!=gameTex&&count("auto: the game's own instance, written in place")==1);
  assert(JsonParser(text(sharedTex+"/status.json")).parse().at("detail").string=="TexMod：自动，1 / 1 个槽位已写入");
  control("auto",false);run(1000);assert(f->diffuse[mat]==gameTex);
  store<uint32_t>(load<Ptr>(mat+0x10)+8,7);control("auto",true);run(1500);Ptr acopy=load<Ptr>(f->obj[body].materials+0x20);
  assert(acopy!=mat&&f->diffuse[acopy]!=gameTex&&f->diffuse[mat]==gameTex&&count("auto: a loaded asset, copied")==1);
  control("auto",false);run(1000);assert(load<Ptr>(f->obj[body].materials+0x20)==mat&&f->diffuse[mat]==gameTex);store<uint32_t>(load<Ptr>(mat+0x10)+8,uint32_t(-3));
  control("diagnose",true);unsigned writes=gameWrites;run(2000);assert(gameWrites==writes&&f->diffuse[mat]==gameTex&&JsonParser(text(sharedTex+"/status.json")).parse().at("detail").string=="TexMod：只诊断，不写入（调试页里可换替换方式）");
  // Test rounds: direct, then clone; each restore verified against the saved slot.
  control("diagnose",true,",\"testRequest\":1,\"testStrategies\":[\"direct\",\"clone\"],\"observeMs\":1000");run(6000);
  assert(count("texture-test T1 start: direct -> clone")==1&&count("texture-test T1-1 direct baseline: Body#")==1&&count("texture-apply T1-1/direct TexMod")==1&&count("texture-apply T1-2/clone TexMod")==1);
  assert(count("T1-1 direct restore: ok")==1&&count("T1-2 clone restore: ok")==1&&count("texture-test T1 done: 2 rounds, every restore verified")==1&&f->diffuse[mat]==gameTex&&load<Ptr>(f->obj[body].materials+0x20)==mat&&gameWrites>writes);
  // A restore that cannot be done (the source material destroyed under our copy): the run stops, our copy stays alive.
  control("diagnose",true,",\"testRequest\":2,\"testStrategies\":[\"clone\",\"direct\"],\"observeMs\":1000");run(600);Ptr copy=load<Ptr>(f->obj[body].materials+0x20);assert(copy!=mat&&count("texture-apply T2-1/clone")==1);
  Ptr nat=load<Ptr>(mat+0x10);store<Ptr>(mat+0x10,0);run(3000);
  assert(count("the source material is destroyed; our copy stays on the slot")==1&&count("T2-1 clone restore: FAILED")==1&&count("texture-test T2 stopped: a restore failed (rounds left: 1)")==1&&alive(copy)&&count("texture-apply T2-2")==0);
  if(std::getenv("TEXMOD_PRINT")){auto l=log();size_t p=0;while((p=l.find("texture-test",p))!=std::string::npos){auto e2=l.find('\n',p);std::cerr<<l.substr(p,std::min<size_t>(e2-p,600))<<"\n";p=e2;}}
  store<Ptr>(mat+0x10,nat);
  // Index-buffer hashes (vk_capture.hpp): an upload whose hash a rule names is logged with the meshes of its index count;
  // the Body mesh confirmed by its submesh starts (3 indices) is shown next to the uploads of that count, every hash listed.
  {auto& T=ms::vkcap::tracker();T.installed(true,"");uint16_t idx[3]={0,1,2};uint8_t staging[6];std::memcpy(staging,idx,6);
   T.created(0x100,6,0x40);T.created(0x200,6,0x1);T.bound(0x200,0x300,0);T.mapped(0x300,0,6,staging);ms::vkcap::Copy c{0,0,6};T.copied(0x200,0x100,&c,1);
   char hx[12];std::snprintf(hx,sizeof hx,"%08x",ms::vkcap::ibHashes(staging,6)[1].value);std::filesystem::create_directories(sharedTex+"/models/IbMod");
   write(sharedTex+"/models/IbMod/texture.json",std::string(R"J({"format":"gibox-texture-mod","version":1,"name":"IbMod","generation":"i1","character":{"id":10000107},"variants":{"vars":[],"default":{}},"textures":[],
    "diag":{"format":"gibox-texture-diag","version":1,"name":"IbMod","character":10000107,"rules":[{"section":"TextureOverrideIb","part":"Ib","hint":"ib","hash":")J")+hx+R"J(","first":0,"kind":"segment","textures":[]}],"resources":[]}})J");
   run(3000);
   if(std::getenv("TEXMOD_PRINT")){auto l=log();size_t p=0;while((p=l.find("ib-",p))!=std::string::npos){auto e2=l.find('\n',p);std::cerr<<l.substr(p,std::min<size_t>(e2-p,500))<<"\n";p=e2;}}
   assert(count(std::string("ib-hash ")+hx+" (IbMod/TextureOverrideIb) equals upload #1 (staging copy, 6 bytes = 3 16-bit / - 32-bit indices, largest 16-bit value 2, first 16-bit values 0 1 2) read as uploaded, desc DEFAULT, Bind=IB; meshes with this index count: Body#")==1);
   assert(count(") mesh Body (3 indices, confirmed by submesh starts; its rules' hash d5ae71a3) and upload #1 (staging copy")==1&&count("ib-candidate TexMod: Body#")==1&&count("no hash equal")==1&&count("ib-capture 1 index uploads read (0 MiB): copy regions into index buffers 1")==1);
   assert(text(sharedTex+"/ib-hashes.txt").find(std::string("#1 6 bytes via staging copy buffer 0x100+0 max16 2 | 0=")) !=std::string::npos);
   T.installed(false,"");std::filesystem::remove_all(sharedTex+"/models/IbMod");}
  tm.shutdown();f->world(0);}
 // 42. A confirmed Mesh shown by another renderer (the party / details page): written as soon as it is seen, without the
 //     diagnostics reading it again; at the game's hand-off even before it is shown, except that "direct" leaves a
 //     loaded asset material alone there (writing it would reach every copy made from it).
 {std::string mediaQ=root+"/mediaQuick",sharedQ=mediaQ+"/"+pkg+"/GIBox",modQ=sharedQ+"/models/TexMod",logQ=sharedQ+"/GIBox Log.txt";std::filesystem::create_directories(modQ+"/textures");
  Ptr entity=load<Ptr>(load<Ptr>(load<Ptr>(f->manager+0x10)+0x10)+0x20);store<uint32_t>(entity+0x3a4,10000107);
  Ptr body=f->renderer(f->mesh("Body",3)),mat=load<Ptr>(f->obj[body].materials+0x20),gameTex=f->object("texture","Avatar_Tex_Body_Diffuse");f->diffuse[mat]=gameTex;f->world(body);f->scene={body};store<uint32_t>(load<Ptr>(mat+0x10)+8,uint32_t(-5));
  Bytes px(16,0x7f);write(modQ+"/textures/0.rgba",px.data(),px.size());
  write(modQ+"/texture.json",R"J({"format":"gibox-texture-mod","version":1,"name":"TexMod","generation":"q1","character":{"id":10000107},"variants":{"vars":[],"default":{}},
   "textures":[{"resource":"rbody","file":"textures/0.rgba","width":2,"height":2}],
   "diag":{"format":"gibox-texture-diag","version":1,"name":"TexMod","character":10000107,"rules":[{"section":"TextureOverrideModBodyA","part":"ModBodyA","hint":"body","hash":"d5ae71a3","first":0,"kind":"segment","textures":[
    {"slot":"ps-t0","resource":"rbody","role":"diffuse","condition":"","via":""}]}],"resources":[]}})J");
  write(sharedQ+"/control.json","{\"enabled\":true,\"selected\":[\"TexMod\"],\"textureMods\":{\"strategy\":\"direct\"}}");
  Runtime q(e,pkg,root+"/privateQuick",mediaQ,false);uint64_t t=7000000;auto run=[&](uint64_t ms){for(uint64_t end=t+ms;t<end;t+=100)q.tick(t);};
  auto count=[&](const std::string& x){auto l=text(logQ);size_t n=0;for(size_t p=l.find(x);p!=std::string::npos;p=l.find(x,p+1))n++;return n;};
  run(3000);assert(f->diffuse[mat]!=gameTex);Ptr ours=f->diffuse[mat];
  // The same Mesh object on another renderer in the scene: written by the per-tick scan, the diagnostics not involved.
  Ptr twin=f->renderer(f->obj[body].mesh),tmat=load<Ptr>(f->obj[twin].materials+0x20);f->diffuse[tmat]=gameTex;store<uint32_t>(load<Ptr>(tmat+0x10)+8,uint32_t(-6));f->scene={body,twin};run(700);
  if(std::getenv("TEXMOD_PRINT")){auto l=text(logQ);size_t p=0;while((p=l.find("texture-",p))!=std::string::npos){auto e2=l.find('\n',p);std::cerr<<l.substr(p,std::min<size_t>(e2-p,300))<<"\n";p=e2;}}
  assert(f->diffuse[tmat]==ours&&count("evidence=完整 Mesh 特征与 Body#")>=1);
  // The hand-off: a runtime instance material is written before the game shows the model; a loaded asset is left to the game.
  Ptr handed=f->renderer(f->obj[body].mesh),hmat=load<Ptr>(f->obj[handed].materials+0x20);f->diffuse[hmat]=gameTex;store<uint32_t>(load<Ptr>(hmat+0x10)+8,uint32_t(-7));
  Ptr go=f->gameObjectOf({handed});q.spawned(go);q.spawnedAfter();assert(f->diffuse[hmat]==ours&&count("seen by hand-off")==1&&count("texture-handoff TexMod: Avatar_Girl_Catalyst_Citlali_Manekin handed over: 1 renderers read and matched in")>=1);
  Ptr asset=f->renderer(f->obj[body].mesh),amat=load<Ptr>(f->obj[asset].materials+0x20);f->diffuse[amat]=gameTex;Ptr go2=f->gameObjectOf({asset});q.spawned(go2);q.spawnedAfter();
  assert(f->diffuse[amat]==gameTex&&count("still holds the loaded asset material; direct waits for the game's instance")==1);
  run(500);assert(f->diffuse[amat]==gameTex);   // the per-tick scan waits too
  Ptr ainst=clone(amat,0);store<uint32_t>(load<Ptr>(ainst+0x10)+8,uint32_t(-11));f->obj[asset].materials=f->typed("material",ainst);run(300);assert(f->diffuse[ainst]==ours&&f->diffuse[amat]==gameTex);
  // The game makes its instance from a material we wrote: the instance carries ours and is followed, the material off
  // the slot gets its texture back, and switching off restores the instance to the texture saved first.
  Ptr minst=clone(mat,0);store<uint32_t>(load<Ptr>(minst+0x10)+8,uint32_t(-13));f->obj[body].materials=f->typed("material",minst);run(300);
  assert(f->diffuse[minst]==ours&&f->diffuse[mat]==gameTex&&count("; it was made from the material we wrote ")==1&&count("the material off the slot ")==1);
  // Off: every one of them goes back.
  write(sharedQ+"/control.json","{\"enabled\":true,\"selected\":[],\"textureMods\":{\"strategy\":\"direct\"}}");run(1000);
  assert(f->diffuse[mat]==gameTex&&f->diffuse[tmat]==gameTex&&f->diffuse[hmat]==gameTex&&f->diffuse[ainst]==gameTex&&f->diffuse[minst]==gameTex);
  q.shutdown();f->world(0);f->scene={};}
 // 43. The first match at the game's hand-off: no earlier confirmation, the model not yet in the world; the textures were
 //     prepared once the mod was on, so the hand-off only checks and binds. The mapping is saved and, after a restart,
 //     used for this build and these rules only; a lower LOD without migration waits and says why; a material the game
 //     swaps at its mesh binding is written again right there.
 {std::string mediaH=root+"/mediaHand",sharedH=mediaH+"/"+pkg+"/GIBox",modH=sharedH+"/models/TexMod",logH=sharedH+"/GIBox Log.txt";std::filesystem::create_directories(modH+"/textures");
  Ptr entity=load<Ptr>(load<Ptr>(load<Ptr>(f->manager+0x10)+0x10)+0x20);store<uint32_t>(entity+0x3a4,10000107);
  Ptr seed=f->renderer(f->mesh("Seed",3));f->world(seed);f->scene={seed};   // learns the renderer type / GameObject class; not the mod's mesh
  texRanges[f->obj[seed].mesh]={{9,3}};
  Bytes px(16,0x7f);write(modH+"/textures/0.rgba",px.data(),px.size());
  write(modH+"/texture.json",R"J({"format":"gibox-texture-mod","version":1,"name":"TexMod","generation":"h1","character":{"id":10000107},"variants":{"vars":[],"default":{}},
   "textures":[{"resource":"rbody","file":"textures/0.rgba","width":2,"height":2}],
   "diag":{"format":"gibox-texture-diag","version":1,"name":"TexMod","character":10000107,"rules":[{"section":"TextureOverrideModBodyA","part":"ModBodyA","hint":"body","hash":"d5ae71a3","first":0,"kind":"segment","textures":[
    {"slot":"ps-t0","resource":"rbody","role":"diffuse","condition":"","via":""}]}],"resources":[]}})J");
  write(sharedH+"/control.json","{\"enabled\":true,\"selected\":[\"TexMod\"],\"textureMods\":{\"strategy\":\"clone\"}}");
  auto count=[&](const std::string& x){auto l=text(logH);size_t n=0;for(size_t p=l.find(x);p!=std::string::npos;p=l.find(x,p+1))n++;return n;};
  Ptr gameTex=f->object("texture","Avatar_Tex_Body_Diffuse");Ptr fullMesh=f->mesh("Body",3);
  {Runtime h(e,pkg,root+"/privateHand",mediaH,false);uint64_t t=8000000;auto run=[&](uint64_t ms){for(uint64_t end=t+ms;t<end;t+=100)h.tick(t);};
   run(1500);if(std::getenv("TEXMOD_PRINT")){auto l=text(logH);size_t p=0;while((p=l.find("texture-",p))!=std::string::npos){auto e2=l.find('\n',p);std::cerr<<l.substr(p,std::min<size_t>(e2-p,300))<<"\n";p=e2;}}
   assert(count("texture-ready TexMod: 1 Texture2D made on the game thread")==1&&count("INFO texture-apply")==0);
   Ptr body=f->renderer(fullMesh),mat=load<Ptr>(f->obj[body].materials+0x20);f->diffuse[mat]=gameTex;Ptr go=f->gameObjectOf({body});
   h.spawned(go);Ptr slot=load<Ptr>(f->obj[body].materials+0x20);
   assert(slot!=mat&&f->diffuse[slot]!=gameTex&&count("texture-handoff TexMod: Avatar_Girl_Catalyst_Citlali_Manekin handed over: 1 renderers read and matched in")==1&&count("1 slot(s) confirmed by index starts")==1);
   assert(count("texture-timing -/clone TexMod: Body#")==1&&count("seen by hand-off, matched +0 ms, textures ready ")==1&&count("texture-map TexMod: 1 confirmed mesh(es) saved to")==1&&exists(modH+"/texture-mapping.json"));
   // The game swaps the material at its own mesh binding: noticed there, not at the next poll.
   h.spawnedAfter();Ptr fresh=f->object("material","Material");f->diffuse[fresh]=gameTex;f->obj[body].materials=f->typed("material",fresh);h.meshBound(load<Ptr>(body+0x10));
   Ptr again=load<Ptr>(f->obj[body].materials+0x20);assert(again!=fresh&&f->diffuse[again]!=gameTex&&count("noticed by the game's mesh binding")==1);
   // A lower LOD handed over without migration: waits, with the reason.
   Ptr low=f->renderer(f->mesh("Body_LOD1",2)),lmat=load<Ptr>(f->obj[low].materials+0x20);f->diffuse[lmat]=gameTex;h.spawned(f->gameObjectOf({low}));
   assert(f->diffuse[lmat]==gameTex&&count("waits: lower LOD Body_LOD1: the setting writes only what the full mesh confirmed")==1);
   h.shutdown();}
  // A restart: the saved mapping is checked and used; a new hand-off is written before any diagnostics ran.
  {Runtime h(e,pkg,root+"/privateHand",mediaH,false);uint64_t t=9000000;auto run=[&](uint64_t ms){for(uint64_t end=t+ms;t<end;t+=100)h.tick(t);};run(1500);
   assert(count("texture-map TexMod: saved mapping in use: 1 mesh(es) for build 2b4dd731")==1);
   Ptr body=f->renderer(fullMesh),mat=load<Ptr>(f->obj[body].materials+0x20);f->diffuse[mat]=gameTex;h.spawned(f->gameObjectOf({body}));
   Ptr slot=load<Ptr>(f->obj[body].materials+0x20);assert(slot!=mat&&f->diffuse[slot]!=gameTex);h.shutdown();}
  // Another game build: the saved mapping is not used.
  {auto m=text(modH+"/texture-mapping.json");auto at=m.find(abi::buildId);assert(at!=std::string::npos);m.replace(at,8,"00000000");write(modH+"/texture-mapping.json",m);
   Runtime h(e,pkg,root+"/privateHand",mediaH,false);uint64_t t=10000000;for(uint64_t end=t+1500;t<end;t+=100)h.tick(t);
   assert(count("saved mapping not used: made for another game build")==1);h.shutdown();}
  if(std::getenv("TEXMOD_PRINT")){auto l=text(logH);size_t p=0;while((p=l.find("texture-",p))!=std::string::npos){auto e2=l.find('\n',p);auto x=l.substr(p,std::min<size_t>(e2-p,330));if(x.find("texture-diag")!=0)std::cerr<<"H| "<<x<<"\n";p=e2;}}
  f->world(0);f->scene={};}
 std::filesystem::remove_all(root);
 // 45. The index-buffer hash as confirmation (0.7.59): Bang starts at index 0 like Body, so submesh starts cannot tell
 //     its rule apart; its uploaded index data reproduces the rule's hash, which ties it to the one diagnosed mesh with
 //     that index and vertex count. The record carries the hash, is written and saved, and is used after a restart with
 //     no capture; an upload two meshes could be waits.
 {std::string mediaI=root+"/mediaIbc",sharedI=mediaI+"/"+pkg+"/GIBox",modI=sharedI+"/models/TexMod",logI=sharedI+"/GIBox Log.txt";std::filesystem::create_directories(modI+"/textures");
  Ptr entity=load<Ptr>(load<Ptr>(load<Ptr>(f->manager+0x10)+0x10)+0x20);store<uint32_t>(entity+0x3a4,10000107);
  Ptr gameTex=f->object("texture","Avatar_Tex_Hair_Diffuse");Ptr body=f->renderer(f->mesh("Body",3)),bmat=load<Ptr>(f->obj[body].materials+0x20);
  Ptr bangMesh=f->mesh("Bang",4);texRanges[bangMesh]={{0,6}};Ptr bang=f->renderer(bangMesh,"BoneA","Bang"),gmat=load<Ptr>(f->obj[bang].materials+0x20);
  for(Ptr m:{bmat,gmat}){f->diffuse[m]=gameTex;store<uint32_t>(load<Ptr>(m+0x10)+8,uint32_t(-21));}
  Ptr two[2]={body,bang};store<Ptr>(f->owner+0x68,f->array(8,2,two));f->scene={};
  uint16_t idx[6]={0,1,2,2,1,3};char hb[12];std::snprintf(hb,sizeof hb,"%08x",ms::vkcap::ibHashes(reinterpret_cast<const uint8_t*>(idx),12)[2].value);   // DEFAULT, IB|SRV|UAV
  Bytes px(16,0x7f);write(modI+"/textures/0.rgba",px.data(),px.size());
  write(modI+"/texture.json",std::string(R"J({"format":"gibox-texture-mod","version":1,"name":"TexMod","generation":"b1","character":{"id":10000107},"variants":{"vars":[],"default":{}},
   "textures":[{"resource":"rhair","file":"textures/0.rgba","width":2,"height":2}],
   "diag":{"format":"gibox-texture-diag","version":1,"name":"TexMod","character":10000107,"rules":[
    {"section":"TextureOverrideLinBody","part":"Body","hint":"body","hash":"d5ae71a3","first":0,"kind":"segment","textures":[{"slot":"ps-t0","resource":"rhair","role":"diffuse","condition":"","via":""}]},
    {"section":"TextureOverrideLinBangs","part":"Bangs","hint":"bangs","hash":")J")+hb+R"J(","first":0,"kind":"segment","textures":[{"slot":"ps-t0","resource":"rhair","role":"diffuse","condition":"","via":""}]}],"resources":[]}})J");
  write(sharedI+"/control.json","{\"enabled\":true,\"selected\":[\"TexMod\"],\"textureMods\":{\"strategy\":\"direct\"}}");
  auto count=[&](const std::string& x){auto l=text(logI);size_t n=0;for(size_t p=l.find(x);p!=std::string::npos;p=l.find(x,p+1))n++;return n;};
  auto& T=ms::vkcap::tracker();
  {Runtime q(e,pkg,root+"/privateIbc",mediaI,false);uint64_t t=11000000;auto run=[&](uint64_t ms){for(uint64_t end=t+ms;t<end;t+=100)q.tick(t);};
   run(3000);assert(f->diffuse[gmat]==gameTex&&f->diffuse[bmat]==gameTex&&count("INFO texture-apply")==0);   // both start at 0: nothing confirmed
   T.installed(true,"");uint8_t host[12];std::memcpy(host,idx,12);T.created(0x500,12,0x40);T.bound(0x500,0x600,0);T.mapped(0x600,0,12,host);T.indexBound(0x500,0);
   run(3000);
   if(std::getenv("TEXMOD_PRINT")){auto l=text(logI);size_t p=0;while((p=l.find("texture-",p))!=std::string::npos){auto e2=l.find('\n',p);auto x=l.substr(p,std::min<size_t>(e2-p,400));if(x.find("texture-diag")!=0)std::cerr<<"I| "<<x<<"\n";p=e2;}}
   assert(count(std::string("texture-hash-confirm TexMod: ")+hb+" (upload #")==1&&count("= Bang#")==1&&count("TextureOverrideLinBangs -> submesh 0 (starts at 0) = material slot 0 Material")==1);
   assert(f->diffuse[gmat]!=gameTex&&f->diffuse[bmat]==gameTex&&count(std::string("evidence=IB hash ")+hb+" 与 PC 相同")==1);
   assert(text(modI+"/texture-mapping.json").find(std::string("\"hash\":\"")+hb+"\"")!=std::string::npos);
   T.installed(false,"");q.shutdown();assert(f->diffuse[gmat]==gameTex);}
  // A restart, no capture: the saved record (with its hash) is used.
  {Runtime q(e,pkg,root+"/privateIbc",mediaI,false);uint64_t t=12000000;for(uint64_t end=t+3000;t<end;t+=100)q.tick(t);
   assert(count("saved mapping in use: 1 mesh(es)")==1&&f->diffuse[gmat]!=gameTex&&f->diffuse[bmat]==gameTex);q.shutdown();}
  // Two meshes with the upload's index and vertex counts: not tied, said why.
  std::filesystem::remove(modI+"/texture-mapping.json");Ptr twinMesh=f->mesh("Bang2",4);texRanges[twinMesh]={{0,6}};Ptr bang2=f->renderer(twinMesh,"BoneA","Hair");
  Ptr three[3]={body,bang,bang2};store<Ptr>(f->owner+0x68,f->array(8,3,three));
  {Runtime q(e,pkg,root+"/privateIbc2",mediaI,false);uint64_t t=13000000;auto run=[&](uint64_t ms){for(uint64_t end=t+ms;t<end;t+=100)q.tick(t);};
   T.installed(true,"");uint8_t host[12];std::memcpy(host,idx,12);T.created(0x700,12,0x40);T.bound(0x700,0x800,0);T.mapped(0x800,0,12,host);T.indexBound(0x700,0);run(3000);
   assert(count("meshes Bang, Bang2 all have its index and vertex counts; not tied")==1&&f->diffuse[gmat]==gameTex);T.installed(false,"");q.shutdown();}
  f->world(0);}
 std::cout<<"Native runtime: PASS (texture-only diagnostics read-only and cached; logic core: outfits by what the logic selects, App value as input, off, not finite and missing variable refused; App key presses as core key events (cycle, toggle, back, taken once, not replayed at start, refused when not a cycle/toggle key or the core is off), persist saved atomically per model and restored over the App table, not for another INI; driven outfit models (the program picks by clock and keys, the App table not pushed); frames by what the draws select, as FrameLoop, a selection no frame has keeps the frame, no match at the start keeps FrameLoop, switch off; face texture swaps: written, copies tracked, fresh materials, restored, assets copied; direct textures: written, put back, game copies tracked, assets and shared materials copied; merged draws: hidden and non-contiguous ranges, variant switches, separateDraws rebuild; slot textures by property block and custom texture, glow trace windows and read-back, structural capture exit after 30 s preserved, replacement-off restores originals and says so, missing renderer logged once, preload and per-variant textures, glow experiments, appearance timeline, hidden party members by library identity, model.bin checked once per file, extra material slots borrow a shader, paired restoration after game material copies, clean palette backups, inherited refresh schedule, temporary material readiness, clones retaken after switch-in, lower-LOD world mesh with the same bones (attach, and build checked against the bone library), indexed skeleton bounds and retry, idle sets released after a minute, version 2 conditional draws switched in place, unverified lower LODs never recorded or built against, bone export only on request, wait reasons in status and log, GIBox model folders and learned.json, skeleton.json library (quiet while loading, reasons once), first-switched-on precedence for two mods of one character, manual import by ID + model name, re-import for another character, XXMI log lines and levels,fingerprint matching, detail-page/asset-reload followers, NPC/skeleton guards, multi-model, idle cache, hot reload, broken-edit fallback, variants, name-matched models, async hand-off attach/build, game mesh put back (hand-off, LOD), game rebinding answered with ours, no magenta after drops, adoption, clone hand-over, update retargeting, bone library, Type from this_arg pairs or array classes, scan-off fallback, failure latch, rollback, GC release, threaded worker)\n";
}
