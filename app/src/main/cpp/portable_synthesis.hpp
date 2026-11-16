#pragma once
#include "runtime.hpp"
namespace ms {
class PortableSynthesis {
 Engine& e;std::function<void(const std::string&,const std::string&)> log;bool pending=false;uint64_t lastOpen=0;
 void say(const std::string& text){if(log){log("synthesis",text);}}
 public:
 explicit PortableSynthesis(Engine& engine,std::function<void(const std::string&,const std::string&)> reporter={}):e(engine),log(std::move(reporter)){}
 static bool supported(){for(auto offset:{abi::synthesisOpen,abi::newString,abi::pin,abi::unpin}){if(!abi::at(offset)||abi::at(offset)>=abi::notFound){return false;}}return true;}
 void open(){pending=true;}
 void tick(Ptr actor,uint64_t now){if(!pending){return;}pending=false;
  if(!supported()){say("当前客户端尚未适配随身合成台");return;}
  if(!actor){say("请先进入大世界");return;}
  if(lastOpen&&(now<lastOpen||now-lastOpen<1000)){say("合成台请求过于频繁");return;}lastOpen=now;
  std::vector<uint32_t> handles;auto release=[&]{while(!handles.empty()){e.call<void>(abi::unpin,handles.back());handles.pop_back();}};
  try{auto text=[&](const char* value){Ptr p=e.call<Ptr>(abi::newString,Ptr(0),value);require(p,"合成台字符串分配失败");auto h=e.call<uint32_t>(abi::pin,p,2,Ptr(0));require(h,"合成台 GC 引用失败");handles.push_back(h);return p;};
   Ptr page=text("SynthesisPage"),argument=text("");bool accepted=(e.call<uint32_t>(abi::synthesisOpen,page,argument,Ptr(0),Ptr(0),Ptr(0),Ptr(0))&1)!=0;
   release();say(accepted?"已请求打开合成台":"游戏没有接受合成台打开请求");
  }catch(const std::exception& ex){release();say(std::string("合成台打开失败：")+ex.what());}
 }
};
}
