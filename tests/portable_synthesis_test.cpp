#include "portable_synthesis.hpp"
#include <cassert>
#include <iostream>
#include <map>
using namespace ms;

namespace {
struct Object {
 std::array<Ptr,0x500/sizeof(Ptr)> fields{};
 Ptr parent=0;
 std::vector<Ptr> children;
 bool active=true;
 std::string label;
};
std::map<Ptr,std::unique_ptr<Object>> objects;
std::map<Ptr,std::string> strings;
std::map<uint32_t,Ptr> handles;
std::vector<Ptr> clones;
uint32_t serial=0;
unsigned opens=0;
bool rejected=false,failed=false;
Ptr gameEvent,customEvent;

Object& obj(Ptr p){return *objects.at(p);}
Ptr make(Ptr parent=0){
 auto item=std::make_unique<Object>();Ptr p=reinterpret_cast<Ptr>(item->fields.data());
 objects.emplace(p,std::move(item));store<Ptr>(p+0x10,1);obj(p).parent=parent;
 if(parent)obj(parent).children.push_back(p);
 return p;
}
Ptr actualHud(Ptr top,Ptr templ,Ptr font){
 auto hud=make();
 store(hud+abi::synthesisTop,top);store(hud+abi::synthesisTemplate,templ);store(hud+abi::synthesisFont,font);
 return hud;
}
Ptr identity(Ptr p,Ptr){return p;}
Ptr getParent(Ptr p,Ptr){return obj(p).parent;}
int count(Ptr p,Ptr){return int(obj(p).children.size());}
Ptr getChild(Ptr p,int i,Ptr){return obj(p).children.at(i);}
bool active(Ptr p,Ptr){return obj(p).active;}
void setActive(Ptr p,int on,Ptr){obj(p).active=on;}
void setParent(Ptr p,Ptr parent,bool,Ptr){auto& item=obj(p);assert(!item.parent);item.parent=parent;obj(parent).children.push_back(p);}
void destroy(Ptr p,Ptr){store<Ptr>(p+0x10,0);}
Ptr clone(Ptr source,Ptr){
 assert(count(source,0)>0);
 Ptr p=make();
 store(make(p)+abi::buttonClickEvent,gameEvent);
 clones.push_back(p);return p;
}
Ptr newString(Ptr zero,const char* text){assert(!zero);auto p=make();strings[p]=text;return p;}
uint32_t pin(Ptr p,int kind,Ptr){assert(objects.count(p)&&kind==2);handles[++serial]=p;return serial;}
void unpin(uint32_t h){assert(handles.erase(h)==1);}
Ptr component(Ptr p,Ptr name,Ptr){assert(strings.at(name)=="Button"||strings.at(name)=="Text");return p;}
Ptr newObject(Ptr type){assert(type==load<Ptr>(gameEvent));return make();}
Ptr createText(Ptr*,Ptr){return make();}
void setClick(Ptr button,Ptr event,Ptr){
 assert(load<Ptr>(button+abi::buttonClickEvent)==gameEvent&&event!=gameEvent);
 store(button+abi::buttonClickEvent,event);customEvent=event;
}
void setText(Ptr p,Ptr t,Ptr){obj(p).label=strings.at(t);}
void noOp(Ptr,Ptr){}
void setObject(Ptr,Ptr,Ptr){}
void setInt(Ptr,int,Ptr){}
void setTwo(Ptr,synthesisAbi::V2,Ptr){}
void setThree(Ptr,synthesisAbi::V3,Ptr){}
void setFour(Ptr,synthesisAbi::Color,Ptr){}
uint32_t open(Ptr page,Ptr argument,Ptr callback,Ptr actor,Ptr interaction,Ptr method){
 assert(strings.at(page)=="SynthesisPage"&&strings.at(argument).empty());
 assert(!callback&&!actor&&!interaction&&!method);
 ++opens;
 if(failed)throw std::runtime_error("fake-open-error");
 return !rejected;
}
Ptr resolve(abi::Offset o){switch(o){
#define F(a,b) case abi::a:return reinterpret_cast<Ptr>(&b)
 F(synthesisOpen,open);F(newString,newString);F(pin,pin);F(unpin,unpin);
 F(uiGoTransform,identity);F(uiComponentTransform,identity);F(gameObject,identity);F(getParent,getParent);
 F(uiChildCount,count);F(uiChild,getChild);F(goActive,active);F(goSetActive,setActive);F(destroy,destroy);
 F(clone,clone);F(uiSetParent,setParent);F(uiSetSibling,setInt);F(uiSetName,setText);F(componentByName,component);
 F(objectNew,newObject);F(uiNewClick,noOp);F(uiSetClick,setClick);F(uiCreateText,createText);F(uiSimpleFont,identity);
 F(uiAnchorMin,setTwo);F(uiAnchorMax,setTwo);F(uiSizeDelta,setTwo);F(uiAnchoredPosition,setTwo);F(uiLocalScale,setThree);
 F(uiSetFont,setObject);F(uiTextSet,setText);F(uiFontSize,setInt);F(uiAlignment,setInt);F(uiOverflowX,setInt);F(uiOverflowY,setInt);
 F(graphicSetColor,setFour);F(uiTargetGraphic,setObject);
#undef F
 default:assert(false);return 0;}}
}

int main(){
 auto controller=make(),top=make(),source=make(make()),font=make(),eventType=make();
 gameEvent=make();store(gameEvent,eventType);store(source+abi::buttonClickEvent,gameEvent);
 store(controller+abi::synthesisHud,actualHud(top,source,font));
 Engine engine(0);engine.resolver=resolve;std::string message;
 PortableSynthesis synthesis(engine,[&](const std::string& log,const std::string& value){assert(log=="synthesis");message=value;});
 synthesis.frame(controller,1000);
 assert(clones.empty()&&!synthesis.click(gameEvent));
 synthesis.setEnabled(true);synthesis.frame(controller,1100);synthesis.frame(controller,1200);
 assert(clones.size()==1&&obj(clones.back()).parent==top&&handles.size()==2);
 assert(obj(obj(obj(clones.back()).children.at(0)).children.at(0)).label=="合成");
 assert(load<Ptr>(source+abi::buttonClickEvent)==gameEvent&&!synthesis.click(gameEvent));
 assert(synthesis.click(customEvent)&&synthesis.click(customEvent)&&opens==0);
 synthesis.frame(controller,1300);synthesis.frame(controller,1400);
 assert(opens==1&&handles.size()==2&&message=="已请求打开合成台");
 assert(synthesis.click(customEvent));synthesis.frame(controller,1500);assert(opens==1);
 rejected=true;assert(synthesis.click(customEvent));synthesis.frame(controller,2500);
 assert(opens==2&&message=="游戏没有接受合成台打开请求");
 failed=true;assert(synthesis.click(customEvent));synthesis.frame(controller,3600);
 assert(opens==3&&handles.size()==2&&message.find("fake-open-error")!=std::string::npos);

 auto first=clones.back(),firstEvent=customEvent,nextTop=make();
 assert(synthesis.click(firstEvent)); // An old HUD click must not open over a new HUD.
 store(controller+abi::synthesisHud,actualHud(nextTop,source,font));synthesis.frame(controller,4700);
 assert(clones.size()==2&&!active(first,0)&&obj(clones.back()).parent==nextTop);
 assert(!synthesis.click(firstEvent)&&opens==3&&handles.size()==2);
 auto second=clones.back();
 assert(synthesis.click(customEvent));setActive(nextTop,0,0);synthesis.frame(controller,5000);
 setActive(nextTop,1,0);synthesis.frame(controller,5100);assert(opens==3);
 synthesis.setEnabled(false);synthesis.frame(controller,5200);
 assert(!active(second,0)&&handles.empty()&&!synthesis.click(customEvent));
 synthesis.setEnabled(true);synthesis.frame(controller,5300);
 assert(clones.size()==3&&handles.size()==2);
 destroy(clones.back(),0);synthesis.collect();
 assert(handles.empty()&&!synthesis.click(customEvent));
 std::cout<<"Portable synthesis: PASS (HUD lifecycle, isolated clicks, deferred open, rate limit, cleanup)\n";
}
