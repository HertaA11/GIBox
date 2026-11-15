#pragma once
#include "runtime.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <mutex>
namespace ms {
class GameTools {
 public:
 struct Vec {float x=0,y=0,z=0;};
 struct Input {bool enabled=false;float x=0,z=0,vertical=0,speed=5;uint64_t arm=0,at=0;};
 private:
 Engine& e;std::function<void(const std::string&,const std::string&)> log;
 mutable std::mutex mutex;Input input;uint64_t requested=0,seen=0,blockedArm=0,lastOpen=0;bool allowed=false,reportedActive=false;std::string message="工具尚未启用";
 Ptr entity=0,root=0,body=0,collider=0,camera=0,rootNative=0,bodyNative=0,colliderNative=0;std::vector<uint32_t> handles;bool saved=false,oldGravity=false,oldKinematic=false,oldCollider=false;Vec target;uint64_t last=0;
 static bool live(Ptr p){return p&&load<Ptr>(p+0x10)!=0;}
 static bool finite(Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
 static float distance(Vec a,Vec b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));}
 void pin(Ptr p){if(p){auto h=e.call<uint32_t>(abi::pin,p,2,Ptr(0));require(h,"工具 GC 引用失败");handles.push_back(h);}}
 Ptr text(const char* s){Ptr p=e.call<Ptr>(abi::newString,Ptr(0),s);require(p,"工具字符串分配失败");pin(p);return p;}
 void say(const std::string& value){bool changed;{std::lock_guard<std::mutex> lock(mutex);changed=message!=value;message=value;}if(changed&&log){log("game-tools",value);}}
 bool flag(abi::Offset getter,Ptr p){return (e.call<uint32_t>(getter,p,Ptr(0))&1)!=0;}
 void publish(){std::lock_guard<std::mutex> lock(mutex);reportedActive=entity!=0;}
 bool restore(){bool ok=true;auto attempt=[&](auto action){try{action();}catch(const std::exception&){ok=false;}};
  if(saved){if(live(collider)&&load<Ptr>(collider+0x10)==colliderNative){attempt([&]{e.op(abi::toolsColliderSet,collider,oldCollider?1:0);});}
   if(live(body)&&load<Ptr>(body+0x10)==bodyNative){attempt([&]{e.op(abi::toolsGravitySet,body,oldGravity?1:0);});attempt([&]{e.op(abi::toolsKinematicSet,body,oldKinematic?1:0);});}}
  if(!ok){say("正在恢复角色物理状态");return false;}for(auto h:handles){e.call<void>(abi::unpin,h);}handles.clear();entity=root=body=collider=camera=0;saved=false;last=0;publish();return true;
 }
 void attach(Ptr actor,uint64_t now){entity=actor;pin(actor);root=e.get(abi::toolsEntityRoot,actor);require(live(root),"角色物理根节点未就绪");pin(root);rootNative=load<Ptr>(root+0x10);
  Vec actual=e.call<Vec>(abi::toolsEntityPosition,actor,Ptr(0)),physical=e.call<Vec>(abi::toolsTransformPosition,root,Ptr(0));require(finite(actual)&&finite(physical)&&distance(actual,physical)<.02f,"角色实体与物理根位置不一致");
  Ptr go=e.get(abi::gameObject,root);require(live(go),"角色对象未就绪");pin(go);body=e.call<Ptr>(abi::componentByName,go,text("Rigidbody"),Ptr(0));collider=e.call<Ptr>(abi::componentByName,go,text("Collider"),Ptr(0));
  require(live(body)&&live(collider),"尚未找到角色刚体和碰撞体");pin(body);pin(collider);bodyNative=load<Ptr>(body+0x10);colliderNative=load<Ptr>(collider+0x10);
  require(e.get(abi::toolsColliderBody,collider)==body,"角色碰撞体与刚体不对应");oldGravity=flag(abi::toolsGravityGet,body);oldKinematic=flag(abi::toolsKinematicGet,body);oldCollider=flag(abi::toolsColliderGet,collider);saved=true;
  e.op(abi::toolsGravitySet,body,0);e.op(abi::toolsKinematicSet,body,1);e.op(abi::toolsColliderSet,collider,0);
  require(!flag(abi::toolsGravityGet,body)&&flag(abi::toolsKinematicGet,body)&&!flag(abi::toolsColliderGet,collider),"角色物理状态写入未生效");
  Ptr main=e.call<Ptr>(abi::toolsCamera,Ptr(0));if(live(main)){pin(main);camera=e.get(abi::toolsComponentTransform,main);if(live(camera)){pin(camera);}else{camera=0;}}
  target=actual;last=now;say("穿行已开启");publish();
 }
 void synthesis(Ptr actor,uint64_t now){if(!actor){say("请先进入大世界");return;}if(lastOpen&&now-lastOpen<1000){say("合成台请求过于频繁");return;}lastOpen=now;
  auto from=handles.size();try{Ptr name=text("Synthesis"),argument=text("");bool opened=(e.call<uint32_t>(abi::toolsSynthesis,name,argument,Ptr(0),Ptr(0),Ptr(0),Ptr(0))&1)!=0;
   say(opened?"已请求打开合成台":"游戏没有接受合成台打开请求");}catch(...){while(handles.size()>from){e.call<void>(abi::unpin,handles.back());handles.pop_back();}throw;}
  while(handles.size()>from){e.call<void>(abi::unpin,handles.back());handles.pop_back();}
 }
 public:
 explicit GameTools(Engine& engine,std::function<void(const std::string&,const std::string&)> reporter={}):e(engine),log(std::move(reporter)){}
 static bool supported(){for(auto offset:{abi::toolsSynthesis,abi::toolsEntityRoot,abi::toolsEntityPosition,abi::toolsEntitySetPosition,abi::toolsTransformPosition,abi::toolsForward,abi::toolsCamera,abi::toolsComponentTransform,abi::toolsColliderGet,abi::toolsColliderSet,abi::toolsColliderBody,abi::toolsGravityGet,abi::toolsGravitySet,abi::toolsKinematicGet,abi::toolsKinematicSet,abi::componentByName,abi::gameObject,abi::newString,abi::pin,abi::unpin}){if(!abi::at(offset)||abi::at(offset)>=abi::notFound){return false;}}return true;}
 void submit(Input value){if(!std::isfinite(value.x)||!std::isfinite(value.z)||!std::isfinite(value.vertical)||!std::isfinite(value.speed)){value.enabled=false;}value.x=std::max(-1.f,std::min(1.f,value.x));value.z=std::max(-1.f,std::min(1.f,value.z));value.vertical=std::max(-1.f,std::min(1.f,value.vertical));value.speed=std::max(1.f,std::min(20.f,value.speed));std::lock_guard<std::mutex> lock(mutex);input=value;}
 void open(){std::lock_guard<std::mutex> lock(mutex);requested++;}
 bool wantsTick()const{std::lock_guard<std::mutex> lock(mutex);return input.enabled||reportedActive||requested!=seen;}
 std::string status()const{std::lock_guard<std::mutex> lock(mutex);return std::string("{\"ready\":true,\"allowed\":")+(allowed?"true":"false")+",\"supported\":"+(supported()?"true":"false")+",\"active\":"+(reportedActive?"true":"false")+",\"blockedArm\":"+std::to_string(blockedArm)+",\"message\":"+quote(message)+"}";}
 void tick(Ptr actor,bool permitted,uint64_t now){Input in;bool action,permissionChanged;{std::lock_guard<std::mutex> lock(mutex);permissionChanged=allowed!=permitted;allowed=permitted;in=input;action=requested!=seen;seen=requested;}
  bool blocked;{std::lock_guard<std::mutex> lock(mutex);blocked=blockedArm&&blockedArm==in.arm;}
  if(!supported()){if(restore()&&(permitted||action)){say("当前客户端尚未适配游戏工具");}return;}
  if(permissionChanged&&permitted){say("工具已启用");}
  if(action){{std::lock_guard<std::mutex> lock(mutex);blockedArm=in.arm;}if(!restore()){return;}try{synthesis(actor,now);}catch(const std::exception& ex){say(std::string("合成台打开失败：")+ex.what());}return;}
  if(!permitted){if(restore()&&permissionChanged){say("工具尚未启用");}return;}
  if(in.enabled&&in.arm&&(now<in.at||now-in.at>300||!actor)){{std::lock_guard<std::mutex> lock(mutex);blockedArm=in.arm;}blocked=true;}
  if(!in.enabled||blocked||!in.arm||now<in.at||now-in.at>300||!actor){bool was=entity!=0;if(restore()&&was){say(blocked?"穿行已暂停，请关闭后重开":"穿行已停止");}return;}
  try{if(entity!=actor||!live(root)||load<Ptr>(root+0x10)!=rootNative||!live(body)||load<Ptr>(body+0x10)!=bodyNative||!live(collider)||load<Ptr>(collider+0x10)!=colliderNative){if(!restore()){return;}attach(actor,now);}
   Vec current=e.call<Vec>(abi::toolsEntityPosition,actor,Ptr(0));require(finite(current),"角色位置无效");require(distance(current,target)<std::max(2.f,in.speed*.5f),"角色位置被游戏或服务端大幅回写");
   float dt=last&&now>=last?std::min(.05f,float(now-last)/1000.f):0;last=now;Vec forward=e.call<Vec>(abi::toolsForward,camera&&live(camera)?camera:root,Ptr(0));require(finite(forward),"移动方向无效");float length=std::hypot(forward.x,forward.z);if(length<.05f){forward=e.call<Vec>(abi::toolsForward,root,Ptr(0));length=std::hypot(forward.x,forward.z);}require(length>=.05f,"移动方向尚未就绪");
   forward.x/=length;forward.z/=length;float axis=std::max(1.f,std::hypot(in.x,in.z)),scale=in.speed*dt;target.x+=(forward.z*in.x+forward.x*in.z)*scale/axis;target.z+=(-forward.x*in.x+forward.z*in.z)*scale/axis;target.y+=in.vertical*scale;
   require(finite(target),"目标位置无效");e.call<void>(abi::toolsEntitySetPosition,actor,target,int(0),Ptr(0));Vec back=e.call<Vec>(abi::toolsEntityPosition,actor,Ptr(0));require(finite(back)&&distance(back,target)<.1f,"实体位置写入未生效");
  }catch(const std::exception& ex){{std::lock_guard<std::mutex> lock(mutex);blockedArm=in.arm;}restore();say(std::string("穿行已暂停：")+ex.what());}publish();
 }
 void shutdown(){restore();}
};
}
