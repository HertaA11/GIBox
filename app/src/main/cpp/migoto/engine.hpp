// SPDX-License-Identifier: GPL-3.0-only
// GIBox 3DMigoto logic core: command lists and when they run.
// Ported from 3DMigoto (XXMI fork) at a7847eae (see third_party/3dmigoto/README.md):
//  - IniHandler.cpp: ParseCommandList, ParseCommandListLine, ParseConstantsSection, RegisterGlobalVariable, the
//    TextureOverride section keys and sort order, RegisterPresetKeyBindings;
//  - CommandList.cpp: AddCommandToList (pre/post placement), local variables and their scopes, the if / else if /
//    else / endif parser and IfCommand::run, RunExplicitCommandList, CheckTextureOverrideCommand, handling,
//    VariableAssignment, the draw command keys, _RunCommandList (recursion limit, abort), process_texture_filter;
//  - Override.cpp: key types (activate, hold, toggle, cycle), conditions, cycles (wrap, smart), the global save of
//    held keys, transitions, and the key "run" lists;
//  - HackerDXGI.cpp RunFrameActions and Present: the order [Present] pre, input, transitions, frame_number++, the
//    game's Present, [Present] post; HackerContext.cpp: [Constants] pre then post, BeforeDraw / AfterDraw.
// What is not ported (GPU work: resource copies, custom shaders, pools, presets, frame analysis) is accepted only as
// a recorded effect or a diagnostic; nothing here touches DirectX, Windows, Unity or JNI.
#pragma once
#include "expr.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <deque>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <set>

namespace migoto {

// What a slot or a [Resource] holds, as far as the logic can know: nothing set, null, a game resource known by its
// hash, or a mod [Resource] section (its lowercase namespaced name).
struct Ref {
 enum Kind {None,Null,Game,Mod} kind=None;uint32_t hash=0;std::string resource;
 static Ref game(uint32_t h){Ref r;r.kind=Game;r.hash=h;return r;}
 static Ref mod(const std::string& k){Ref r;r.kind=Mod;r.resource=k;return r;}
 bool operator==(const Ref& o)const{return kind==o.kind&&hash==o.hash&&resource==o.resource;}
 bool operator!=(const Ref& o)const{return !(*this==o);}
};

struct ResourceDef {std::string name,shortName,ns,path;std::map<std::string,std::string> values;};   // values: lowercase key -> first value

// One draw as the adapter sees it: what the game bound, the draw arguments it knows, and the order in which the
// host configuration (d3dx.ini ShaderOverride / ShaderRegex lists) runs checktextureoverride for it. That order is
// configuration, not 3DMigoto code, so the adapter states it.
struct DrawInfo {
 std::map<std::string,Ref> bindings;
 std::vector<std::string> checks;
 std::map<std::string,float> call;   // vertex_count, index_count, instance_count, first_vertex, first_index, first_instance, ...
};
struct IssuedDraw {std::string kind,where;std::vector<float> args;bool automatic=false,fromCaller=false;std::map<std::string,Ref> bindings;};
struct DrawResult {
 bool skip=false;                        // handling = skip: the game's own draw does not happen
 std::vector<IssuedDraw> draws;          // draws the command lists issue, with the slots at that moment
 std::map<std::string,Ref> bindings;     // the slots after the pre lists
 std::vector<std::string> matched;       // TextureOverride sections run (pre), in order
 std::vector<std::string> customShaders; // run = CustomShader...: recorded, not executed
};

// Everything that happened, in order; tests compare these against upstream's semantics.
struct Event {enum Kind {Assign,Branch,Run,Bind,Draw,Skip,Abort,Match,Key,Unported} kind;std::string where,detail;float before=0,after=0;};

class Engine;
struct RunState;
// kind: what a command is, for the parser's endif and the import scan (no RTTI: the Android build may not have it).
enum class Kind {Other,Assign,Else,If,Run,Bind,Draw,CustomShader,Unported};
struct Command {std::string line;virtual ~Command()=default;virtual void run(RunState&)=0;virtual Kind kind()const{return Kind::Other;}};
using CommandPtr=std::shared_ptr<Command>;
struct CommandList {std::vector<CommandPtr> commands;bool post=false;std::string section;};
// A section with command lists ([TextureOverride], [CommandList], [Present], [Constants], a key's run). Locals live
// with the section, start at 0 and keep their value between runs (upstream static_vars).
struct ListSection {std::string name,ns;CommandList pre,post;std::deque<Var> locals;};

struct FuzzyNumber {enum Op {Any,Eq,Ne,Lt,Le,Gt,Ge} op=Any;uint32_t value=0;
 bool matches(uint32_t v)const{switch(op){case Any:return true;case Eq:return v==value;case Ne:return v!=value;case Lt:return v<value;case Le:return v<=value;case Gt:return v>value;case Ge:return v>=value;}return false;}};
struct TextureOverride {ListSection* list=nullptr;std::string name;uint32_t hash=0;int priority=0;float filterIndex=FLT_MAX;bool drawMatch=false;std::map<std::string,FuzzyNumber> match;};

struct Transition {float start=0,target=0;double activation=0;int time=-1;bool cosine=false;};
struct Preset {
 std::map<Var*,float> vars,saved;std::map<std::pair<int,char>,float> params,savedParams;int transition=0,releaseTransition=0;bool cosine=false,releaseCosine=false;
 bool conditional=false,active=false;Expression condition;std::unique_ptr<ListSection> run;   // run: activate (pre) and deactivate (post) lists
};
struct KeySection {std::string name,ns,type;std::vector<std::string> keys,backs;std::vector<Preset> presets;int current=-1;bool wrap=true,smart=true;int delay=0,releaseDelay=0;};

struct RunState:Context {
 Engine& e;bool post=false,aborted=false;int recursion=0;const DrawInfo* draw=nullptr;DrawResult* result=nullptr;std::map<std::string,Ref>* bindings=nullptr;std::string thisSlot;
 explicit RunState(Engine& engine);
 float keywordValue(const Keyword& k)override;float slotFilter(const std::string& slot)override;float param(int i,char c)override;float randomValue(float max)override;
};

class Engine {
public:
 static constexpr int maxRecursion=256;   // MAX_COMMAND_LIST_RECURSION
 Diagnostics diag;IniSet ini;std::vector<Event>* trace=nullptr;
 std::map<std::string,Var> globals;std::map<std::string,float> initial;
 std::map<std::string,std::unique_ptr<ListSection>> lists;   // lowercase section name -> lists
 std::map<std::string,ResourceDef> resources;                 // lowercase section name
 std::map<uint32_t,std::vector<TextureOverride>> overrides;    // sorted by (match_priority, section name)
 std::vector<KeySection> keys;
 std::map<std::pair<int,char>,float> params;
 std::map<std::string,Ref> contextBindings;                   // slots set outside a draw (Present, keys)
 std::map<std::string,Ref> customResources;                   // what `ResourceX = ref ps-t0` left in a [Resource]
 std::map<std::string,float> host;                            // host keyword values the adapter provides
 std::function<float()> uniform;                              // [0,1) for random(); upstream seeds from frame and clock
 double nowSeconds=0;float frameTime=0,fps=0;uint64_t frameNumber=0;bool persistDirty=false;
 std::map<Var*,Transition> varTransitions;std::map<std::pair<int,char>,Transition> paramTransitions;
 struct GlobalSave {float save=0;int refcount=0;};std::map<Var*,GlobalSave> globalSaves;std::map<std::pair<int,char>,GlobalSave> globalParamSaves;
 std::vector<CommandList*> pendingPost;
 // Select (GIBox, not upstream): a cycle key's preset by its index, activated as a press reaching it would activate it
 // (condition, transition, run); upstream only steps (Down / Back), and mods offer direct jumps as activate keys.
 struct Input {size_t key;enum Kind {Down,Up,Back,Select} kind;int index=-1;};std::vector<Input> queued;

 void note(Event::Kind k,const std::string& where,const std::string& detail,float before=0,float after=0){if(trace)trace->push_back({k,where,detail,before,after});}
 void limitation(const std::string& where,const std::string& m){diag.add(Diagnostic::Limitation,where,m);}
 void warning(const std::string& where,const std::string& m){diag.add(Diagnostic::Warning,where,m);}

 // ---- lookups
 Var* global(const std::string& lowName,const std::string& ns){if(!ns.empty()){auto it=globals.find(namespacedVar(lowName,ns));if(it!=globals.end())return &it->second;}
  auto it=globals.find(lowName);return it==globals.end()?nullptr:&it->second;}
 template<class Map>typename Map::mapped_type* bySection(Map& m,const std::string& name,const std::string& ns){std::string n;
  if(!ns.empty()&&namespacedSection(name,ns,n)){auto it=m.find(lower(n));if(it!=m.end())return &it->second;}
  auto it=m.find(lower(name));return it==m.end()?nullptr:&it->second;}
 ListSection* listSection(const std::string& name,const std::string& ns){auto* p=bySection(lists,name,ns);return p?p->get():nullptr;}
 std::string resourceKey(const std::string& name,const std::string& ns){auto* r=bySection(resources,name,ns);return r?lower(r->name):std::string();}
 std::vector<const TextureOverride*> matches(uint32_t hash,const DrawInfo* d){std::vector<const TextureOverride*> out;auto it=overrides.find(hash);if(it==overrides.end())return out;
  for(auto& t:it->second){if(t.drawMatch){if(!d)continue;bool ok=true;for(auto& m:t.match){auto c=d->call.find(m.first);
     if(c==d->call.end()){limitation(t.name,"match_"+m.first+" 需要绘制调用的 "+m.first+"，但适配层未提供");ok=false;break;}
     ok=ok&&m.second.matches(uint32_t(c->second));}
    if(!ok)continue;}
   out.push_back(&t);}
  return out;}

 // ---- loading
 void load(const std::vector<IniFile>& files);
 // [Constants]: globals back to their initial values, the pre list, persisted values (upstream runs d3dx_user.ini's
 // [Constants] lines inside the pre list, last), then the post list. On load and on every reload.
 void start(const std::map<std::string,float>& persisted={});
 // One presented frame. Draws of the frame come before it (draw()); input queued since the last frame is dispatched
 // after [Present] pre, like DispatchInputEvents in RunFrameActions.
 void frame(double seconds);
 DrawResult draw(const DrawInfo& d);
 void keyDown(const std::string& section){queue(section,Input::Down);}
 void keyUp(const std::string& section){queue(section,Input::Up);}
 void keyBack(const std::string& section){queue(section,Input::Back);}
 void keySelect(const std::string& section,int index){queue(section,Input::Select,index);}
 // What each key shows now: a cycle's preset matching the variables (smart's resync, without changing anything; -1 none),
 // whether an activate key's assignments are all in effect, whether a toggle / hold is on.
 struct KeyState {std::string name,type;int current=-1;bool active=false,matches=false;};
 bool matchesCurrent(const Preset& p){if(p.vars.empty()&&p.params.empty())return false;for(auto& v:p.vars)if(v.second!=currentOrTarget(v.first))return false;for(auto& v:p.params)if(v.second!=currentOrTarget(v.first))return false;return true;}
 std::vector<KeyState> keyStates(){std::vector<KeyState> out;
  for(auto& k:keys){KeyState st;st.name=k.name;st.type=k.type;
   if(k.type=="cycle"){if(k.current>=0&&size_t(k.current)<k.presets.size()&&matchesCurrent(k.presets[size_t(k.current)]))st.current=k.current;
    else for(size_t i=0;i<k.presets.size();i++)if(matchesCurrent(k.presets[i])){st.current=int(i);break;}}
   else if(!k.presets.empty()){st.active=k.presets[0].active;st.matches=matchesCurrent(k.presets[0]);}
   out.push_back(st);}
  return out;}
 std::map<std::string,float> persistent()const{std::map<std::string,float> m;for(auto& kv:globals)if(kv.second.flags&VarPersist)m[kv.first]=kv.second.value;return m;}

 // ---- execution (used by the commands)
 void runList(CommandList& l,RunState& s,bool recursive=true){
  if(s.recursion>maxRecursion){warning(l.section,"命令列表递归超过上限，可能存在循环引用");return;}
  if(l.commands.empty())return;
  if(recursive)s.recursion++;
  for(size_t i=0;i<l.commands.size()&&!s.aborted;i++)l.commands[i]->run(s);
  if(recursive)s.recursion--;}
 void checkSlot(RunState& s,const std::string& slot,bool together,const std::string& where);
 void activate(KeySection& k,Preset& p,bool hold);
 void deactivate(Preset& p);
 void schedule(const std::map<Var*,float>& vars,const std::map<std::pair<int,char>,float>& ps,int time,bool cosine){
  for(auto& v:vars){Transition t;t.start=v.first->value;t.target=v.second;t.activation=nowSeconds;t.time=time;t.cosine=cosine;varTransitions[v.first]=t;}
  for(auto& v:ps){Transition t;t.start=params[v.first];t.target=v.second;t.activation=nowSeconds;t.time=time;t.cosine=cosine;paramTransitions[v.first]=t;}}
 static float step(Transition& t,double now){if(t.time==-1)return FLT_MAX;if(t.time==0){t.time=-1;return t.target;}
  float percent=float((now-t.activation)*1000.0/t.time);if(percent>=1.0f){t.time=-1;return t.target;}
  if(t.cosine)percent=float((1.0-std::cos(percent*3.14159265358979323846))/2.0);
  return t.target*percent+t.start*(1.0f-percent);}
 void updateTransitions(){
  for(auto it=paramTransitions.begin();it!=paramTransitions.end();){params[it->first]=step(it->second,nowSeconds);if(it->second.time==-1)it=paramTransitions.erase(it);else ++it;}
  for(auto it=varTransitions.begin();it!=varTransitions.end();){float v=step(it->second,nowSeconds);
   if(it->first->value!=v){note(Event::Assign,"transition",it->first->name,it->first->value,v);it->first->value=v;if(it->first->flags&VarPersist)persistDirty=true;}
   if(it->second.time==-1)it=varTransitions.erase(it);else ++it;}
  auto pending=pendingPost;pendingPost.clear();for(auto* l:pending){RunState s(*this);s.post=true;s.bindings=&contextBindings;runList(*l,s);}}
 float currentOrTarget(Var* v){auto it=varTransitions.find(v);return it!=varTransitions.end()&&it->second.time!=-1?it->second.target:v->value;}
 float currentOrTarget(const std::pair<int,char>& p){auto it=paramTransitions.find(p);return it!=paramTransitions.end()&&it->second.time!=-1?it->second.target:params[p];}

private:
 struct Scope {std::deque<std::map<std::string,Var*>> levels;};
 // section: the namespaced name, or the name as the MOD writes it when only one file has a key of that name.
 void queue(const std::string& section,Input::Kind k,int index=-1){std::vector<size_t> hits;
  for(size_t i=0;i<keys.size();i++){std::string full;if(lower(keys[i].name)==lower(section)){hits={i};break;}
   if(!keys[i].ns.empty()&&namespacedSection(section,keys[i].ns,full)&&lower(full)==lower(keys[i].name))hits.push_back(i);}
  if(hits.size()!=1)throw std::invalid_argument(std::string(hits.empty()?"找不到按键段 ":"存在多个同名按键段 ")+section);
  queued.push_back({hits[0],k,index});}
 Resolver resolver(const std::string& ns,Scope* scope){return [this,ns,scope](const std::string& name)->Var*{std::string low=lower(name);
  if(scope)for(auto& level:scope->levels){auto it=level.find(low);if(it!=level.end())return it->second;}
  return global(low,ns);};}
 Expression expression(const std::string& text,const std::string& ns,Scope* scope,bool inList){return parseExpression(lower(text),resolver(ns,scope),inList);}
 void parseConstantsGlobals(std::vector<Line>& rest);
 void parseList(ListSection& ls,const std::vector<Line>& lines,const std::set<std::string>& whitelist);
 bool parseLine(ListSection& ls,Scope& scope,const Line& line,const std::string& key,const std::string& val,const std::string& raw,CommandList* explicitList,CommandList& target);
 void parseKey(const Section& s);
 void parseTextureOverride(const Section& s,ListSection& ls);
 float staticValue(const std::string& text,const std::string& ns,const std::string& where,bool& ok);
};

// ------------------------------------------------------------------ commands
inline void add(CommandPtr c,CommandList* explicitList,CommandList* sensible,CommandList& pre,CommandList& post){
 if(explicitList)explicitList->commands.push_back(c);else if(sensible)sensible->commands.push_back(c);else{pre.commands.push_back(c);post.commands.push_back(c);}}

struct Assign:Command {Kind kind()const override{return Kind::Assign;}Var* var=nullptr;int index=0;char comp='x';bool param=false;Expression value;std::string where;
 void run(RunState& s)override{float v=value(s);if(param){auto k=std::make_pair(index,comp);float b=s.e.params[k];s.e.params[k]=v;s.e.note(Event::Assign,where,std::string(1,comp)+std::to_string(index),b,v);return;}
  float b=var->value;var->value=v;s.e.note(Event::Assign,where,var->name,b,v);if((var->flags&VarPersist)&&v!=b)s.e.persistDirty=true;}};
struct ElsePlaceholder:Command {Kind kind()const override{return Kind::Else;}void run(RunState&)override{}};
struct If:Command {Kind kind()const override{return Kind::If;}Expression cond;CommandList truePre,truePost,falsePre,falsePost;bool preDone=false,postDone=false,elseIf=false,nested=false;std::string where;
 void run(RunState& s)override{bool yes=cond(s)!=0;if(s.e.trace)s.e.note(Event::Branch,where,line+(yes?" : true":" : false"),0,yes);
  CommandList& l=yes?(s.post?truePost:truePre):(s.post?falsePost:falsePre);s.e.runList(l,s,false);}};
struct RunList:Command {Kind kind()const override{return Kind::Run;}ListSection* target=nullptr;bool together=false;std::string where;
 void run(RunState& s)override{if(s.e.trace)s.e.note(Event::Run,where,target->name+(together?" (pre+post)":s.post?" (post)":" (pre)"));
  if(together){bool saved=s.post;s.post=false;s.e.runList(target->pre,s);s.post=true;s.e.runList(target->post,s);s.post=saved;}
  else s.e.runList(s.post?target->post:target->pre,s);}};
struct Skip:Command {std::string where;void run(RunState& s)override{s.e.note(Event::Skip,where,"handling = skip");if(s.result)s.result->skip=true;}};
struct Abort:Command {std::string where;void run(RunState& s)override{s.e.note(Event::Abort,where,"handling = abort");s.aborted=true;}};
struct CheckOverride:Command {std::string slot,where;bool together=false;void run(RunState& s)override{s.e.checkSlot(s,slot,together,where);}};
struct CustomShaderRun:Command {Kind kind()const override{return Kind::CustomShader;}std::string name,where;void run(RunState& s)override{s.e.note(Event::Unported,where,"run = "+name);if(s.result)s.result->customShaders.push_back(name);}};
struct Unported:Command {Kind kind()const override{return Kind::Unported;}std::string where,what;void run(RunState& s)override{s.e.note(Event::Unported,where,what);}};
// dst = [options] src. A slot gets the source as the draw sees it; a [Resource] keeps what the source held then.
struct Bind:Command {Kind kind()const override{return Kind::Bind;}std::string dst,src,options,where,ns;bool dstResource=false;
 Ref resolve(RunState& s){if(src=="null")return Ref{Ref::Null,0,{}};
  if(slotName(src)||src=="this"){std::string slot=src=="this"?s.thisSlot:src;auto it=s.bindings->find(slot);return it==s.bindings->end()?Ref{}:it->second;}
  auto c=s.e.customResources.find(src);if(c!=s.e.customResources.end())return c->second;return Ref::mod(src);}
 void run(RunState& s)override{Ref r=resolve(s);std::string to=dst=="this"?s.thisSlot:dst;
  if(dstResource){s.e.customResources[to]=r;if(s.e.trace)s.e.note(Event::Bind,where,to+" = "+src);return;}
  if(!s.draw&&s.bindings==&s.e.contextBindings)s.e.limitation(where,"在绘制调用之外设置了槽位（"+to+"）；Android 上没有可保持该绑定的 D3D 上下文");
  (*s.bindings)[to]=r;if(s.e.trace)s.e.note(Event::Bind,where,to+" = "+src);}};
struct DrawCmd:Command {Kind kind()const override{return Kind::Draw;}std::string type,where;std::vector<Expression> args;bool automatic=false,fromCaller=false;
 void run(RunState& s)override{IssuedDraw d;d.kind=type;d.where=where;d.automatic=automatic;d.fromCaller=fromCaller;for(auto& a:args)d.args.push_back(a(s));d.bindings=*s.bindings;
  if(s.e.trace){std::string detail=type+(automatic?" = auto":fromCaller?" = from_caller":"");for(size_t i=0;i<d.args.size();i++)detail+=(i?",":" = ")+std::to_string(d.args[i]);
  s.e.note(Event::Draw,where,detail);}if(s.result)s.result->draws.push_back(d);else s.e.limitation(where,type+" 位于绘制调用之外：逻辑核心仅记录，不进行绘制");}};

// ------------------------------------------------------------------ context
inline RunState::RunState(Engine& engine):e(engine){}
inline float RunState::keywordValue(const Keyword& k){std::string n=k.name;
 if(k.scope==Scope::Frame){if(n=="time")return float(e.nowSeconds);if(n=="frame_time")return e.frameTime;if(n=="fps")return e.fps;return float(e.frameNumber);}
 if(k.scope==Scope::Draw){if(!draw)return 0;auto it=draw->call.find(n);if(it!=draw->call.end())return it->second;
  e.limitation(n,"适配层未提供绘制调用的 "+n+"，按 0 读取");return 0;}
 auto it=e.host.find(n);if(it!=e.host.end())return it->second;e.limitation(n,"宿主值 "+n+" 不可用，按 0 读取");return 0;}
inline float RunState::slotFilter(const std::string& slot){auto it=bindings->find(slot);if(it==bindings->end()||it->second.kind==Ref::None||it->second.kind==Ref::Null)return -0.0f;
 if(it->second.kind==Ref::Mod){e.limitation(slot,"对存放 MOD 资源的槽位使用贴图过滤：该资源没有可追踪的 hash，按 0 读取（不匹配）");return 0;}
 auto m=e.matches(it->second.hash,draw);if(m.empty())return 0;for(auto r=m.rbegin();r!=m.rend();++r)if((*r)->filterIndex!=FLT_MAX)return (*r)->filterIndex;return 1;}
inline float RunState::param(int i,char c){auto it=e.params.find({i,c});return it==e.params.end()?0:it->second;}
inline float RunState::randomValue(float max){if(max==0)return 0;float sign=max<0?-1.0f:1.0f;float u=e.uniform?e.uniform():0.5f;
 return float(int(u*16777216.0f)&0x00ffffff)/16777216.0f*std::fabs(max)*sign;}

inline void Engine::checkSlot(RunState& s,const std::string& slot,bool together,const std::string& where){
 std::string checked=slot=="this"?s.thisSlot:slot;auto it=s.bindings->find(checked);if(it==s.bindings->end()||it->second.kind!=Ref::Game){
  if(it!=s.bindings->end()&&it->second.kind==Ref::Mod)diag.add(Diagnostic::Notice,where,"checktextureoverride = "+checked+"：该槽位存放的是 MOD 资源，没有可追踪的 hash，不会匹配");
  return;}
 auto found=matches(it->second.hash,s.draw);std::string saved=s.thisSlot;s.thisSlot=checked;
 for(auto* t:found){if(trace)note(Event::Match,where,t->name+(together?" (pre+post)":s.post?" (post)":" (pre)"));if(!s.post&&s.result&&!together)s.result->matched.push_back(t->name);}
 if(together){bool p=s.post;s.post=false;for(auto* t:found)runList(t->list->pre,s);s.post=true;for(auto* t:found)runList(t->list->post,s);s.post=p;}
 else for(auto* t:found)runList(s.post?t->list->post:t->list->pre,s);
 s.thisSlot=saved;}

// ------------------------------------------------------------------ parsing
inline float Engine::staticValue(const std::string& text,const std::string& ns,const std::string& where,bool& ok){
 float v=0;char* end=nullptr;std::string t=trim(text);v=std::strtof(t.c_str(),&end);if(!t.empty()&&end&&*end==0){ok=true;return v;}
 try{auto e=expression(t,ns,nullptr,false);StaticContext c;c.time=float(nowSeconds);c.frameNumber=float(frameNumber);c.params=[this](int i,char k){return params[{i,k}];};
  if(staticEvaluate(e,c,v)){ok=true;return v;}warning(where,"无法静态求值："+text);}
 catch(const SyntaxError& ex){warning(where,std::string("无法解析表达式 \"")+text+"\"："+ex.what());}
 ok=false;return 0;}

inline void Engine::parseConstantsGlobals(std::vector<Line>& rest){
 const Section* s=ini.find("Constants");if(!s)return;
 for(auto& l:s->lines){std::string name=lower(l.hasEquals?l.key:l.raw);unsigned flags=0;
  for(;;){if(startsWith(name,"global ")){flags|=VarGlobal;name=trim(name.substr(7));}else if(startsWith(name,"persist ")){flags|=VarPersist;name=trim(name.substr(8));}
   else if(startsWith(name,"locked ")){flags|=VarLocked;name=trim(name.substr(7));}else break;}
  if(!(flags&VarGlobal)){rest.push_back(l);continue;}
  if(!validVariableName(name)){warning("[Constants] @ "+l.ns,"全局变量名无效：\""+name+"\"");continue;}
  if(!l.ns.empty())name=namespacedVar(name,l.ns);
  float v=0;if(!l.val.empty()){bool ok;v=staticValue(lower(l.val),l.ns,"[Constants] "+l.raw,ok);if(!ok)continue;}   // upstream skips the declaration
  if(globals.count(name)){warning("[Constants] @ "+l.ns,"重复声明："+name);continue;}
  Var var;var.name=name;var.value=v;var.flags=flags;globals[name]=var;initial[name]=v;}}

// The keys of a [TextureOverride] that are its own settings, not commands (IniHandler.cpp TextureOverrideIniKeys).
inline const std::set<std::string>& overrideKeys(){static const std::set<std::string> k={"hash","format","width","height","width_multiply","height_multiply",
 "override_byte_stride","override_vertex_count","uav_byte_stride","iteration","filter_index","expand_region_copy","deny_cpu_read","match_priority",
 "match_type","match_usage","match_bind_flags","match_cpu_access_flags","match_misc_flags","match_byte_width","match_stride","match_mips","match_format",
 "match_width","match_height","match_depth","match_array","match_msaa","match_msaa_quality","match_first_vertex","match_first_index","match_first_instance",
 "match_vertex_count","match_index_count","match_instance_count"};return k;}

inline void Engine::parseList(ListSection& ls,const std::vector<Line>& lines,const std::set<std::string>& whitelist){
 Scope scope;scope.levels.emplace_front();ls.pre.post=false;ls.post.post=true;ls.pre.section=ls.post.section=ls.name;
 for(auto& line:lines){std::string key=lower(line.key),val=lower(line.val),raw=lower(line.raw);
  if(whitelist.count(key))continue;
  CommandList* target=&ls.pre;CommandList* explicitList=nullptr;std::string k=key;
  if(startsWith(key,"post ")){k=trim(key.substr(5));target=&ls.post;explicitList=&ls.post;}else if(startsWith(key,"pre ")){k=trim(key.substr(4));explicitList=&ls.pre;}
  try{if(!parseLine(ls,scope,line,k,val,raw,explicitList,*target))warning("["+ls.name+"] @ "+line.ns,"无法识别的条目："+line.raw);}
  catch(const SyntaxError& e){warning("["+ls.name+"] @ "+line.ns,"语法错误：\""+line.raw+"\"："+e.what());}}
 if(scope.levels.size()!=1)warning("["+ls.name+"]","作用域不平衡");}

inline bool Engine::parseLine(ListSection& ls,Scope& scope,const Line& line,const std::string& key,const std::string& val,const std::string& raw,CommandList* explicitList,CommandList& target){
 const std::string& ns=line.ns;std::string where="["+ls.name+"] "+line.raw;
 // Variable assignment (and local declarations); no "pre"/"post" check applies here, the target list does.
 {std::string l=key.empty()?raw:key;bool local=startsWith(l,"local");
  if(local||(!l.empty()&&l[0]=='$')){
   std::string name=local?trim(l.substr(5)):trim(l);
   if(local){if(l.size()<6||(l[5]!=' '&&l[5]!='\t'))return false;
    if(!validVariableName(name)){warning(where,"局部变量名无效：\""+name+"\"");return true;}
    for(auto& level:scope.levels)if(level.count(name)){warning(where,"局部变量重复声明：\""+name+"\"");return true;}
    if(global(name,ns))diag.add(Diagnostic::Notice,where,"局部变量 \""+name+"\" 遮蔽了同名全局变量");
    ls.locals.emplace_front();ls.locals.front().name=name;scope.levels.front()[name]=&ls.locals.front();if(val.empty())return true;}
   if(!name.empty()&&name.back()==']')return false;   // pools are not ported
   Var* v=resolver(ns,&scope)(name);if(!v)return false;
   if(v->flags&VarLocked){warning(where,"对锁定变量赋值："+name);return true;}
   auto a=std::make_shared<Assign>();a->var=v;a->where=where;a->line=line.raw;a->value=expression(val,ns,&scope,true);target.commands.push_back(a);return true;}}
 // Flow control: if / elif / else if / else / endif (never with an explicit pre/post).
 if(!explicitList){
  auto openIf=[&](size_t prefix,bool elseIf){auto c=std::make_shared<If>();c->elseIf=elseIf;c->where="["+ls.name+"]";c->line=trim(raw);
   size_t p=raw.find_first_not_of(" \t",prefix);c->cond=expression(p==std::string::npos?"":raw.substr(p),ns,&scope,true);return c;};
  if(startsWith(raw,"if ")){auto c=openIf(3,false);scope.levels.emplace_front();add(c,nullptr,nullptr,ls.pre,ls.post);return true;}
  if(startsWith(raw,"elif ")||startsWith(raw,"else if ")){auto c=openIf(startsWith(raw,"elif ")?5:8,true);if(scope.levels.size()>1)scope.levels.front().clear();
   add(std::make_shared<ElsePlaceholder>(),nullptr,nullptr,ls.pre,ls.post);add(c,nullptr,nullptr,ls.pre,ls.post);return true;}
  if(raw=="else"){if(scope.levels.size()>1)scope.levels.front().clear();add(std::make_shared<ElsePlaceholder>(),nullptr,nullptr,ls.pre,ls.post);return true;}
  if(raw=="endif"){
   std::function<bool(CommandList&,bool,bool)> close=[&](CommandList& list,bool post,bool nested)->bool{size_t elsePos=list.commands.size();
    for(size_t k=list.commands.size();k-->0;){if(list.commands[k]->kind()==Kind::Else)elsePos=k;
     auto* c=list.commands[k]->kind()==Kind::If?static_cast<If*>(list.commands[k].get()):nullptr;if(!c||(post?c->postDone:c->preDone))continue;
     CommandList& t=post?c->truePost:c->truePre;CommandList& f=post?c->falsePost:c->falsePre;t.post=f.post=post;t.section=f.section=ls.name;
     t.commands.assign(list.commands.begin()+long(k)+1,list.commands.begin()+long(elsePos));
     if(elsePos!=list.commands.size())f.commands.assign(list.commands.begin()+long(elsePos)+1,list.commands.end());
     list.commands.erase(list.commands.begin()+long(k)+1,list.commands.end());(post?c->postDone:c->preDone)=true;c->nested=nested;
     return c->elseIf?close(list,post,true):true;}
    warning(where,"语句 \"endif\" 缺少对应的 \"if\"");return false;};
   if(close(ls.pre,false,false)&&close(ls.post,true,false)&&scope.levels.size()>1)scope.levels.pop_front();
   return true;}}
 // General commands.
 if(key=="checktextureoverride"){auto c=std::make_shared<CheckOverride>();c->slot=val;c->where=where;c->line=line.raw;c->together=explicitList!=nullptr;
  if(!slotName(val)&&val!="this")return false;
  add(c,explicitList,nullptr,ls.pre,ls.post);return true;}
 if(key=="run"){
  if(startsWith(val,"customshader")||startsWith(val,"builtincustomshader")){auto c=std::make_shared<CustomShaderRun>();c->name=val;c->where=where;c->line=line.raw;
   limitation(where,"Android 上不运行自定义着色器（仅记录）");add(c,explicitList,nullptr,ls.pre,ls.post);return true;}
  if(startsWith(val,"commandlist")||startsWith(val,"builtincommandlist")){ListSection* t=listSection(val,ns);
   if(!t&&startsWith(val,"builtincommandlist")){auto c=std::make_shared<Unported>();c->where=where;c->what=line.raw;c->line=line.raw;   // defined inside 3DMigoto, not in an INI
    limitation(where,"内置命令列表作用于 D3D 状态，不可用");add(c,explicitList,nullptr,ls.pre,ls.post);return true;}
   if(!t)return false;
   auto c=std::make_shared<RunList>();c->target=t;c->together=explicitList!=nullptr;c->where=where;c->line=line.raw;add(c,explicitList,nullptr,ls.pre,ls.post);return true;}}
 if(key=="handling"){if(val=="skip"){auto c=std::make_shared<Skip>();c->where=where;c->line=line.raw;add(c,explicitList,&ls.pre,ls.pre,ls.post);return true;}
  if(val=="abort"){auto c=std::make_shared<Abort>();c->where=where;c->line=line.raw;add(c,explicitList,nullptr,ls.pre,ls.post);return true;}}
 static const std::set<std::string> unported={"preset","exclude_preset","reset_per_frame_limits","clear","analyse_options","dump","special","store"};
 if(unported.count(key)||startsWith(key,"commandlist")){auto c=std::make_shared<Unported>();c->where=where;c->what=line.raw;c->line=line.raw;
  limitation(where,key+" 未移植（属于 GPU 或帧分析操作）");add(c,explicitList,key=="preset"||key=="exclude_preset"?nullptr:&ls.pre,ls.pre,ls.post);return true;}
 static const std::map<std::string,size_t> drawKinds={{"draw",2},{"drawauto",0},{"drawindexed",3},{"drawindexedinstanced",5},{"drawinstanced",4},{"dispatch",3}};
 auto dk=drawKinds.find(key);
 if(dk!=drawKinds.end()){auto c=std::make_shared<DrawCmd>();c->type=key;c->where=where;c->line=line.raw;
  if(key=="draw"&&val=="from_caller")c->fromCaller=true;else if((key=="draw"||key=="drawindexed"||key=="drawindexedinstanced")&&val=="auto")c->automatic=true;
  else if(dk->second){size_t start=0;for(;;){size_t comma=val.find(',',start);c->args.push_back(expression(val.substr(start,comma==std::string::npos?std::string::npos:comma-start),ns,&scope,true));
    if(comma==std::string::npos)break;
    start=comma+1;}
   if(c->args.size()!=dk->second)return false;}
  add(c,explicitList,&ls.pre,ls.pre,ls.post);return true;}
 if(key=="drawindexedinstancedindirect"||key=="drawinstancedindirect"||key=="dispatchindirect"){auto c=std::make_shared<Unported>();c->where=where;c->what=line.raw;c->line=line.raw;
  limitation(where,"间接绘制需要读取 GPU 缓冲，不可用");add(c,explicitList,&ls.pre,ls.pre,ls.post);return true;}
 // Ini params: x = ..., y3 = ...
 {int index;char comp;if(iniParam(key,index,comp)){auto a=std::make_shared<Assign>();a->param=true;a->index=index;a->comp=comp;a->where=where;a->line=line.raw;
   a->value=expression(val,ns,&scope,true);target.commands.push_back(a);return true;}}
 // Resource copy directive: dst = [options] src.
 {bool dstResource=istarts(key,"resource");if(dstResource||slotName(key)||key=="this"){
   static const std::set<std::string> options={"ref","reference","copy","copy_desc","unless_null","raw","stereo","mono","stereo2mono","set_viewport","no_view_cache","resolve_msaa"};
   std::string rest=val,opts;for(;;){size_t sp=rest.find(' ');std::string w=rest.substr(0,sp);if(sp==std::string::npos||!options.count(w))break;opts+=(opts.empty()?"":" ")+w;rest=trim(rest.substr(sp+1));}
   auto c=std::make_shared<Bind>();c->where=where;c->line=line.raw;c->options=opts;c->ns=ns;
   if(dstResource){c->dst=resourceKey(key,ns);c->dstResource=true;if(c->dst.empty())return false;}else c->dst=key;
   if(rest=="null"||slotName(rest)||rest=="this")c->src=rest;
   else if(istarts(rest,"resource")){c->src=resourceKey(rest,ns);if(c->src.empty())return false;}
   else return false;
   target.commands.push_back(c);return true;}}
 return false;}

inline void Engine::parseTextureOverride(const Section& s,ListSection& ls){
 std::map<std::string,std::string> kv;for(auto& l:s.lines)if(l.hasEquals&&overrideKeys().count(lower(l.key))){std::string k=lower(l.key);
  if(kv.count(k))warning("["+s.name+"]","非命令列表键重复："+k);else kv[k]=lower(l.val);}
 if(!kv.count("hash")){for(auto& f:kv)if(startsWith(f.first,"match_")&&f.first.find("_first_")==std::string::npos&&f.first.find("_count")==std::string::npos){
   limitation("["+s.name+"]","模糊资源匹配（无 hash）需要 D3D 资源描述，未移植");return;}
  warning("["+s.name+"]","TextureOverride 缺少 hash");return;}
 TextureOverride t;t.list=&ls;t.name=s.name;t.hash=uint32_t(std::strtoul(kv["hash"].c_str(),nullptr,16));
 if(kv.count("match_priority"))t.priority=std::atoi(kv["match_priority"].c_str());
 if(kv.count("filter_index")){bool ok;float f=staticValue(kv["filter_index"],s.ns,"["+s.name+"] filter_index",ok);if(ok)t.filterIndex=f;}
 for(const char* m:{"first_vertex","first_index","first_instance","vertex_count","index_count","instance_count"}){auto it=kv.find(std::string("match_")+m);if(it==kv.end())continue;
  FuzzyNumber f;std::string v=it->second;size_t p=0;
  if(startsWith(v,"<=")){f.op=FuzzyNumber::Le;p=2;}else if(startsWith(v,">=")){f.op=FuzzyNumber::Ge;p=2;}else if(startsWith(v,"=")){f.op=FuzzyNumber::Eq;p=1;}
  else if(startsWith(v,"!")){f.op=FuzzyNumber::Ne;p=1;}else if(startsWith(v,"<")){f.op=FuzzyNumber::Lt;p=1;}else if(startsWith(v,">")){f.op=FuzzyNumber::Gt;p=1;}else f.op=FuzzyNumber::Eq;
  v=trim(v.substr(p));char* end=nullptr;unsigned long n=std::strtoul(v.c_str(),&end,10);
  if(v.empty()||*end){limitation("["+s.name+"]","match_"+std::string(m)+" = "+it->second+"：字段表达式未移植");continue;}
  f.value=uint32_t(n);t.match[m]=f;t.drawMatch=true;}
 overrides[t.hash].push_back(t);}

inline void Engine::parseKey(const Section& s){
 KeySection k;k.name=s.name;k.ns=s.ns;std::map<std::string,std::string> first;std::vector<std::pair<std::string,std::string>> vars;
 for(auto& l:s.lines){if(!l.hasEquals)continue;std::string key=lower(l.key);
  if(key=="key")k.keys.push_back(l.val);else if(key=="back")k.backs.push_back(l.val);
  else{int i;char c;bool var=!key.empty()&&key[0]=='$',param=iniParam(key,i,c);
   if(var||param){bool seen=false;for(auto& v:vars)seen=seen||v.first==key;if(seen)warning("["+s.name+"]","键重复："+key);else vars.push_back({key,l.val});}   // the first one counts
   else if(!first.count(key))first[key]=l.val;}}
 if(k.keys.empty()&&k.backs.empty()){warning("["+s.name+"]","缺少 Key=");return;}
 k.type=first.count("type")?lower(first["type"]):"activate";
 if(k.type!="activate"&&k.type!="hold"&&k.type!="toggle"&&k.type!="cycle"){warning("["+s.name+"]","未知的按键类型 "+k.type);k.type="activate";}
 k.delay=first.count("delay")?std::atoi(first["delay"].c_str()):0;k.releaseDelay=first.count("release_delay")?std::atoi(first["release_delay"].c_str()):0;
 if(k.delay||k.releaseDelay)limitation("["+s.name+"]","按键 delay / release_delay 未移植");
 auto parseRun=[&](Preset& p,const std::string& text){if(text.empty())return;std::string v=lower(trim(text));ListSection* t=listSection(v,k.ns);
  if(!t){warning("["+s.name+"]","无效的 run=\""+text+"\"");return;}
  p.run=std::make_unique<ListSection>();p.run->name=s.name+" run";auto c=std::make_shared<RunList>();c->target=t;c->where="["+s.name+"] run = "+v;
  add(c,nullptr,nullptr,p.run->pre,p.run->post);p.run->post.post=true;};
 auto parseCondition=[&](Preset& p,const std::string& text){if(text.empty())return;
  try{p.condition=expression(text,k.ns,nullptr,false);p.conditional=true;}catch(const SyntaxError& e){warning("["+s.name+"]","无效的 condition = \""+text+"\"："+e.what());}};
 auto lookupVar=[&](const std::string& name,const std::string& ns)->Var*{Var* v=global(lower(name),ns);if(!v||(v->flags&VarLocked)){warning("["+s.name+"]","未知或已锁定的变量 "+name);return nullptr;}return v;};
 auto get=[&](const char* key){auto it=first.find(key);return it==first.end()?std::string():it->second;};
 if(k.type!="cycle"){Preset p;
  for(auto& v:vars){bool ok;float f=staticValue(lower(v.second),k.ns,"["+s.name+"] "+v.first,ok);if(!ok)continue;int i;char c;
   if(iniParam(v.first,i,c))p.params[{i,c}]=f;else if(Var* var=lookupVar(v.first,k.ns))p.vars[var]=f;}
  p.transition=std::atoi(get("transition").c_str());p.releaseTransition=std::atoi(get("release_transition").c_str());
  p.cosine=lower(get("transition_type"))=="cosine";p.releaseCosine=lower(get("release_transition_type"))=="cosine";
  parseCondition(p,get("condition"));parseRun(p,get("run"));k.presets.push_back(std::move(p));}
 else{k.wrap=get("wrap").empty()?true:(lower(get("wrap"))!="0"&&lower(get("wrap"))!="false"&&lower(get("wrap"))!="no"&&lower(get("wrap"))!="off");
  k.smart=get("smart").empty()?true:(lower(get("smart"))!="0"&&lower(get("smart"))!="false"&&lower(get("smart"))!="no"&&lower(get("smart"))!="off");
  auto split=[](const std::string& t){std::vector<std::string> out;if(t.empty())return out;size_t start=0;for(;;){size_t c=t.find(',',start);out.push_back(trim(t.substr(start,c==std::string::npos?std::string::npos:c-start)," "));if(c==std::string::npos)break;start=c+1;}return out;};
  std::vector<std::pair<std::string,std::vector<std::string>>> columns;for(auto& v:vars)columns.push_back({v.first,split(v.second)});
  auto tr=split(get("transition")),rtr=split(get("release_transition")),tt=split(get("transition_type")),rtt=split(get("release_transition_type")),cond=split(get("condition")),run=split(get("run"));
  size_t n=0;for(auto& c:columns)n=std::max(n,c.second.size());for(auto* c:{&tr,&rtr,&tt,&rtt,&cond,&run})n=std::max(n,c->size());
  auto at=[](const std::vector<std::string>& c,size_t i){return i<c.size()?c[i]:std::string();};
  for(size_t i=0;i<n;i++){Preset p;
   for(auto& c:columns){std::string cell=at(c.second,i);if(cell.empty())continue;char* end=nullptr;float f=std::strtof(cell.c_str(),&end);if(end==cell.c_str())continue;   // sscanf("%f"): a blank entry leaves it alone
    int pi;char pc;if(iniParam(c.first,pi,pc))p.params[{pi,pc}]=f;else if(Var* var=lookupVar(c.first,k.ns))p.vars[var]=f;}
   p.transition=std::atoi(at(tr,i).c_str());p.releaseTransition=std::atoi(at(rtr,i).c_str());p.cosine=lower(at(tt,i))=="cosine";p.releaseCosine=lower(at(rtt,i))=="cosine";
   parseCondition(p,at(cond,i));parseRun(p,at(run,i));k.presets.push_back(std::move(p));}}
 keys.push_back(std::move(k));}

inline void Engine::load(const std::vector<IniFile>& files){
 ini.diag=&diag;
 for(auto& f:files)ini.add(f,[this](const std::string& cond,const std::string& ns){bool ok;float v=staticValue(lower(cond),ns,"include 条件",ok);return ok&&v!=0;});
 std::vector<Line> constants;parseConstantsGlobals(constants);
 // Every command list section exists before any is parsed, so a run may name one defined later.
 for(auto& kv:ini.sections){const Section& s=kv.second;if(!isCommandListSection(s.name)||istarts(s.name,"ShaderOverride")||istarts(s.name,"ShaderRegex")||istarts(s.name,"CustomShader")||istarts(s.name,"BuiltInCustomShader")||istarts(s.name,"Clear"))continue;
  auto ls=std::make_unique<ListSection>();ls->name=s.name;ls->ns=s.ns;lists[kv.first]=std::move(ls);}
 for(auto& kv:ini.sections){const Section& s=kv.second;
  if(istarts(s.name,"Resource")){ResourceDef r;r.name=s.name;r.ns=s.ns;r.path=s.path;const char* p=sectionPrefix(s.name);
   r.shortName=s.ns.empty()?s.name:std::string(p)+s.name.substr(std::string(p).size()+s.ns.size()+2);
   for(auto& l:s.lines)if(l.hasEquals&&!r.values.count(lower(l.key)))r.values[lower(l.key)]=l.val;
   resources[kv.first]=r;}
  else if(istarts(s.name,"ShaderOverride")||istarts(s.name,"ShaderRegex"))limitation("["+s.name+"]","着色器覆盖需要游戏的 D3D 着色器，Android 上不运行");
  else if(istarts(s.name,"Pool")||istarts(s.name,"Preset"))limitation("["+s.name+"]","未移植");}
 for(auto& kv:lists){ListSection& ls=*kv.second;const Section* s=ini.find(ls.name);
  if(lower(ls.name)=="constants"){parseList(ls,constants,{});continue;}
  bool texture=istarts(ls.name,"TextureOverride");parseList(ls,s->lines,texture?overrideKeys():std::set<std::string>());
  if(texture)parseTextureOverride(*s,ls);}
 for(auto& kv:overrides)std::stable_sort(kv.second.begin(),kv.second.end(),[](const TextureOverride& a,const TextureOverride& b){return a.priority!=b.priority?a.priority<b.priority:a.name<b.name;});
 for(auto* s:ini.withPrefix("Key"))parseKey(*s);}

inline void Engine::start(const std::map<std::string,float>& persisted){
 for(auto& kv:globals)kv.second.value=initial[kv.first];
 varTransitions.clear();paramTransitions.clear();globalSaves.clear();globalParamSaves.clear();
 auto it=lists.find("constants");RunState pre(*this);pre.bindings=&contextBindings;if(it!=lists.end())runList(it->second->pre,pre);
 for(auto& p:persisted){auto g=globals.find(p.first);if(g!=globals.end()&&(g->second.flags&VarPersist)){note(Event::Assign,"persisted",p.first,g->second.value,p.second);g->second.value=p.second;}}
 persistDirty=false;   // upstream: [Constants] pre does not make the user config dirty
 RunState post(*this);post.post=true;post.bindings=&contextBindings;if(it!=lists.end())runList(it->second->post,post);}

inline void Engine::activate(KeySection& k,Preset& p,bool hold){
 if(p.conditional){RunState s(*this);s.bindings=&contextBindings;if(p.condition(s)==0){note(Event::Key,k.name,"condition not met");return;}}
 note(Event::Key,k.name,hold?"activate (hold)":"activate");
 if(hold){p.active=true;for(auto& v:p.vars){float val=currentOrTarget(v.first);p.saved[v.first]=val;auto& g=globalSaves[v.first];if(!g.refcount)g.save=val;g.refcount++;}
  for(auto& v:p.params){float val=currentOrTarget(v.first);p.savedParams[v.first]=val;auto& g=globalParamSaves[v.first];if(!g.refcount)g.save=val;g.refcount++;}}
 schedule(p.vars,p.params,p.transition,p.cosine);
 if(p.run){RunState s(*this);s.bindings=&contextBindings;runList(p.run->pre,s);if(!hold)pendingPost.push_back(&p.run->post);}}

inline void Engine::deactivate(Preset& p){
 if(!p.active)return;
 p.active=false;note(Event::Key,"","deactivate");
 for(auto it=globalSaves.begin();it!=globalSaves.end();){if(p.vars.count(it->first)){if(--it->second.refcount==0){p.saved[it->first]=it->second.save;it=globalSaves.erase(it);continue;}}++it;}
 for(auto it=globalParamSaves.begin();it!=globalParamSaves.end();){if(p.params.count(it->first)){if(--it->second.refcount==0){p.savedParams[it->first]=it->second.save;it=globalParamSaves.erase(it);continue;}}++it;}
 schedule(p.saved,p.savedParams,p.releaseTransition,p.releaseCosine);
 if(p.run){RunState s(*this);s.post=true;s.bindings=&contextBindings;runList(p.run->post,s);}}

inline void Engine::frame(double seconds){
 frameTime=float(seconds-nowSeconds);nowSeconds=seconds;if(frameTime>0)fps=1.0f/frameTime;
 auto present=lists.find("present");
 {RunState s(*this);s.bindings=&contextBindings;if(present!=lists.end())runList(present->second->pre,s);}
 auto events=queued;queued.clear();
 for(auto& in:events){KeySection& k=keys[in.key];if(k.presets.empty())continue;
  if(k.type=="cycle"){
   auto matchesCurrent=[&](Preset& p){for(auto& v:p.vars)if(v.second!=currentOrTarget(v.first))return false;for(auto& v:p.params)if(v.second!=currentOrTarget(v.first))return false;return true;};
   if(in.kind==Input::Up)continue;
   if(in.kind==Input::Select){if(in.index<0||size_t(in.index)>=k.presets.size())continue;k.current=in.index;
    note(Event::Key,k.name,"select -> "+std::to_string(k.current));activate(k,k.presets[size_t(k.current)],false);continue;}
   if(k.smart&&!(k.current>=0&&size_t(k.current)<k.presets.size()&&matchesCurrent(k.presets[size_t(k.current)])))
    for(size_t i=0;i<k.presets.size();i++)if(int(i)!=k.current&&matchesCurrent(k.presets[i])){k.current=int(i);break;}
   int n=int(k.presets.size());
   if(in.kind==Input::Down){if(k.current==-1)k.current=0;else if(k.wrap)k.current=(k.current+1)%n;else if(k.current<n-1)k.current++;else continue;}
   else{if(k.current==-1)k.current=n-1;else if(k.wrap)k.current=(k.current?k.current:n)-1;else if(k.current>0)k.current--;else continue;}
   note(Event::Key,k.name,"cycle -> "+std::to_string(k.current));activate(k,k.presets[size_t(k.current)],false);continue;}
  Preset& p=k.presets[0];
  if(in.kind==Input::Back)continue;
  if(in.kind==Input::Select)in.kind=Input::Down;   // one preset: selecting it is pressing it
  if(in.kind==Input::Down){
   if(k.type=="toggle"){if(p.conditional){RunState s(*this);s.bindings=&contextBindings;if(p.condition(s)==0){note(Event::Key,k.name,"toggle: condition not met");continue;}}
    if(p.active)deactivate(p);else activate(k,p,true);}
   else if(k.type=="hold"&&!p.active)activate(k,p,true);
   else activate(k,p,false);}
  else if(k.type=="hold")deactivate(p);}
 updateTransitions();
 frameNumber++;   // G->frame_no++ at the end of RunFrameActions, before the game's Present
 {RunState s(*this);s.post=true;s.bindings=&contextBindings;if(present!=lists.end())runList(present->second->post,s);}}

inline DrawResult Engine::draw(const DrawInfo& d){
 DrawResult r;r.bindings=d.bindings;
 // BeforeDraw: the host lists' checktextureoverride lines, pre. One state for the whole draw (the host's lists are
 // modelled as one list; upstream gives each shader stage's list its own state, which matters only for abort).
 {RunState s(*this);s.draw=&d;s.result=&r;s.bindings=&r.bindings;for(auto& slot:d.checks)checkSlot(s,slot,false,"draw "+slot);}
 // AfterDraw: the post lists, looked up again by what each slot holds now.
 {RunState s(*this);s.post=true;s.draw=&d;s.result=&r;auto after=r.bindings;s.bindings=&after;for(auto& slot:d.checks)checkSlot(s,slot,false,"draw "+slot+" (post)");}
 return r;}

}  // namespace migoto
