#include "portable_synthesis.hpp"
#include <cassert>
#include <iostream>
#include <map>
using namespace ms;
namespace {
std::map<Ptr,std::string> strings;std::map<uint32_t,Ptr> handles;uint32_t handle=0;Ptr serial=0;unsigned calls=0;bool accepted=true,fail=false;
Ptr newString(Ptr zero,const char* text){assert(!zero);strings[++serial]=text;return serial;}
uint32_t pin(Ptr p,int kind,Ptr){assert(strings.count(p)&&kind==2);handles[++handle]=p;return handle;}
void unpin(uint32_t h){assert(handles.erase(h)==1);}
uint32_t open(Ptr page,Ptr argument,Ptr callback,Ptr actor,Ptr interaction,Ptr method){assert(strings.at(page)=="SynthesisPage"&&strings.at(argument).empty());assert(!callback&&!actor&&!interaction&&!method);calls++;if(fail){throw std::runtime_error("fake-open-error");}return accepted;}
Ptr resolve(abi::Offset o){switch(o){
#define F(a,b) case abi::a:return reinterpret_cast<Ptr>(&b)
 F(synthesisOpen,open);F(newString,newString);F(pin,pin);F(unpin,unpin);
#undef F
 default:assert(false);return 0;}}
}
int main(){Engine engine(0);engine.resolver=resolve;std::string message;PortableSynthesis synthesis(engine,[&](const std::string& event,const std::string& value){assert(event=="synthesis");message=value;});
 synthesis.tick(1,1000);assert(calls==0);synthesis.open();synthesis.tick(0,1000);assert(!calls&&message=="请先进入大世界");
 synthesis.open();synthesis.tick(1,1100);assert(calls==1&&handles.empty()&&message=="已请求打开合成台");synthesis.tick(1,1200);assert(calls==1);
 synthesis.open();synthesis.tick(1,1300);assert(calls==1&&message=="合成台请求过于频繁");
 accepted=false;synthesis.open();synthesis.tick(1,2200);assert(calls==2&&handles.empty()&&message=="游戏没有接受合成台打开请求");
 fail=true;synthesis.open();synthesis.tick(1,3300);assert(calls==3&&handles.empty()&&message.find("fake-open-error")!=std::string::npos);
 auto saved=abi::table();abi::table()={{uintptr_t(abi::synthesisOpen),uintptr_t(abi::synthesisOpen)}};assert(!PortableSynthesis::supported());synthesis.open();synthesis.tick(1,4400);assert(calls==3&&handles.empty());abi::table()=saved;
 std::cout<<"Portable synthesis: PASS (SynthesisPage key, one-shot requests, rate limit, game rejection, error cleanup, unsupported build)\n";
}
