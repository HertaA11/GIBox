// SPDX-License-Identifier: GPL-3.0-only
// Import side: what a mod's Present program does over time, found by running it in the core rather than by matching
// its text (ModelConverter's present-loop pattern stays the first choice; this is for the programs it refuses).
//
// A state is what can still change what is drawn: the values of the draw variables (ImportScan.drawVars: what the
// draws read and everything that decides it) and the state of the keys that set them. The rehearsal walks the state
// graph from the start: one Present with no input, or one key event before a Present. Two states are the same only
// when every draw variable is the same, with one exception that keeps a counter from looking endless: a variable that
// is only read as x % c (c a constant) and only written as x = x + d or x = constant is kept as its phase, x modulo a
// common multiple of the c (and its sign, as fmod keeps the dividend's sign). Any other counter keeps its full value,
// so a program whose early frames repeat but whose counter later opens another branch never repeats here, and is
// refused at the limit, not cut short.
//
// Refused, with the reason: a draw variable that depends on the clock, random(), a texture filter, a game resource
// or a transition (one run without input does not cover them); draws that change a draw variable (that depends on
// the game's draw order); commands the core does not port; a draw variable that is not a global of the main INI or
// takes a value that is not an integer (the converter builds frames by those values); no repeat, or more states,
// frames, time or memory than the limits allow.
#pragma once
#include "adapters.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace migoto {

struct RehearsalLimits {uint64_t presents=65536,states=65536,frames=64,ms=3000,bytes=64ull<<20;};
struct Rehearsal {
 using Limits=RehearsalLimits;
 struct Phase {std::string var;double modulus=0;};
 bool ok=false,external=false;std::string reason;   // external: refused because what is drawn follows the game (clock, its draws), not for a limit
 std::vector<std::map<std::string,int>> frames;   // each frame: the draw variables by their name in the main INI
 std::vector<size_t> sequence;size_t loop=0;      // no input: the frame of the start, then of each Present; after the last, from loop again
 std::vector<Phase> phases;                       // counters kept as their phase
 std::vector<std::string> keys,notes;             // the keys explored; what the result does not cover
 uint64_t states=0,presents=0,us=0;size_t keyFrames=0;   // keyFrames: frames only a key press reaches

 Rehearsal(Engine& engine,const std::string& mainNs,Limits limits={}):e(engine),lim(limits){
  auto started=std::chrono::steady_clock::now();
  try{run(mainNs,started);ok=true;}catch(const Refused& r){reason=r.why;external=r.external;frames.clear();sequence.clear();}
  us=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count());}

private:
 struct Refused {std::string why;bool external=false;};
 [[noreturn]] static void refuse(const std::string& why,bool external=false){throw Refused{why,external};}
 Engine& e;Limits lim;
 std::vector<Var*> all;std::vector<Var*> state;std::map<Var*,double> modulus;std::map<Var*,std::string> name;
 std::vector<KeySection*> relevant;std::vector<Preset*> presets;
 std::vector<std::pair<Var*,std::string>> seeds;   // what the draws read, by plain name
 struct Snap {std::vector<float> v;std::vector<int> current;std::vector<char> active;std::vector<std::map<Var*,float>> saved;std::vector<std::map<std::pair<int,char>,float>> savedParams;
  std::map<Var*,Engine::GlobalSave> gs;std::map<std::pair<int,char>,Engine::GlobalSave> gps;std::map<Var*,Transition> vt;std::map<std::pair<int,char>,Transition> pt;std::map<std::pair<int,char>,float> params;uint64_t frame=0;double now=0;
  size_t bytes()const{size_t n=sizeof(Snap)+v.size()*4+current.size()*4+active.size()+(gs.size()+gps.size()+vt.size()+pt.size()+params.size())*64;for(auto& m:saved)n+=m.size()*48;for(auto& m:savedParams)n+=m.size()*48;return n;}};

 Snap take()const{Snap s;for(auto* v:all)s.v.push_back(v->value);for(auto& k:e.keys)s.current.push_back(k.current);
  for(auto* p:presets){s.active.push_back(p->active);s.saved.push_back(p->saved);s.savedParams.push_back(p->savedParams);}
  s.gs=e.globalSaves;s.gps=e.globalParamSaves;s.vt=e.varTransitions;s.pt=e.paramTransitions;s.params=e.params;s.frame=e.frameNumber;s.now=e.nowSeconds;return s;}
 void restore(const Snap& s){for(size_t i=0;i<all.size();i++)all[i]->value=s.v[i];for(size_t i=0;i<e.keys.size();i++)e.keys[i].current=s.current[i];
  for(size_t i=0;i<presets.size();i++){presets[i]->active=s.active[i];presets[i]->saved=s.saved[i];presets[i]->savedParams=s.savedParams[i];}
  e.globalSaves=s.gs;e.globalParamSaves=s.gps;e.varTransitions=s.vt;e.paramTransitions=s.pt;e.params=s.params;e.frameNumber=s.frame;e.nowSeconds=s.now;e.queued.clear();e.pendingPost.clear();}

 std::string key()const{std::string k;k.reserve(state.size()*9+relevant.size()*8);
  for(auto* v:state){auto m=modulus.find(v);if(m!=modulus.end()){double x=v->value,r=std::fmod(x,m->second);if(r<0)r+=m->second;k+=x<0?'-':x>0?'+':'0';k.append(reinterpret_cast<const char*>(&r),sizeof r);}
   else k.append(reinterpret_cast<const char*>(&v->value),sizeof(float));}
  for(auto* key:relevant){k.append(reinterpret_cast<const char*>(&key->current),sizeof(int));
   for(auto& p:key->presets){k+=p.active?'a':'-';if(p.active)for(auto& s:p.saved){k.append(reinterpret_cast<const char*>(&s.second),sizeof(float));}}}
  for(auto& g:e.globalSaves)if(modulus.count(g.first)||std::find(state.begin(),state.end(),g.first)!=state.end()){k.append(reinterpret_cast<const char*>(&g.second.save),sizeof(float));k.append(reinterpret_cast<const char*>(&g.second.refcount),sizeof(int));}
  return k;}
 std::map<std::string,int> assignment()const{std::map<std::string,int> a;
  for(auto& s:seeds){float v=s.first->value;if(!(std::fabs(v)<=1000000)||v!=std::floor(v))refuse("绘制读取的 "+s.second+" = "+std::to_string(v)+" 不是整数；帧按整数取值转换");a[s.second]=int(v);}
  return a;}

 // ---- the phase of a counter: which variables are only read as x % c and only written as x + d or a constant
 std::set<Var*> written,irreducible;std::map<Var*,std::vector<double>> divisors;
 bool constVar(const Var* v)const{return !written.count(const_cast<Var*>(v))&&!(v->flags&VarPersist);}
 bool constant(const Node& n,double& out)const{if(n.kind==Node::Value){out=n.value;return true;}if(n.kind==Node::Variable&&constVar(n.var)){out=n.var->value;return true;}
  if(n.kind==Node::Unary&&(n.op==Op::Neg||n.op==Op::Plus)&&n.rhs&&constant(*n.rhs,out)){if(n.op==Op::Neg)out=-out;return true;}return false;}
 void reads(const Node& n,const Node* parent,const Var* self){
  if(n.kind==Node::Variable&&n.var!=self){double c;if(parent&&parent->kind==Node::Binary&&parent->op==Op::Mod&&parent->lhs.get()==&n&&parent->rhs&&constant(*parent->rhs,c)&&c!=0)divisors[n.var].push_back(std::fabs(c));else irreducible.insert(n.var);}
  if(n.lhs)reads(*n.lhs,&n,self);
  if(n.rhs)reads(*n.rhs,&n,self);}
 // x = x + d, x = d + x, x = x - d: the read of x itself is the counter's own step.
 const Var* increment(const Assign& a)const{const Node& r=*a.value.root;double d;
  if(r.kind!=Node::Binary||(r.op!=Op::Add&&r.op!=Op::Sub)||!r.lhs||!r.rhs)return nullptr;
  if(r.lhs->kind==Node::Variable&&r.lhs->var==a.var&&constant(*r.rhs,d))return a.var;
  if(r.op==Op::Add&&r.rhs->kind==Node::Variable&&r.rhs->var==a.var&&constant(*r.lhs,d))return a.var;
  return nullptr;}
 // Every command once: an else-if chain shares its branches between pre and post, and following both is exponential.
 template<class F>void commands(CommandList& l,F f){std::set<const Command*> done;std::vector<std::pair<CommandList*,int>> todo{{&l,0}};std::set<const CommandList*> lists;
  while(!todo.empty()){auto [list,depth]=todo.back();todo.pop_back();if(!lists.insert(list).second)continue;if(depth>1024)refuse("命令列表嵌套过深，无法预演");
   for(auto& c:list->commands){if(!done.insert(c.get()).second)continue;f(*c);
    if(c->kind()==Kind::If){auto* i=static_cast<If*>(c.get());for(auto* sub:{&i->truePre,&i->truePost,&i->falsePre,&i->falsePost})todo.push_back({sub,depth+1});}}}}
 template<class F>void everyList(F f){for(auto& kv:e.lists)if(kv.first!="constants"){f(kv.second->pre);f(kv.second->post);}for(auto& k:e.keys)for(auto& p:k.presets)if(p.run){f(p.run->pre);f(p.run->post);}}
 void phasesOf(){
  everyList([&](CommandList& l){commands(l,[&](Command& c){if(c.kind()==Kind::Assign){auto& a=static_cast<Assign&>(c);if(!a.param)written.insert(a.var);}});});
  for(auto& k:e.keys)for(auto& p:k.presets)for(auto& v:p.vars)written.insert(v.first);
  everyList([&](CommandList& l){commands(l,[&](Command& c){
   if(c.kind()==Kind::Assign){auto& a=static_cast<Assign&>(c);if(a.param){reads(*a.value.root,nullptr,nullptr);return;}double d;
    const Var* self=increment(a);if(!self&&!constant(*a.value.root,d))irreducible.insert(a.var);reads(*a.value.root,nullptr,self);}
   else if(c.kind()==Kind::If)reads(*static_cast<If&>(c).cond.root,nullptr,nullptr);
   else if(c.kind()==Kind::Draw)for(auto& x:static_cast<DrawCmd&>(c).args)reads(*x.root,nullptr,nullptr);});});
  for(auto& k:e.keys)for(auto& p:k.presets)if(p.conditional)reads(*p.condition.root,nullptr,nullptr);
  for(auto* v:state){if(irreducible.count(v)||!divisors.count(v))continue;
   // A common multiple of the divisors, in 1/1024 steps; divisors that are not, or a multiple too large, keep the full value.
   uint64_t m=1;bool fits=true;for(double c:divisors[v]){double s=c*1024;if(std::fabs(s-std::round(s))>1e-6||s<1||s>double(1u<<30)){fits=false;break;}
    uint64_t q=uint64_t(std::llround(s)),a=m,b=q;while(b){uint64_t t=a%b;a=b;b=t;}m=m/a*q;if(m>(1ull<<40)){fits=false;break;}}
   if(fits){modulus[v]=double(m)/1024;phases.push_back({name[v],double(m)/1024});}}}

 void run(const std::string& mainNs,std::chrono::steady_clock::time_point started){
  ImportScan scan(e);
  if(!scan.inputs.empty())refuse("绘制内容依赖 MOD 自身变量以外的数据："+scan.inputs.front(),true);
  for(auto& r:scan.reasons)if(r.find("嵌套过深")!=std::string::npos)refuse(r);
  if(!scan.unported.empty())refuse("包含逻辑核心未移植的命令："+scan.unported.front());
  if(!scan.customShaders.empty())refuse("运行了自定义着色器："+scan.customShaders.front());
  for(auto& g:e.globals)name[&g.second]=g.first;
  for(auto& kv:e.lists)for(auto& v:kv.second->locals)name[&v]=kv.second->name+" local "+v.name;
  for(auto& k:e.keys)for(auto& p:k.presets)if(p.run)for(auto& v:p.run->locals)name[&v]=k.name+" local "+v.name;
  for(auto& w:scan.writes)if(w.inDraw&&scan.drawVars.count(w.var))refuse("绘制过程中修改了 "+name[w.var]+"（"+w.where+"），其效果取决于游戏的绘制顺序",true);
  // What the draws read, by the name the main INI gives it.
  std::string prefix="$\\"+lower(mainNs)+"\\";
  for(auto* v:scan.seeds){auto n=name[v];if(n.empty())continue;
   if(n.compare(0,prefix.size(),prefix)==0)seeds.push_back({v,"$"+n.substr(prefix.size())});
   else if(n.find('\\')==std::string::npos&&n[0]=='$')seeds.push_back({v,n});
   else refuse("绘制读取的 "+n+" 不是主 INI 的全局变量；帧按主 INI 的变量转换");}
  for(auto& g:e.globals)all.push_back(&g.second);
  for(auto& kv:e.lists)for(auto& v:kv.second->locals)all.push_back(&v);
  for(auto& k:e.keys)for(auto& p:k.presets){presets.push_back(&p);if(p.run)for(auto& v:p.run->locals)all.push_back(&v);}
  for(auto* v:all)if(scan.drawVars.count(v))state.push_back(v);
  phasesOf();
  for(auto* v:state)if(v->flags&VarPersist){notes.push_back(name[v]+" 为 persist 变量：预演从 INI 中的初始值开始，保存值若不在预演得到的状态内则不会被转换");}
  // The keys that set a draw variable (directly or through their run lists), and the events they take.
  struct Input {KeySection* key;int kind;};std::vector<Input> inputs;
  for(auto& k:e.keys){bool sets=false;for(auto& p:k.presets){for(auto& v:p.vars)sets|=scan.drawVars.count(v.first)>0;
    if(p.run)for(auto* l:{&p.run->pre,&p.run->post})commands(*l,[&](Command& c){if(c.kind()==Kind::Assign&&!static_cast<Assign&>(c).param)sets|=scan.drawVars.count(static_cast<Assign&>(c).var)>0;});}
   if(!sets||k.presets.empty())continue;
   if(k.delay||k.releaseDelay)refuse("["+k.name+"] 延迟改变绘制内容，逻辑核心未移植此功能");
   relevant.push_back(&k);keys.push_back(k.name);
   if(k.type=="hold"){inputs.push_back({&k,0});inputs.push_back({&k,1});}
   else{inputs.push_back({&k,0});if(k.type=="cycle"&&!k.backs.empty())inputs.push_back({&k,2});}}
  // ---- the walk
  std::unordered_map<std::string,size_t> index;std::vector<Snap> snaps;std::vector<size_t> frameOf;std::map<std::map<std::string,int>,size_t> frameIndex;size_t bytes=0;
  std::map<Var*,std::set<float>> seen;
  auto check=[&]{if(uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count())>lim.ms)refuse("预演超过 "+std::to_string(lim.ms)+" ms（"+std::to_string(presents)+" 次 Present，"+std::to_string(snaps.size())+" 个状态）");
   if(!e.contextBindings.empty())refuse("在绘制调用之外绑定了槽位（"+e.contextBindings.begin()->first+"），转换器无法得知游戏会用它绘制什么");
   if(!e.customResources.empty())refuse("程序运行中对 [Resource] 赋值（"+e.customResources.begin()->first+"），帧只按变量转换");};
  auto visit=[&]()->size_t{check();auto k=key();auto it=index.find(k);if(it!=index.end())return it->second;
   if(snaps.size()>=lim.states)refuse("程序状态超过 "+std::to_string(lim.states)+" 个"+(inputs.empty()?"":"（含按键，"+std::to_string(inputs.size())+" 种按键事件）"));
   auto a=assignment();auto f=frameIndex.find(a);if(f==frameIndex.end()){if(frameIndex.size()>=lim.frames)refuse("不同的帧超过 "+std::to_string(lim.frames)+" 个（按绘制读取的值组合计）");f=frameIndex.emplace(a,frames.size()).first;frames.push_back(a);}
   snaps.push_back(inputs.empty()?Snap{}:take());bytes+=snaps.back().bytes();if(bytes>lim.bytes)refuse("预演状态超过 "+std::to_string(lim.bytes>>20)+" MiB");
   frameOf.push_back(f->second);index.emplace(std::move(k),snaps.size()-1);return snaps.size()-1;};
  auto present=[&]{presents++;e.frame(double(presents)/60.0);};
  // No input, from the start, until a state repeats.
  std::map<size_t,size_t> stepOf;size_t cur=visit();stepOf[cur]=0;sequence.push_back(frameOf[cur]);
  for(;;){present();for(auto* v:state)if(!modulus.count(v)&&seen[v].size()<=lim.presents)seen[v].insert(v->value);
   size_t next=visit();auto it=stepOf.find(next);if(it!=stepOf.end()){loop=it->second;break;}
   stepOf[next]=sequence.size();sequence.push_back(frameOf[next]);
   if(sequence.size()>=lim.presents){std::string growing;for(auto& kv:seen)if(kv.second.size()*2>=sequence.size())growing+=(growing.empty()?"":", ")+name[kv.first];
    refuse("程序状态在 "+std::to_string(lim.presents)+" 次 Present 内没有重复"+(growing.empty()?"":"；持续出现新值："+growing+"（计数器未以 x % c 的形式读取）"));}}
  size_t trajectory=snaps.size();std::set<size_t> plain(frameOf.begin(),frameOf.end());
  // Key events, from every state found, until no new state appears.
  for(size_t i=0;i<snaps.size()&&!inputs.empty();i++){
   std::vector<int> edges;if(i>=trajectory)edges.push_back(-1);for(size_t n=0;n<inputs.size();n++)edges.push_back(int(n));
   for(int edge:edges){Snap s=snaps[i];restore(s);
    if(edge>=0){auto& in=inputs[size_t(edge)];if(in.kind==0)e.keyDown(in.key->name);else if(in.kind==1)e.keyUp(in.key->name);else e.keyBack(in.key->name);if(in.kind==0&&in.key->type!="hold")e.keyUp(in.key->name);}
    present();visit();}}
  states=snaps.size();for(size_t f=0;f<frames.size();f++)if(!plain.count(f))keyFrames++;
  if(!relevant.empty()&&keyFrames)notes.push_back(std::to_string(keyFrames)+" 帧只能通过按键到达：逻辑核心驱动时可显示；未启用时按无按键的顺序播放");}
};

}  // namespace migoto
