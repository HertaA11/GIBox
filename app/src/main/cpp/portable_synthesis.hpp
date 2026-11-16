#pragma once
#include "runtime.hpp"
#include <array>

namespace ms {
namespace synthesisAbi {
struct V2 {float x,y;};
struct Rect {float x,y,w,h;};
struct Color {float r,g,b,a;};
}

// All instance methods run on the game thread. The cloned button has its own
// UnityEvent; the event hook only consumes that instance.
class PortableSynthesis {
 using V2=synthesisAbi::V2;
 using Rect=synthesisAbi::Rect;
 using Color=synthesisAbi::Color;
 struct Roots {
  Engine& e;
  std::array<uint32_t,4> handles{};
  size_t count=0;
  explicit Roots(Engine& engine):e(engine){}
  ~Roots(){while(count)e.call<void>(abi::unpin,handles[--count]);}
  Ptr hold(Ptr p){
   require(p&&count<handles.size(),"合成台界面对象未创建");
   auto handle=e.call<uint32_t>(abi::pin,p,2,Ptr(0));
   require(handle,"合成台 GC 引用失败");handles[count++]=handle;return p;
  }
  Ptr text(const char* value){return hold(e.call<Ptr>(abi::newString,Ptr(0),value));}
 };

 Engine& e;
 Texture icon;
 std::function<void(const std::string&,const std::string&)> log;
 bool enabled=false,pending=false;
 Ptr root=0,clickEvent=0,texture=0,sprite=0;
 uint32_t rootHandle=0,eventHandle=0,textureHandle=0,spriteHandle=0;
 uint64_t lastOpen=0,nextCreate=0;
 std::string lastError;

 void say(const std::string& text){if(log)log("synthesis",text);}
 static bool live(Ptr p){return p&&load<Ptr>(p+0x10);}
 Ptr checked(Ptr p,const char* name){require(live(p),name);return p;}
 uint32_t retain(Ptr p){auto h=e.call<uint32_t>(abi::pin,p,2,Ptr(0));require(h,"合成台 GC 引用失败");return h;}
 Ptr child(Ptr parent,int index){
  return checked(e.call<Ptr>(abi::uiChild,parent,index,Ptr(0)),"合成台按钮层级无效");
 }
 void parent(Ptr node,Ptr target){e.call<void>(abi::uiSetParent,node,target,false,Ptr(0));}
 Ptr component(Ptr object,Ptr name){return checked(e.call<Ptr>(abi::componentByName,object,name,Ptr(0)),"合成台按钮组件不可用");}
 void hideChildren(Ptr node,int start,Ptr keep=0){
  auto count=e.call<int>(abi::uiChildCount,node,Ptr(0));
  for(int i=start;i<count;++i){
   auto tr=child(node,i);if(tr==keep)continue;
   if(auto go=e.get(abi::gameObject,tr);live(go))e.op(abi::goSetActive,go,0);
  }
 }
 void remove(){
  pending=false;clickEvent=0;
  if(live(root)){e.op(abi::goSetActive,root,0);e.action(abi::destroy,root);}
  root=0;
  if(live(sprite))e.action(abi::destroy,sprite);
  if(live(texture))e.action(abi::destroy,texture);
  sprite=texture=0;
  if(spriteHandle)e.call<void>(abi::unpin,std::exchange(spriteHandle,0));
  if(textureHandle)e.call<void>(abi::unpin,std::exchange(textureHandle,0));
  if(eventHandle)e.call<void>(abi::unpin,std::exchange(eventHandle,0));
  if(rootHandle)e.call<void>(abi::unpin,std::exchange(rootHandle,0));
 }

 void createSprite(){
  auto type=e.call<Ptr>(abi::textureType);
  require(type,"合成台图标缺少纹理类型");
  texture=e.call<Ptr>(abi::objectNew,load<Ptr>(type));
  require(texture,"合成台图标纹理分配失败");
  textureHandle=retain(texture);
  e.call<void>(abi::textureInit,texture,int(icon.w),int(icon.h),4,0,0,Ptr(0));
  checked(texture,"合成台图标纹理初始化失败");
  e.call<void>(abi::texturePixels,texture,reinterpret_cast<Ptr>(icon.pixels.data()),int(icon.pixels.size()),Ptr(0));
  e.op(abi::textureWrap,texture,1);
  e.op(abi::textureFilter,texture,1);
  e.action(abi::textureApply,texture);
  sprite=checked(e.call<Ptr>(abi::spriteCreate,texture,Rect{0,0,float(icon.w),float(icon.h)},V2{.5f,.5f},Ptr(0)),"合成台图标 Sprite 创建失败");
  spriteHandle=retain(sprite);
 }

 void create(Ptr topTransform,Ptr source){
  auto sourceTransform=checked(e.get(abi::uiComponentTransform,source),"合成台按钮模板不可用");
  auto wrapper=checked(e.get(abi::getParent,sourceTransform),"合成台按钮模板没有容器");
  auto sourceRoot=checked(e.get(abi::gameObject,wrapper),"合成台按钮模板容器不可用");
  const auto originalEvent=load<Ptr>(source+abi::buttonClickEvent);
  require(originalEvent,"合成台按钮模板没有点击事件");
  const auto eventClass=load<Ptr>(originalEvent);
  require(eventClass,"合成台点击事件类型不可用");

  Roots temp(e);
  root=checked(e.get(abi::clone,sourceRoot),"合成台按钮复制失败");
  rootHandle=retain(root);
  e.op(abi::goSetActive,root,0);
  auto rootTransform=checked(e.get(abi::uiGoTransform,root),"合成台按钮没有布局组件");
  parent(rootTransform,topTransform);
  e.set(abi::uiSetName,root,temp.text("GIBoxSynthesis"));
  e.op(abi::uiSetSibling,rootTransform,0);

  require(e.call<int>(abi::uiChildCount,rootTransform,Ptr(0))>0,"合成台按钮模板没有子节点");
  auto buttonTransform=child(rootTransform,0);
  auto buttonObject=checked(e.get(abi::gameObject,buttonTransform),"合成台按钮节点不可用");
  auto button=component(buttonObject,temp.text("Button"));
  auto event=e.call<Ptr>(abi::objectNew,eventClass);
  require(event,"合成台点击事件创建失败");
  eventHandle=retain(event);
  e.action(abi::uiNewClick,event);
  e.set(abi::uiSetClick,button,event);

  // Reuse the native icon RectTransform; the surrounding layout controls its size.
  auto iconTransform=checked(e.call<Ptr>(abi::uiFindChild,buttonTransform,temp.text("Icon"),Ptr(0)),"合成台按钮模板没有图标");
  auto iconObject=checked(e.get(abi::gameObject,iconTransform),"合成台图标节点不可用");
  auto image=component(iconObject,temp.text("Image"));
  createSprite();
  e.op(abi::imageSetType,image,0); // Simple
  e.op(abi::imageSetAspect,image,1);
  e.call<void>(abi::graphicSetColor,image,Color{1,1,1,1},Ptr(0));
  e.op(abi::uiSetTransition,button,1); // ColorTint keeps the icon through pressed/selected states.
  e.set(abi::uiTargetGraphic,button,image);
  e.set(abi::imageSetOverride,image,sprite);
  hideChildren(buttonTransform,0,iconTransform);
  hideChildren(rootTransform,1);
  clickEvent=event;
  e.op(abi::goSetActive,root,1);
 }
 void open(uint64_t now){
  if(lastOpen&&(now<lastOpen||now-lastOpen<1000))return;
  lastOpen=now;
  try{
   Roots args(e);
   auto page=args.text("SynthesisPage"),argument=args.text("");
   auto accepted=e.call<uint32_t>(abi::synthesisOpen,page,argument,Ptr(0),Ptr(0),Ptr(0),Ptr(0))&1;
   say(accepted?"已请求打开合成台":"游戏没有接受合成台打开请求");
  }catch(const std::exception& ex){say(std::string("合成台打开失败：")+ex.what());}
 }

 public:
 explicit PortableSynthesis(Engine& engine,Texture image,std::function<void(const std::string&,const std::string&)> reporter={})
  :e(engine),icon(std::move(image)),log(std::move(reporter)){}
 void setEnabled(bool on){
  if(enabled==on)return;
  enabled=on;nextCreate=0;lastError.clear();
  if(!on)remove();
 }
 void collect(){if(root&&!live(root)){remove();nextCreate=0;}}
 bool click(Ptr event) noexcept {
  if(!enabled||!clickEvent||event!=clickEvent)return false;
  pending=true;return true;
 }
 void frame(Ptr controller,uint64_t now){
  if(!enabled||!controller||icon.pixels.empty())return;
  collect();
  auto hud=load<Ptr>(controller+abi::synthesisHud);
  auto top=live(hud)?load<Ptr>(hud+abi::synthesisTop):0;
  if(!live(top)){if(root)remove();return;}
  auto topTransform=e.get(abi::uiGoTransform,top);
  if(root){
   auto rootTransform=e.get(abi::uiGoTransform,root);
   if(!live(topTransform)||!live(rootTransform)||e.get(abi::getParent,rootTransform)!=topTransform){remove();nextCreate=0;}
  }
  if(!live(topTransform)||!e.call<bool>(abi::goActive,top,Ptr(0))){pending=false;return;}
  if(root){if(std::exchange(pending,false))open(now);return;}
  if(now<nextCreate)return;

  auto source=load<Ptr>(hud+abi::synthesisTemplate);
  if(!live(source))return;
  nextCreate=now+1000;
  try{create(topTransform,source);lastError.clear();}
  catch(const std::exception& ex){
   remove();
   if(lastError!=ex.what()){lastError=ex.what();say("无法显示合成按钮："+lastError);}
  }
 }
};
}
