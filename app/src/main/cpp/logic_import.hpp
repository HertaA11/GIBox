// SPDX-License-Identifier: GPL-3.0-only
// The converter's door to the logic core (migoto/rehearsal.hpp): a request with the mod's INI files, a reply with the
// frames its Present program shows and the order it shows them in. One JSON string each way, so the App (through
// NativeBridge.rehearse) and the host tests (core_test --rehearse) call the same code.
//
// Request: {"main": namespace of the main INI, "files": [{"namespace", "text"}], "limits": {"presents", "states",
// "frames", "ms"} (each optional)}. Reply: {"ok": true, "frames": [{"$var": value}], "sequence": [frame per Present
// from the start], "loop": index the sequence repeats from, "states", "presents", "keyFrames", "keys", "phases":
// [{"var", "modulus"}], "notes", "us"} or {"ok": false, "external", "reason"}; external: what is drawn
// follows the game (its clock, its draws), so no order can be rehearsed, but the converted outfits can still be picked
// in the game by the core.
#pragma once
#include "json.hpp"
#include "migoto/rehearsal.hpp"
#include <cstdio>
#include <string>
#include <vector>

namespace ms {

inline std::string rehearseJson(const std::string& request){
 try{Json j=JsonParser(request).parse();require(j.type==Json::Object,"预演请求：不是对象");const auto& main=j.at("main");require(main.type==Json::String&&!main.string.empty(),"预演请求：main 无效");
  std::vector<migoto::IniFile> files;const auto& list=j.at("files");require(list.type==Json::Array&&!list.array.empty()&&list.array.size()<=64,"预演请求：files 无效");size_t total=0;
  for(auto& f:list.array){require(f.type==Json::Object&&f.at("namespace").type==Json::String&&f.at("text").type==Json::String,"预演请求：file 无效");total+=f.at("text").string.size();require(total<=8u*1024*1024,"预演请求：INI 文件总计超过 8 MiB");files.push_back({f.at("namespace").string,f.at("text").string});}
  migoto::Rehearsal::Limits lim;const auto& l=j.at("limits");
  if(l.type==Json::Object){auto take=[&](const char* k,uint64_t& out,uint64_t hi){const auto& n=l.at(k);if(n.type==Json::Number){require(n.number>=1&&n.number<=double(hi)&&std::floor(n.number)==n.number,std::string("预演请求：限制值无效 ")+k);out=uint64_t(n.number);}};
   take("presents",lim.presents,1u<<20);take("states",lim.states,1u<<20);take("frames",lim.frames,256);take("ms",lim.ms,60000);}
  migoto::Engine e;e.load(files);e.start();migoto::Rehearsal r(e,main.string,lim);
  auto number=[](double v){char b[32];std::snprintf(b,sizeof b,"%.9g",v);return std::string(b);};
  if(!r.ok)return "{\"ok\":false,\"external\":"+std::string(r.external?"true":"false")+",\"reason\":"+quote(r.reason)+",\"us\":"+std::to_string(r.us)+"}";
  std::string out="{\"ok\":true,\"frames\":[";
  for(size_t i=0;i<r.frames.size();i++){out+=i?",{":"{";bool first=true;for(auto& kv:r.frames[i]){out+=(first?"":",")+quote(kv.first)+":"+std::to_string(kv.second);first=false;}out+="}";}
  out+="],\"sequence\":[";for(size_t i=0;i<r.sequence.size();i++)out+=(i?",":"")+std::to_string(r.sequence[i]);
  out+="],\"loop\":"+std::to_string(r.loop)+",\"states\":"+std::to_string(r.states)+",\"presents\":"+std::to_string(r.presents)+",\"keyFrames\":"+std::to_string(r.keyFrames)+",\"keys\":[";
  for(size_t i=0;i<r.keys.size();i++)out+=(i?",":"")+quote(r.keys[i]);
  out+="],\"phases\":[";for(size_t i=0;i<r.phases.size();i++)out+=std::string(i?",":"")+"{\"var\":"+quote(r.phases[i].var)+",\"modulus\":"+number(r.phases[i].modulus)+"}";
  out+="],\"notes\":[";for(size_t i=0;i<r.notes.size();i++)out+=(i?",":"")+quote(r.notes[i]);
  return out+"],\"us\":"+std::to_string(r.us)+"}";}
 catch(const std::exception& ex){return "{\"ok\":false,\"reason\":"+quote(std::string("the INI could not be rehearsed: ")+ex.what())+"}";}}

// What the mod's INI draws for one assignment, by the core (the converter's 3DMigoto backend, 0.7.42): the INIs
// are loaded and [Constants] run, the main INI's variables set, then the game's draws (the converter's passes, as in
// model.json logic.passes) replayed in order. Request: {"main", "files": [{"namespace", "text"}], "vars": {"$var":
// value}, "passes": [{"name", "bindings": {slot: hash}, "checks", "call"}]}. Reply: {"ok": true, "passes": [{"name",
// "skip", "bindings": {slot: ref}, "draws": [{"kind", "args", "auto", "bindings": {slot: ref}}]}], "notes"} or
// {"ok": false, "reason"}; a ref is "mod:<resource key>", "game:<hash>", "null" or "-".
inline std::string drawsJson(const std::string& request){
 auto ref=[](const migoto::Ref& r)->std::string{switch(r.kind){case migoto::Ref::None:return "-";case migoto::Ref::Null:return "null";
   case migoto::Ref::Game:{char b[16];std::snprintf(b,sizeof b,"game:%08x",r.hash);return b;}case migoto::Ref::Mod:return "mod:"+r.resource;}return "-";};
 auto slots=[&](const std::map<std::string,migoto::Ref>& m){std::string o="{";bool first=true;for(auto& kv:m){o+=(first?"":",")+quote(kv.first)+":"+quote(ref(kv.second));first=false;}return o+"}";};
 try{Json j=JsonParser(request).parse();require(j.type==Json::Object,"绘制请求：不是对象");const auto& main=j.at("main");require(main.type==Json::String&&!main.string.empty(),"绘制请求：main 无效");
  std::vector<migoto::IniFile> files;const auto& list=j.at("files");require(list.type==Json::Array&&!list.array.empty()&&list.array.size()<=64,"绘制请求：files 无效");size_t total=0;
  for(auto& f:list.array){require(f.type==Json::Object&&f.at("namespace").type==Json::String&&f.at("text").type==Json::String,"绘制请求：file 无效");total+=f.at("text").string.size();require(total<=8u*1024*1024,"INI files over 8 MiB");files.push_back({f.at("namespace").string,f.at("text").string});}
  const auto& passes=j.at("passes");require(passes.type==Json::Array&&!passes.array.empty()&&passes.array.size()<=256,"绘制请求：passes 无效");
  // One assignment ("vars") or many ("assignments", 0.7.107: Varka's 4096 outfits were 4096 calls, each sending and
  // parsing the whole INI again).
  // The INIs are loaded once; before each assignment the engine is put back to what start() left: every global and
  // ini param, the slots set outside a draw, and nothing pending (transitions, saves, post lists, notes).
  migoto::Engine e;e.load(files);e.start();
  const auto globals0=e.globals;const auto params0=e.params;const auto context0=e.contextBindings;const auto custom0=e.customResources;const auto notes0=e.diag.list;
  auto one=[&](const Json& vars){
   for(auto& kv:e.globals){auto it=globals0.find(kv.first);if(it!=globals0.end())kv.second=it->second;}
   e.params=params0;e.contextBindings=context0;e.customResources=custom0;e.varTransitions.clear();e.paramTransitions.clear();
   e.globalSaves.clear();e.globalParamSaves.clear();e.pendingPost.clear();e.queued.clear();e.persistDirty=false;e.diag.list=notes0;
   if(vars.type==Json::Object)for(auto& kv:vars.object){require(kv.second.type==Json::Number&&std::isfinite(kv.second.number),"绘制请求：变量值无效 "+kv.first);
    if(migoto::Var* v=e.global(migoto::lower(kv.first),main.string))v->value=float(kv.second.number);}
   std::string out="{\"passes\":[";
  for(size_t i=0;i<passes.array.size();i++){const auto& p=passes.array[i];require(p.type==Json::Object&&p.at("name").type==Json::String,"绘制请求：pass 无效");migoto::DrawInfo d;
   const auto& b=p.at("bindings");if(b.type==Json::Object)for(auto& kv:b.object){require(kv.second.type==Json::String,"绘制请求：binding 无效");d.bindings[kv.first]=migoto::Ref::game(uint32_t(std::stoul(kv.second.string,nullptr,16)));}
   const auto& c=p.at("checks");if(c.type==Json::Array)for(auto& x:c.array){require(x.type==Json::String,"绘制请求：check 无效");d.checks.push_back(x.string);}
   const auto& call=p.at("call");if(call.type==Json::Object)for(auto& kv:call.object){require(kv.second.type==Json::Number,"绘制请求：call 无效");d.call[kv.first]=float(kv.second.number);}
   auto r=e.draw(d);out+=std::string(i?",":"")+"{\"name\":"+quote(p.at("name").string)+",\"skip\":"+(r.skip?"true":"false")+",\"bindings\":"+slots(r.bindings)+",\"draws\":[";
   for(size_t k=0;k<r.draws.size();k++){auto& x=r.draws[k];out+=std::string(k?",":"")+"{\"kind\":"+quote(x.kind)+",\"auto\":"+(x.automatic?"true":"false")+",\"args\":[";
    for(size_t a=0;a<x.args.size();a++){char n[32];std::snprintf(n,sizeof n,"%.9g",double(x.args[a]));out+=std::string(a?",":"")+n;}out+="],\"bindings\":"+slots(x.bindings)+"}";}
   out+="]}";}
  out+="],\"notes\":[";size_t n=0;for(auto& d:e.diag.list){if(d.level==migoto::Diagnostic::Notice)continue;if(n>=24)break;out+=(n++?",":"")+quote(d.where+": "+d.message);}
  return out+"]}";};
  const auto& many=j.at("assignments");
  if(many.type==Json::Array){require(!many.array.empty()&&many.array.size()<=4096,"绘制请求：assignments 无效");std::string out="{\"ok\":true,\"results\":[";
   for(size_t k=0;k<many.array.size();k++){out+=(k?",":"")+one(many.array[k]);}
   return out+"]}";}
  auto r=one(j.at("vars"));return "{\"ok\":true,"+r.substr(1);}
 catch(const std::exception& ex){return "{\"ok\":false,\"reason\":"+quote(std::string("the INI could not be run: ")+ex.what())+"}";}}

}  // namespace ms
