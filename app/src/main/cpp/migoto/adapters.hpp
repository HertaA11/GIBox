// SPDX-License-Identifier: GPL-3.0-only
// GIBox 3DMigoto logic core: the two adapters over one engine.
//  - ImportScan (converter side): what can be known without running the mod: every [Resource] a command list can
//    bind (so meshes and textures can be prepared ahead), and, per variable, whether its values are a fixed finite
//    set. Only a variable every write of which is a constant (and no transition) is finite; a name, a file name
//    pattern or a count of draws never makes one.
//  - Session (game side): runs the frame's draws and Present through the engine and reports, per draw, whether
//    what it selects (skip, issued draws, bound resources) changed since the previous frame, so the renderer is
//    asked to switch only then, never rebuilt per frame.
// GPLv3 because it builds on the port in engine.hpp; it contains no upstream code of its own.
#pragma once
#include "engine.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <sstream>

namespace migoto {

inline std::string describe(const Engine& e,const Ref& r){
 switch(r.kind){case Ref::None:return "-";case Ref::Null:return "null";
  case Ref::Game:{char b[16];std::snprintf(b,sizeof b,"game:%08x",r.hash);return b;}
  case Ref::Mod:{auto it=e.resources.find(r.resource);return it==e.resources.end()?r.resource:it->second.shortName;}}
 return "?";}

// What a renderer has to load for a ref: the file and its layout. Sections that name the same file the same way
// (GIMI animation frames often repeat one texture) are one resource to the renderer.
inline std::string identity(const Engine& e,const Ref& r){if(r.kind!=Ref::Mod)return describe(e,r);auto it=e.resources.find(r.resource);if(it==e.resources.end())return r.resource;
 auto get=[&](const char* k){auto v=it->second.values.find(k);return v==it->second.values.end()?std::string():v->second;};std::string file=get("filename");
 if(file.empty())return it->second.shortName;
 std::replace(file.begin(),file.end(),'\\','/');
 return lower(it->second.path)+"|"+lower(file)+"|"+lower(get("type"))+"|"+get("stride")+"|"+lower(get("format"));}

// What the logic can do, without running it. Every variable that what is drawn depends on (an if around a draw or
// a binding, a draw argument, and transitively whatever its writes read and the ifs those writes sit in) is a draw
// variable; it is finite when its values are a fixed set: its initial value, constants, key values without a
// transition, and expressions of finite variables that stop growing (16 rounds, 256 values). A counter, a time, a
// random value, an ini param or a texture filter is not finite. Anything unsure stays not finite, with the reason.
struct ImportScan {
 struct Binding {std::string slot,resource;};
 struct VarInfo {std::string name;bool finite=true,readByDraws=false;std::set<float> values;std::vector<std::string> why;};
 std::vector<Binding> bindings;          // every slot <- [Resource] a command list can execute, all branches, each once
 std::set<std::string> resources;        // their section keys
 std::map<std::string,VarInfo> vars;     // by name; a local is "<section> local $name"
 std::vector<std::string> customShaders,unported,external;   // external: a [Resource] that takes a game resource (ref ps-t0)
 std::vector<std::string> reasons;       // why the draw variables are not all finite
 std::vector<std::string> inputs;        // the part of reasons that is outside input (clock, random, filters, transitions, game resources), not a counter
 bool finiteStates=false;uint64_t states=1;   // true: the draw variables are all finite and their product is small

 static bool constant(const Node& n){if(n.kind==Node::Value)return true;if(n.kind==Node::Unary)return n.op!=Op::Random&&constant(*n.rhs);
  if(n.kind==Node::Binary)return constant(*n.lhs)&&constant(*n.rhs);
  return false;}
 static void reads(const Node& n,std::set<Var*>& out){if(n.kind==Node::Variable)out.insert(n.var);if(n.lhs)reads(*n.lhs,out);if(n.rhs)reads(*n.rhs,out);}
 // What an expression reads that is not a variable: a keyword, an ini param, a texture filter, random().
 static std::string outside(const Node& n){if(n.kind==Node::Key)return n.key->name;if(n.kind==Node::Param)return std::string(1,n.comp)+std::to_string(n.index);if(n.kind==Node::Slot)return "贴图过滤 "+n.slot;
  if(n.kind==Node::Unary&&n.op==Op::Random)return "random()";
  for(auto* c:{n.lhs.get(),n.rhs.get()})if(c){auto o=outside(*c);if(!o.empty())return o;}
  return "";}

 struct Write {Var* var;const Expression* value=nullptr;float keyValue=0;bool transition=false,inDraw=false;std::set<Var*> control;std::string where;};
 std::vector<Write> writes;std::map<const Command*,size_t> written;std::set<Var*> seeds;std::map<Var*,std::string> names;
 std::set<Var*> drawVars;   // the seeds (what draws read) and everything that decides them
 // Stands in the control of a write under a condition that reads something outside the mod (a keyword, random(), a
 // texture filter): such a write depends on more than the variables.
 Var outsideCondition;std::string outsideText;
 // A list is walked again only when it is reached under variables it was not yet reached under (else-if chains share
 // their branches between pre and post, and a walk of every path is exponential); its writes then gain that control.
 std::map<std::pair<const CommandList*,bool>,std::set<Var*>> visited;std::set<std::pair<std::string,std::string>> bound;

 void walk(Engine& e,CommandList& l,bool draws,std::vector<std::set<Var*>>& control,const std::string& where,int depth){
  // Each else if is one level deeper; past this, what the list does is not known, and the scan says so.
  if(depth>1024){if(reasons.empty()||reasons.back().find("嵌套过深")==std::string::npos)reasons.push_back(where+"：嵌套过深，无法分析");return;}
  std::set<Var*> ctl;for(auto& c:control)ctl.insert(c.begin(),c.end());
  bool first=true;auto v=visited.find({&l,draws});
  if(v!=visited.end()){if(std::includes(v->second.begin(),v->second.end(),ctl.begin(),ctl.end()))return;v->second.insert(ctl.begin(),ctl.end());ctl=v->second;first=false;}
  else visited[{&l,draws}]=ctl;
  for(auto& c:l.commands){Command* cmd=c.get();
   if(auto* a=cmd->kind()==Kind::Assign?static_cast<Assign*>(cmd):nullptr){if(a->param)continue;
    auto at=written.find(cmd);if(at!=written.end()){writes[at->second].control.insert(ctl.begin(),ctl.end());writes[at->second].inDraw|=draws;continue;}
    Write w;w.var=a->var;w.value=&a->value;w.control=ctl;w.inDraw=draws;w.where=where+"："+a->line;written[cmd]=writes.size();writes.push_back(w);}
   else if(auto* i=cmd->kind()==Kind::If?static_cast<If*>(cmd):nullptr){std::set<Var*> r;reads(*i->cond.root,r);auto o=outside(*i->cond.root);
    if(draws){seeds.insert(r.begin(),r.end());if(!o.empty()&&first){reasons.push_back(where+"："+i->line+" 读取了 "+o);inputs.push_back(reasons.back());}}
    if(!o.empty()){r.insert(&outsideCondition);if(outsideText.empty())outsideText=where+"："+i->line+" 读取了 "+o;}
    control.push_back(r);for(auto* sub:{&i->truePre,&i->truePost,&i->falsePre,&i->falsePost})walk(e,*sub,draws,control,where,depth+1);control.pop_back();}
   else if(auto* r=cmd->kind()==Kind::Run?static_cast<RunList*>(cmd):nullptr){walk(e,r->target->pre,draws,control,r->target->name,depth+1);walk(e,r->target->post,draws,control,r->target->name,depth+1);}
   else if(auto* d=cmd->kind()==Kind::Draw?static_cast<DrawCmd*>(cmd):nullptr){if(!draws)continue;
    for(auto& x:d->args){reads(*x.root,seeds);auto o=outside(*x.root);if(!o.empty()&&first){reasons.push_back(where+"："+d->line+" 读取了 "+o);inputs.push_back(reasons.back());}}}
   else if(auto* b=cmd->kind()==Kind::Bind?static_cast<Bind*>(cmd):nullptr){if(!first)continue;if(e.resources.count(b->src)){if(bound.insert({b->dst,b->src}).second)bindings.push_back({b->dst,b->src});resources.insert(b->src);}
    else if(b->dstResource&&b->src!="null")external.push_back(where+"："+b->line);}
   else if(!first)continue;
   else if(auto* s=cmd->kind()==Kind::CustomShader?static_cast<CustomShaderRun*>(cmd):nullptr)customShaders.push_back(s->where);
   else if(auto* u=cmd->kind()==Kind::Unported?static_cast<Unported*>(cmd):nullptr)unported.push_back(u->where);}}

 explicit ImportScan(Engine& e,uint64_t limit=4096){
  for(auto& g:e.globals)names[&g.second]=g.first;
  for(auto& kv:e.lists)for(auto& v:kv.second->locals)names[&v]=kv.second->name+" local "+v.name;
  for(auto& k:e.keys)for(auto& p:k.presets)if(p.run)for(auto& v:p.run->locals)names[&v]=k.name+" local "+v.name;
  std::vector<std::set<Var*>> control;
  for(auto& kv:e.overrides)for(auto& t:kv.second){walk(e,t.list->pre,true,control,t.name,0);walk(e,t.list->post,true,control,t.name,0);}
  for(auto& kv:e.lists){if(istarts(kv.second->name,"TextureOverride"))continue;walk(e,kv.second->pre,false,control,kv.second->name,0);walk(e,kv.second->post,false,control,kv.second->name,0);}
  // A key's writes depend on its condition, and a smart cycle picks its next preset by the current values of all
  // the variables it sets.
  for(auto& k:e.keys){std::set<Var*> keyVars;if(k.type=="cycle"&&k.smart)for(auto& p:k.presets)for(auto& v:p.vars)keyVars.insert(v.first);
   for(auto& p:k.presets){std::set<Var*> ctl=keyVars;if(p.conditional){reads(*p.condition.root,ctl);auto o=outside(*p.condition.root);if(!o.empty()){ctl.insert(&outsideCondition);if(outsideText.empty())outsideText="["+k.name+"] 的条件读取了 "+o;}}
    for(auto& v:p.vars){Write w;w.var=v.first;w.keyValue=v.second;w.transition=p.transition||p.releaseTransition;w.control=ctl;w.where="["+k.name+"]";writes.push_back(w);}
    if(p.run){control.push_back(ctl);walk(e,p.run->pre,false,control,k.name,1);walk(e,p.run->post,false,control,k.name,1);control.pop_back();}}}
  // Draw variables: the seeds, and whatever the writes of a draw variable read or sit under.
  std::set<Var*> draw=seeds;for(bool grew=true;grew;){grew=false;
   for(auto& w:writes){if(!draw.count(w.var))continue;std::set<Var*> r=w.control;if(w.value)reads(*w.value->root,r);for(auto* x:r)grew|=draw.insert(x).second;}}
  draw.erase(&outsideCondition);
  // Domains: initial values, then rounds of every write over the current domains.
  std::map<Var*,std::set<float>> dom;std::map<Var*,std::string> bad;std::set<Var*> fromOutside;
  for(auto& w:writes)if(draw.count(w.var)&&w.control.count(&outsideCondition)&&!bad.count(w.var)){bad[w.var]=w.where+"：所在条件依赖 MOD 自身变量以外的数据（"+outsideText+"）";fromOutside.insert(w.var);}
  for(auto* v:draw){auto it=e.initial.find(names[v]);dom[v].insert(it!=e.initial.end()?it->second:0.0f);}
  for(auto& w:writes){if(!draw.count(w.var))continue;
   if(!w.value){if(w.transition){bad[w.var]=w.where+"：过渡（transition）会经过中间值";fromOutside.insert(w.var);}else dom[w.var].insert(w.keyValue);continue;}
   auto o=outside(*w.value->root);if(!o.empty()){bad[w.var]=w.where+" 读取了 "+o;fromOutside.insert(w.var);}}
  for(int round=0;round<16;round++){bool grew=false;
   for(auto& w:writes){if(!w.value||!draw.count(w.var)||bad.count(w.var))continue;std::set<Var*> r;reads(*w.value->root,r);std::vector<Var*> in(r.begin(),r.end());
    bool ok=true;uint64_t n=1;for(auto* x:in){if(bad.count(x)){bad[w.var]=w.where+" 读取了 "+names[x]+"，其取值不是有限的";ok=false;break;}n*=dom[x].size();if(n>4096){bad[w.var]=w.where+"：取值组合过多";ok=false;break;}}
    if(!ok)continue;
    // Every combination of the read variables' values; the variables are restored afterwards.
    std::vector<float> saved;for(auto* x:in)saved.push_back(x->value);StaticContext sc;
    std::vector<std::vector<float>> lists;for(auto* x:in)lists.emplace_back(dom[x].begin(),dom[x].end());std::vector<size_t> at(in.size(),0);
    for(uint64_t k=0;k<n;k++){for(size_t i=0;i<in.size();i++)in[i]->value=lists[i][at[i]];float v=0;staticEvaluate(*w.value,sc,v);if(std::isnan(v)){bad[w.var]=w.where+"：不是数值";break;}
     grew|=dom[w.var].insert(v).second;for(size_t i=0;i<at.size();i++){if(++at[i]<lists[i].size())break;at[i]=0;}}
    for(size_t i=0;i<in.size();i++)in[i]->value=saved[i];
    if(dom[w.var].size()>256&&!bad.count(w.var))bad[w.var]=w.where+"：取值超过 256 个（可能是计数器）";}
   if(!grew)break;
   if(round==15)for(auto& w:writes)if(w.value&&draw.count(w.var)&&!bad.count(w.var)){std::set<Var*> r;reads(*w.value->root,r);if(r.count(w.var)||!r.empty())bad[w.var]=w.where+"：取值持续增长（可能是计数器）";}}
  for(auto& w:writes){if(!w.var)continue;auto& info=vars[names[w.var]];info.name=names[w.var];}
  drawVars=draw;
  for(auto* v:draw){auto& info=vars[names[v]];info.name=names[v];info.readByDraws=true;info.values=dom[v];auto b=bad.find(v);if(b!=bad.end()){info.finite=false;info.why.push_back(b->second);if(fromOutside.count(v))inputs.push_back(info.name+"："+b->second);}}
  finiteStates=reasons.empty();
  for(auto* v:draw){auto& info=vars[names[v]];if(!info.finite){finiteStates=false;reasons.push_back(info.name+"："+info.why.front());continue;}
   states*=std::max<uint64_t>(1,info.values.size());if(states>limit){finiteStates=false;reasons.push_back("绘制变量的组合超过 "+std::to_string(limit)+" 种");break;}}
  for(auto& x:external){reasons.push_back("使用了游戏资源："+x);inputs.push_back(reasons.back());}
  if(!external.empty())finiteStates=false;}
};

class Session {
public:
 struct Pass {std::string name;DrawInfo draw;};
 // draws / bindings: section names, for people; key: the same by resource identity, which decides "changed".
 struct Selection {bool skip=false;std::vector<std::string> draws;std::map<std::string,std::string> bindings;std::string key;
  bool operator==(const Selection& o)const{return key==o.key;}};
 Engine& e;std::map<std::string,Selection> last;std::map<std::string,uint64_t> changesOf;uint64_t frames=0,changes=0;
 explicit Session(Engine& engine):e(engine){}
 Selection select(const DrawResult& r)const{Selection s;s.skip=r.skip;
  for(auto& d:r.draws){std::ostringstream o;o<<d.kind<<(d.automatic?"(auto)":d.fromCaller?"(from_caller)":"");for(auto a:d.args)o<<' '<<a;
   std::ostringstream k;k<<o.str();for(auto& b:d.bindings){o<<' '<<b.first<<'='<<describe(e,b.second);k<<' '<<b.first<<'='<<identity(e,b.second);}
   s.draws.push_back(o.str());s.key+=k.str()+";";}
  s.key+=s.skip?"skip;":"draw;";for(auto& b:r.bindings){s.bindings[b.first]=describe(e,b.second);s.key+=b.first+"="+identity(e,b.second)+" ";}
  return s;}
 // The frame's draws in the order the game issues them, then its Present. Returns the passes whose selection
 // changed (all of them on the first frame).
 std::vector<std::string> tick(const std::vector<Pass>& passes,double seconds,std::map<std::string,Selection>* out=nullptr){
  std::vector<std::string> changed;
  for(auto& p:passes){Selection s=select(e.draw(p.draw));auto it=last.find(p.name);if(it==last.end()||!(it->second==s)){changed.push_back(p.name);changesOf[p.name]++;}last[p.name]=s;if(out)(*out)[p.name]=s;}
  e.frame(seconds);frames++;changes+=changed.size();return changed;}
};

}  // namespace migoto
