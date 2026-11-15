#include "game_tools.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
using namespace ms;
namespace {
using Vec=GameTools::Vec;
std::vector<void*> allocated;std::map<uint32_t,Ptr> handles;std::map<Ptr,std::string> strings;uint32_t serial=0;Ptr actor,root,go,body,collider,cam,camTransform;Vec position{},forward{0,0,1};bool gravity=true,kinematic=false,collision=true,missing=false,failRestore=false;unsigned moves=0,opens=0;
Ptr object(){void* p=std::calloc(1,64);assert(p);allocated.push_back(p);Ptr value=reinterpret_cast<Ptr>(p);store<Ptr>(value+0x10,value+32);return value;}
uint32_t pin(Ptr p,int kind,Ptr){assert(p&&kind==2);handles[++serial]=p;return serial;}void unpin(uint32_t id){assert(handles.erase(id)==1);}
Ptr stringNew(Ptr zero,const char* s){assert(!zero);Ptr p=object();strings[p]=s;return p;}
Ptr entityRoot(Ptr p,Ptr){assert(p==actor);return root;}Vec entityPosition(Ptr p,Ptr){assert(p==actor);return position;}
Vec transformPosition(Ptr p,Ptr){assert(p==root);return position;}void setPosition(Ptr p,Vec v,int teleport,Ptr){assert(p==actor&&!teleport);position=v;moves++;}
Ptr gameObject(Ptr p,Ptr){assert(p==root);return go;}Ptr component(Ptr p,Ptr name,Ptr){assert(p==go);if(missing){return 0;}return strings.at(name)=="Rigidbody"?body:collider;}
Ptr attachedBody(Ptr p,Ptr){assert(p==collider);return body;}uint32_t getGravity(Ptr p,Ptr){assert(p==body);return gravity;}uint32_t getKinematic(Ptr p,Ptr){assert(p==body);return kinematic;}uint32_t getCollision(Ptr p,Ptr){assert(p==collider);return collision;}
void setGravity(Ptr p,int v,Ptr){assert(p==body);gravity=v!=0;}void setKinematic(Ptr p,int v,Ptr){assert(p==body);kinematic=v!=0;}
void setCollision(Ptr p,int v,Ptr){assert(p==collider);if(v&&failRestore){throw std::runtime_error("retry");}collision=v!=0;}
Ptr camera(Ptr){return cam;}Ptr cameraTransform(Ptr p,Ptr){assert(p==cam);return camTransform;}Vec direction(Ptr p,Ptr){assert(p==camTransform||p==root);return forward;}
uint32_t synthesis(Ptr name,Ptr argument,Ptr callback,Ptr entity,Ptr interaction,Ptr method){assert(strings.at(name)=="Synthesis"&&strings.at(argument).empty());assert(!callback&&!entity&&!interaction&&!method);opens++;return 1;}
Ptr resolve(abi::Offset offset){switch(offset){
#define F(a,b) case abi::a:return reinterpret_cast<Ptr>(&b)
 F(pin,pin);F(unpin,unpin);F(newString,stringNew);F(toolsEntityRoot,entityRoot);F(toolsEntityPosition,entityPosition);F(toolsTransformPosition,transformPosition);F(toolsEntitySetPosition,setPosition);F(gameObject,gameObject);F(componentByName,component);F(toolsColliderBody,attachedBody);F(toolsGravityGet,getGravity);F(toolsKinematicGet,getKinematic);F(toolsColliderGet,getCollision);F(toolsGravitySet,setGravity);F(toolsKinematicSet,setKinematic);F(toolsColliderSet,setCollision);F(toolsCamera,camera);F(toolsComponentTransform,cameraTransform);F(toolsForward,direction);F(toolsSynthesis,synthesis);
#undef F
 default:assert(false);return 0;}}
GameTools::Input input(uint64_t arm,uint64_t at,float x=0,float z=1){return {true,x,z,0,5,arm,at};}
bool near(float a,float b){return std::abs(a-b)<.001f;}
void restored(){assert(gravity&&!kinematic&&collision&&handles.empty());}
}
int main(){actor=object();root=object();go=object();body=object();collider=object();cam=object();camTransform=object();Engine engine(0);engine.resolver=resolve;GameTools tools(engine);
 tools.tick(actor,false,1000);restored();assert(!moves&&!opens);
 tools.open();tools.tick(actor,true,1000);assert(opens==1&&handles.empty());tools.tick(actor,true,1050);assert(opens==1);tools.open();tools.tick(actor,true,1100);assert(opens==1);tools.open();tools.tick(0,true,2200);assert(opens==1);
 tools.submit(input(1,2300,1,1));tools.tick(actor,true,2300);assert(!gravity&&kinematic&&!collision&&!handles.empty());tools.tick(actor,true,2350);assert(near(position.x,.25f/std::sqrt(2.f))&&near(position.z,position.x));
 tools.submit({});tools.tick(actor,true,2360);restored();
 // Timeout locks this activation; a late heartbeat cannot silently resume movement.
 tools.submit(input(2,2400));tools.tick(actor,true,2400);tools.tick(actor,true,2800);restored();unsigned n=moves;tools.submit(input(2,2801));tools.tick(actor,true,2801);assert(moves==n);tools.submit(input(3,2810));tools.tick(actor,true,2810);assert(!collision);tools.tick(actor,false,2820);restored();
 // Camera-relative forward, bounded frame time, and server correction stop.
 forward={1,0,0};position={};tools.submit(input(4,3000));tools.tick(actor,true,3000);tools.tick(actor,true,3200);assert(near(position.x,.25f)&&near(position.z,0));position.x+=50;tools.tick(actor,true,3210);restored();n=moves;tools.submit(input(4,3220));tools.tick(actor,true,3220);assert(n==moves);
 // Unavailable physics components must not move a model-only root.
 missing=true;tools.submit(input(5,3300));tools.tick(actor,true,3300);restored();assert(n==moves);missing=false;
 // Restore retries retain their GC roots until all original fields can be restored.
 tools.submit(input(6,3400));tools.tick(actor,true,3400);failRestore=true;tools.submit({});tools.tick(actor,true,3410);assert(!handles.empty());failRestore=false;tools.tick(actor,true,3420);restored();
 // Opening synthesis stops an active movement activation, without resuming next tick.
 tools.submit(input(7,3500));tools.tick(actor,true,3500);tools.open();tools.tick(actor,true,3510);restored();assert(opens==2);n=moves;tools.tick(actor,true,3520);assert(n==moves);
 // Preserve already-disabled collision and the game's original physics flags.
 gravity=false;kinematic=true;collision=false;tools.submit(input(8,3600));tools.tick(actor,true,3600);tools.submit({});tools.tick(actor,true,3610);assert(!gravity&&kinematic&&!collision&&handles.empty());gravity=true;kinematic=false;collision=true;
 // Missing global-client RVAs are refused, rather than calling CN addresses.
 auto saved=abi::table();abi::table()={{uintptr_t(abi::toolsSynthesis),uintptr_t(abi::toolsSynthesis)}};assert(!GameTools::supported());tools.submit(input(9,3700));tools.open();tools.tick(actor,true,3700);restored();assert(opens==2);abi::table()=saved;
 tools.shutdown();restored();for(void* p:allocated){std::free(p);}std::cout<<"Game tools: PASS (native entity movement, physics restoration and retry, input lease, correction stop, camera direction, synthesis request, unsupported build refusal)\n";
}
