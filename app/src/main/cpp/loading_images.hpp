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
 void restore(){if(applied&&live()){if(load<Ptr>(image+0xe0)==sprite)e.set(abi::imageSetOverride,image,original);e.op(abi::imageSetType,image,originalType);e.op(abi::imageSetAspect,image,originalAspect?1:0);Color restored=originalColor;restored.a=e.call<Color>(abi::graphicColor,image,Ptr(0)).a;e.call<void>(abi::graphicSetColor,image,restored,Ptr(0));}
  for(const auto& h:hidden){if(alive(h.go)&&load<Ptr>(h.go+0x10)==h.native){e.op(abi::goSetActive,h.go,h.active?1:0);}}
  hidden.clear();release(hiddenRefs);applied=false;appliedKey.clear();cleanAssets();}
 void hideUi(){Ptr root=e.get(abi::gameObject,owner);for(unsigned offset:{0x18u,0x20u,0x38u,0x40u}){Ptr component=load<Ptr>(owner+offset);if(!alive(component))continue;Ptr go=e.get(abi::gameObject,component);if(!alive(go)||go==root)continue;
   bool duplicate=false;for(const auto& h:hidden){duplicate=duplicate||h.go==go;}
   if(duplicate){continue;}pin(hiddenRefs,go);bool active=(e.call<uint32_t>(abi::goActiveSelf,go,Ptr(0))&1)!=0;hidden.push_back({go,load<Ptr>(go+0x10),active});if(active)e.op(abi::goSetActive,go,0);}}
 void capture(){original=load<Ptr>(image+0xe0);release(originalRefs);pin(originalRefs,original);originalType=e.call<int>(abi::imageType,image,Ptr(0));originalAspect=(e.call<uint32_t>(abi::imageAspect,image,Ptr(0))&1)!=0;originalColor=e.call<Color>(abi::graphicColor,image,Ptr(0));}
 void apply(const std::string& fit){if(!live()||!prepared||prepared->name.empty()||!prepared->error.empty())return;const auto& t=prepared->image;Rect crop{0,0,float(t.w),float(t.h)};std::string key=prepared->key+":"+fit;
  if(fit=="cover"){Ptr transform=e.get(abi::graphicRect,image);require(alive(transform),"加载背景尺寸尚未就绪");auto bounds=e.call<Rect>(abi::rectBounds,transform,Ptr(0));require(std::isfinite(bounds.w)&&std::isfinite(bounds.h)&&bounds.w>0&&bounds.h>0,"加载背景尺寸无效");
   float ratio=bounds.w/bounds.h;if(float(t.w)/t.h>ratio){crop.w=t.h*ratio;crop.x=(t.w-crop.w)*.5f;}else{crop.h=t.w/ratio;crop.y=(t.h-crop.h)*.5f;}key+=':'+std::to_string(crop.w)+':'+std::to_string(crop.h);}
  if(appliedKey==key&&alive(sprite)&&assets.size()==2&&alive(assets[0])){
   Ptr current=load<Ptr>(image+0xe0);if(current!=sprite){release(originalRefs);original=current;pin(originalRefs,original);e.set(abi::imageSetOverride,image,sprite);}
   if(e.call<int>(abi::imageType,image,Ptr(0))!=0){originalType=e.call<int>(abi::imageType,image,Ptr(0));e.op(abi::imageSetType,image,0);}
   e.op(abi::imageSetAspect,image,fit=="original"?1:0);Color color=e.call<Color>(abi::graphicColor,image,Ptr(0));if(color.r!=1||color.g!=1||color.b!=1){originalColor=color;e.call<void>(abi::graphicSetColor,image,Color{1,1,1,color.a},Ptr(0));}return;
  }restore();capture();
  Ptr descriptor=e.call<Ptr>(abi::textureType);require(descriptor!=0,"缺少加载图片纹理类型");Ptr texture=e.call<Ptr>(abi::objectNew,load<Ptr>(descriptor));require(texture!=0,"加载图片分配失败");assets.push_back(texture);pin(assetRefs,texture);
  e.call<void>(abi::textureInit,texture,int(t.w),int(t.h),4,0,0,Ptr(0));require(alive(texture),"加载图片初始化失败");e.call<void>(abi::texturePixels,texture,reinterpret_cast<Ptr>(t.pixels.data()),int(t.pixels.size()),Ptr(0));e.op(abi::textureWrap,texture,1);e.op(abi::textureFilter,texture,1);e.action(abi::textureApply,texture);
  sprite=e.call<Ptr>(abi::spriteCreate,texture,crop,Pivot{.5f,.5f},Ptr(0));require(alive(sprite),"加载图片 Sprite 创建失败");assets.push_back(sprite);pin(assetRefs,sprite);
  applied=true;e.op(abi::imageSetType,image,0);e.op(abi::imageSetAspect,image,fit=="original"?1:0);e.call<void>(abi::graphicSetColor,image,Color{1,1,1,originalColor.a},Ptr(0));e.set(abi::imageSetOverride,image,sprite);if(prepared->hide)hideUi();appliedKey=key;
 }
 public:
 std::atomic<unsigned> cycle{0};explicit LoadingScreens(Engine& engine):e(engine){}
 bool updating()const{return busy;}
 bool showing()const{return live()&&load<Ptr>(owner+0x28)==image&&(e.call<uint32_t>(abi::imageActive,image,Ptr(0))&1)!=0;}
 bool discover(Ptr type){if(busy||!type||showing()){return false;}std::vector<uint32_t> roots;bool changed=false;Ptr pendingCanvas=0,pendingBg=0;unsigned pendingCount=0;
  try{Ptr found=e.call<Ptr>(abi::findObjectsOfType,type,Ptr(0));if(found){pin(roots,found);uint64_t n=load<uint64_t>(found+0x18);require(n<=64,"加载界面实例数无效");
   for(uint64_t i=0;i<n;i++){Ptr canvas=load<Ptr>(found+0x20+i*8);if(!alive(canvas)){continue;}Ptr go=e.get(abi::gameObject,canvas);if(!alive(go)||!(e.call<uint32_t>(abi::goActive,go,Ptr(0))&1)){continue;}Ptr bg=load<Ptr>(canvas+0x28);if(!alive(bg)){continue;}if(!(e.call<uint32_t>(abi::imageActive,bg,Ptr(0))&1)){pendingCanvas=canvas;pendingBg=bg;pendingCount++;continue;}
    pendingCount=0;changed=owner!=canvas||image!=bg||!live();observe(canvas,bg);break;}}if(!showing()&&pendingCount==1){changed=owner!=pendingCanvas||image!=pendingBg||!live();observe(pendingCanvas,pendingBg);}}
  catch(...){release(roots);throw;}release(roots);return changed;}
 bool hasCanvas()const{return owner!=0;}
 bool ready()const{return prepared&&!prepared->name.empty();}bool active()const{return applied&&visible;}std::string name()const{return prepared?prepared->name:"";}std::string error()const{return prepared?prepared->error:"";}
 void receive(std::shared_ptr<LoadingPrepared> p){prepared=std::move(p);}
 bool matches(Ptr p)const{return p&&p==image;}
 void observe(Ptr canvas,Ptr background){if(busy||!alive(canvas)||!alive(background))return;if(owner==canvas&&image==background&&live())return;
  bool previous=visible;shutdown();if(previous){cycle.fetch_add(1);}owner=canvas;ownerNative=load<Ptr>(canvas+0x10);image=background;imageNative=load<Ptr>(background+0x10);pin(refs,owner);pin(refs,image);
  capture();}
 void event(Ptr background,bool shown){if(busy||!matches(background))return;if(!shown&&visible){busy=true;try{restore();visible=false;cycle.fetch_add(1);}catch(...){busy=false;throw;}busy=false;}}
 void tick(bool enabled,const std::vector<std::string>& selected,const std::string& fit="original"){if(busy)return;busy=true;try{bool selectedNow=false;if(prepared){for(const auto& n:selected){selectedNow=selectedNow||n==prepared->name;}}
   if(!live()||load<Ptr>(owner+0x28)!=image){restore();if(visible){cycle.fetch_add(1);}visible=false;busy=false;return;}bool shown=(e.call<uint32_t>(abi::imageActive,image,Ptr(0))&1)!=0;
   if(!shown&&visible){restore();visible=false;cycle.fetch_add(1);}else if(shown){visible=true;if(enabled&&selectedNow)apply(fit);else restore();}
  }catch(...){try{restore();}catch(...){}busy=false;throw;}busy=false;}
 void destroyed(Ptr canvas){if(canvas==owner){if(visible)cycle.fetch_add(1);shutdown();}}
 void shutdown(){busy=true;try{restore();release(refs);release(originalRefs);owner=0;image=0;visible=false;}catch(...){busy=false;throw;}busy=false;}
};
}
