#pragma once
#include "runtime.hpp"
#include <array>

namespace ms {
namespace synthesisAbi {
struct V2 {float x,y;};
struct V3 {float x,y,z;};
struct Color {float r,g,b,a;};
}

// All instance methods run on the game thread. The cloned button has its own
// UnityEvent; the event hook only consumes that instance.
class PortableSynthesis {
 using V2=synthesisAbi::V2;
 using V3=synthesisAbi::V3;
 using Color=synthesisAbi::Color;
 struct Roots {
  Engine& e;
  std::array<uint32_t,5> handles{};
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
 std::function<void(const std::string&,const std::string&)> log;
 bool enabled=false,pending=false;
 Ptr root=0,clickEvent=0;
 uint32_t rootHandle=0,eventHandle=0;
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
  if(eventHandle)e.call<void>(abi::unpin,std::exchange(eventHandle,0));
  if(rootHandle)e.call<void>(abi::unpin,std::exchange(rootHandle,0));
 }

 void create(Ptr topTransform,Ptr source,Ptr font){
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

  // DefaultControls.Resources is seven sprite references; the AArch64 ABI passes it indirectly.
  std::array<Ptr,7> resources{};
  auto label=checked(e.call<Ptr>(abi::uiCreateText,resources.data(),Ptr(0)),"合成台文字创建失败");
  Ptr labelTransform;
  try{
   temp.hold(label);
   labelTransform=checked(e.get(abi::uiGoTransform,label),"合成台文字没有布局组件");
   parent(labelTransform,buttonTransform);
  }catch(...){e.action(abi::destroy,label);throw;}
  e.call<void>(abi::uiAnchorMin,labelTransform,V2{0,0},Ptr(0));
  e.call<void>(abi::uiAnchorMax,labelTransform,V2{1,1},Ptr(0));
  e.call<void>(abi::uiSizeDelta,labelTransform,V2{0,0},Ptr(0));
  e.call<void>(abi::uiAnchoredPosition,labelTransform,V2{0,0},Ptr(0));
  e.call<void>(abi::uiLocalScale,labelTransform,V3{1,1,1},Ptr(0));

  auto text=component(label,temp.text("Text"));
  e.set(abi::uiSetFont,text,font);
  e.set(abi::uiTextSet,text,temp.text("合成"));
  e.op(abi::uiFontSize,text,22);
  e.op(abi::uiAlignment,text,4); // MiddleCenter
  e.op(abi::uiOverflowX,text,1);
  e.op(abi::uiOverflowY,text,1);
  e.call<void>(abi::graphicSetColor,text,Color{1,1,1,1},Ptr(0));
  e.set(abi::uiTargetGraphic,button,text);
  hideChildren(buttonTransform,0,labelTransform);
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
 explicit PortableSynthesis(Engine& engine,std::function<void(const std::string&,const std::string&)> reporter={})
  :e(engine),log(std::move(reporter)){}
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
  if(!enabled||!controller)return;
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
  auto fontText=load<Ptr>(hud+abi::synthesisFont);
  if(!live(fontText))fontText=load<Ptr>(hud+abi::synthesisFontFallback);
  if(!live(source)||!live(fontText))return;
  auto font=e.get(abi::uiSimpleFont,fontText);
  if(!live(font))return;
  nextCreate=now+1000;
  try{create(topTransform,source,font);lastError.clear();}
  catch(const std::exception& ex){
   remove();
   if(lastError!=ex.what()){lastError=ex.what();say("无法显示合成按钮："+lastError);}
  }
 }
};
}
