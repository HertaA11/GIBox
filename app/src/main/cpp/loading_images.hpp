#pragma once
#include "runtime.hpp"
#include "model.hpp"
#include <atomic>
#include <chrono>
#include <random>
namespace ms {
struct LoadingPrepared {std::string name,key,error;unsigned cycle=0,index=0;bool hide=false;Texture image;};
// Worker-only reader: one decoded image is resident, not a whole slideshow.
struct LoadingReader {
 std::string key,selectionKey;unsigned selectedImage=0;std::mt19937 random{unsigned(std::chrono::steady_clock::now().time_since_epoch().count())};
 std::shared_ptr<LoadingPrepared> update(const std::string& root,const std::vector<std::string>& selected,bool enabled,const std::map<std::string,std::map<std::string,int>>& vars,unsigned cycle){
  std::string folder,name;if(enabled)for(const auto& n:selected){if(n.empty()||n==".."||n.find_first_of("/\\")!=n.npos)continue;if(exists(root+"/"+n+"/loading.json")){name=n;folder=root+"/"+n;break;}}
  std::string next=name.empty()?"none":folder+":"+std::to_string(static_cast<long long>(std::filesystem::last_write_time(folder+"/loading.json").time_since_epoch().count()))+":"+std::to_string(cycle);
  std::string selection=next;auto v=vars.find(name);int hide=-1;if(v!=vars.end()){auto it=v->second.find("$skip_ui");if(it!=v->second.end())hide=it->second;}next+=':'+std::to_string(hide);
  if(next==key){return nullptr;}key=next;auto out=std::make_shared<LoadingPrepared>();out->key=key;out->name=name;out->cycle=cycle;if(name.empty())return out;
  try{auto j=JsonParser(text(folder+"/loading.json")).parse();require(jstr(j,"format",64)=="gibox-loading-images"&&juint(j,"version",1,1)==1,"加载图片格式无效");
   const auto& images=jarr(j,"images",1,64);for(const auto& row:images){auto f=jstr(row,"file",64);require(f.compare(0,7,"images/")==0&&f.size()>12&&f.compare(f.size()-5,5,".rgba")==0&&f.find_first_not_of("0123456789",7)==f.size()-5,"加载图片路径无效");juint(row,"width",1,2048);juint(row,"height",1,2048);}
   if(selection!=selectionKey||selectedImage>=images.size()){selectedImage=std::uniform_int_distribution<unsigned>(0,unsigned(images.size()-1))(random);selectionKey=selection;}out->index=selectedImage;const auto& image=images[out->index];auto file=jstr(image,"file",64);
   require(file.compare(0,7,"images/")==0&&file.size()>12&&file.compare(file.size()-5,5,".rgba")==0&&file.find_first_not_of("0123456789",7)==file.size()-5,"加载图片路径无效");
   out->image.w=unsigned(juint(image,"width",1,2048));out->image.h=unsigned(juint(image,"height",1,2048));out->image.pixels=read(folder+"/"+file,16u<<20);require(out->image.pixels.size()==4ull*out->image.w*out->image.h,"加载图片数据长度不符");
   out->hide=hide>=0?hide!=0:j.at("hideUi").type==Json::Bool&&j.at("hideUi").boolean;
  }catch(const std::exception& ex){out->error=ex.what();}return out;}
};
class LoadingScreens {
 struct Color {float r,g,b,a;};struct Rect {float x,y,w,h;};struct Pivot {float x,y;};
 struct Hidden {Ptr go,native;bool active;};Engine& e;std::vector<uint32_t> refs,assetRefs,hiddenRefs,originalRefs;std::vector<Ptr> assets;std::vector<Hidden> hidden;
 Ptr owner=0,ownerNative=0,image=0,imageNative=0,original=0,sprite=0;int originalType=0;bool originalAspect=false,visible=false,applied=false,busy=false;Color originalColor{1,1,1,1};std::string appliedKey;
 std::shared_ptr<LoadingPrepared> prepared;
 static bool alive(Ptr p){return p&&load<Ptr>(p+0x10)!=0;}
 void pin(std::vector<uint32_t>& roots,Ptr p){if(p){auto h=e.call<uint32_t>(abi::pin,p,2,Ptr(0));require(h!=0,"加载界面 GC 引用失败");roots.push_back(h);}}
 void release(std::vector<uint32_t>& roots){for(auto h:roots){e.call<void>(abi::unpin,h);}
  roots.clear();}
 bool live()const{return alive(owner)&&load<Ptr>(owner+0x10)==ownerNative&&alive(image)&&load<Ptr>(image+0x10)==imageNative;}
 void cleanAssets(){for(auto it=assets.rbegin();it!=assets.rend();++it){if(alive(*it)){e.action(abi::destroy,*it);}}
  assets.clear();release(assetRefs);sprite=0;}
 void restore(){if(applied&&live()){if(e.get(abi::imageSprite,image)==sprite)e.set(abi::imageSetSprite,image,original);e.op(abi::imageSetType,image,originalType);e.op(abi::imageSetAspect,image,originalAspect?1:0);e.call<void>(abi::graphicSetColor,image,originalColor,Ptr(0));}
  for(const auto& h:hidden){if(alive(h.go)&&load<Ptr>(h.go+0x10)==h.native){e.op(abi::goSetActive,h.go,h.active?1:0);}}
  hidden.clear();release(hiddenRefs);applied=false;appliedKey.clear();cleanAssets();}
 void hideUi(){Ptr root=e.get(abi::gameObject,owner);for(unsigned offset:{0x18u,0x20u,0x38u,0x40u}){Ptr component=load<Ptr>(owner+offset);if(!alive(component))continue;Ptr go=e.get(abi::gameObject,component);if(!alive(go)||go==root)continue;
   bool duplicate=false;for(const auto& h:hidden){duplicate=duplicate||h.go==go;}
   if(duplicate){continue;}pin(hiddenRefs,go);bool active=(e.call<uint32_t>(abi::goActiveSelf,go,Ptr(0))&1)!=0;hidden.push_back({go,load<Ptr>(go+0x10),active});if(active)e.op(abi::goSetActive,go,0);}}
 void capture(){original=e.get(abi::imageSprite,image);release(originalRefs);pin(originalRefs,original);originalType=e.call<int>(abi::imageType,image,Ptr(0));originalAspect=(e.call<uint32_t>(abi::imageAspect,image,Ptr(0))&1)!=0;originalColor=e.call<Color>(abi::graphicColor,image,Ptr(0));}
 void apply(){if(!live()||!prepared||prepared->name.empty()||!prepared->error.empty()||prepared->cycle!=cycle.load())return;if(appliedKey==prepared->key)return;restore();capture();
  const auto& t=prepared->image;Ptr descriptor=e.call<Ptr>(abi::textureType);require(descriptor!=0,"缺少加载图片纹理类型");Ptr texture=e.call<Ptr>(abi::objectNew,load<Ptr>(descriptor));require(texture!=0,"加载图片分配失败");assets.push_back(texture);pin(assetRefs,texture);
  e.call<void>(abi::textureInit,texture,int(t.w),int(t.h),4,0,0,Ptr(0));require(alive(texture),"加载图片初始化失败");e.call<void>(abi::texturePixels,texture,reinterpret_cast<Ptr>(t.pixels.data()),int(t.pixels.size()),Ptr(0));e.action(abi::textureApply,texture);
  sprite=e.call<Ptr>(abi::spriteCreate,texture,Rect{0,0,float(t.w),float(t.h)},Pivot{.5f,.5f},Ptr(0));require(alive(sprite),"加载图片 Sprite 创建失败");assets.push_back(sprite);pin(assetRefs,sprite);
  applied=true;e.op(abi::imageSetType,image,0);e.op(abi::imageSetAspect,image,1);e.call<void>(abi::graphicSetColor,image,Color{1,1,1,originalColor.a},Ptr(0));e.set(abi::imageSetSprite,image,sprite);if(prepared->hide)hideUi();appliedKey=prepared->key;
 }
 public:
 std::atomic<unsigned> cycle{0};explicit LoadingScreens(Engine& engine):e(engine){}
 bool hasCanvas()const{return owner!=0;}
 bool ready()const{return prepared&&!prepared->name.empty();}bool active()const{return applied&&visible;}std::string name()const{return prepared?prepared->name:"";}std::string error()const{return prepared?prepared->error:"";}
 void receive(std::shared_ptr<LoadingPrepared> p){prepared=std::move(p);}
 bool matches(Ptr p)const{return p&&p==image;}
 void observe(Ptr canvas,Ptr background){if(busy||!alive(canvas)||!alive(background))return;if(owner==canvas&&image==background&&live())return;
  shutdown();owner=canvas;ownerNative=load<Ptr>(canvas+0x10);image=background;imageNative=load<Ptr>(background+0x10);pin(refs,owner);pin(refs,image);
  capture();}
 void event(Ptr background,bool shown){if(busy||!matches(background))return;if(!shown&&visible){busy=true;restore();visible=false;cycle.fetch_add(1);busy=false;}else if(shown)visible=true;}
 void tick(bool enabled,const std::vector<std::string>& selected){if(busy)return;busy=true;try{bool selectedNow=false;if(prepared){for(const auto& n:selected){selectedNow=selectedNow||n==prepared->name;}}
   if(!live()){restore();busy=false;return;}bool shown=(e.call<uint32_t>(abi::imageActive,image,Ptr(0))&1)!=0;
   if(!shown&&visible){restore();visible=false;cycle.fetch_add(1);}else if(shown){visible=true;if(enabled&&selectedNow)apply();else restore();}
  }catch(...){try{restore();}catch(...){}busy=false;throw;}busy=false;}
 void destroyed(Ptr canvas){if(canvas==owner){if(visible)cycle.fetch_add(1);shutdown();}}
 void shutdown(){restore();release(refs);release(originalRefs);owner=0;image=0;visible=false;}
};
}
