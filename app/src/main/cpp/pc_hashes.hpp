// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// What the PC game's 3DMigoto shows for each character (0.7.82), and what this device's index uploads were tied to.
//
// GIBox/pc-hashes.json (the App installs it; tools/make_pc_hashes.py compacts GI-Model-Importer-Assets
// PlayerCharacterData/*/hash.json, GPL-3.0): per PC character (an outfit is its own one) and component its index-buffer
// hash, the index where each draw starts (= match_first_index), the counts where the source has them, and the
// textures each draw binds by slot (Diffuse, LightMap, NormalMap ...). A texture-only mod's hash is looked up here:
// an index-buffer hash names the component and all its draw starts, a texture hash the component, the draw and the
// slot it is in. Texture hashes cannot be recomputed on Android (its textures are other files), so this table is the
// only way to tell what one is.
//
// GIBox/hash-map.json (written by the Runtime): an index-buffer hash this device's upload reproduced, tied to the one
// mesh read with that upload's index and vertex counts, so a later session finds the mesh without the capture.
#include "core.hpp"
#include "json.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ms::pcdb {

inline std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
inline bool hex8(const std::string& s){return s.size()==8&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
// The material property a PC slot is on Android: "" when GIBox writes no such property.
inline std::string role(const std::string& slot){auto s=lower(slot);if(s=="diffuse")return "diffuse";if(s=="lightmap")return "lightmap";if(s=="normalmap")return "normal";
 if(s=="shadowramp"){return "ramp";}return s;}

struct Component {std::string character,name,ib;unsigned avatar=0;std::vector<uint64_t> indexes,counts;std::vector<std::vector<std::pair<std::string,std::string>>> textures;};
struct Use {size_t component=0;size_t submesh=0;std::string slot;};
struct Db {std::string commit,date,source;std::vector<Component> comps;std::map<std::string,std::vector<size_t>> byIb;std::map<std::string,std::vector<Use>> byTexture;
 std::map<std::string,unsigned> characters;   // PC character -> AvatarId (0: not tied)
 bool empty()const{return comps.empty();}};

inline uint64_t count(const Json& v,const char* what){require(v.type==Json::Number&&v.number>=0&&v.number<4294967296.0&&v.number==double(uint64_t(v.number)),std::string("pc-hashes: ")+what);return uint64_t(v.number);}
inline Db parse(const std::string& text){Json j=JsonParser(text).parse();Db d;
 require(j.type==Json::Object&&j.at("format").string=="gibox-pc-hashes","not a GIBox PC hash database");require(j.at("version").type==Json::Number&&j.at("version").number==1,"PC hash database version is not supported");
 d.commit=j.at("commit").string;d.date=j.at("date").string;d.source=j.at("source").string;
 const auto& cs=j.at("characters");require(cs.type==Json::Array&&cs.array.size()<=1024,"pc-hashes: characters");
 for(auto& c:cs.array){require(c.type==Json::Object&&!c.at("name").string.empty(),"pc-hashes: character");auto name=c.at("name").string;unsigned avatar=0;
  if(c.at("avatar").type==Json::Number){auto a=count(c.at("avatar"),"avatar");require(a==0||(a>=10000000&&a<=10001999),"pc-hashes: avatar of "+name);avatar=unsigned(a);}
  d.characters[name]=avatar;const auto& ps=c.at("components");require(ps.type==Json::Array&&ps.array.size()<=64,"pc-hashes: components of "+name);
  for(auto& p:ps.array){require(p.type==Json::Object,"pc-hashes: component of "+name);Component x;x.character=name;x.avatar=avatar;x.name=p.at("name").string;x.ib=lower(p.at("ib").string);
   require(x.ib.empty()||hex8(x.ib),"pc-hashes: ib of "+name+" "+x.name);
   for(auto* k:{"indexes","counts"}){const auto& a=p.at(k);if(a.type!=Json::Array)continue;require(a.array.size()<=64,"pc-hashes: "+std::string(k));for(auto& v:a.array)(std::string(k)=="indexes"?x.indexes:x.counts).push_back(count(v,k));}
   const auto& t=p.at("textures");if(t.type==Json::Array){require(t.array.size()<=64,"pc-hashes: textures");for(auto& sub:t.array){std::vector<std::pair<std::string,std::string>> s;
     if(sub.type==Json::Array)for(auto& b:sub.array)if(b.type==Json::Array&&b.array.size()==2&&b.array[0].type==Json::String&&hex8(lower(b.array[1].string)))s.push_back({b.array[0].string,lower(b.array[1].string)});
     x.textures.push_back(std::move(s));}}
   size_t at=d.comps.size();if(!x.ib.empty())d.byIb[x.ib].push_back(at);
   for(size_t k=0;k<x.textures.size();k++)for(auto& b:x.textures[k])d.byTexture[b.second].push_back({at,k,b.first});
   d.comps.push_back(std::move(x));}}
 return d;}

// The PC characters a mod is for: those whose index buffers its rules name (an outfit is its own PC character);
// without such a hash, those tied to its AvatarId (every outfit of it).
inline std::set<std::string> charactersFor(const Db& d,unsigned avatar,const std::vector<std::string>& hashes,std::string* how=nullptr){std::set<std::string> out;
 for(auto& h:hashes){auto it=d.byIb.find(lower(h));if(it!=d.byIb.end())for(size_t i:it->second)out.insert(d.comps[i].character);}
 if(!out.empty()){if(how)*how="by the index-buffer hashes its rules name";return out;}
 if(avatar){for(auto& c:d.characters)if(c.second==avatar)out.insert(c.first);}if(how)*how=out.empty()?"none: no index-buffer hash of a PC character and AvatarId "+std::to_string(avatar)+" ties to none":"by AvatarId "+std::to_string(avatar);
 return out;}

// ---------------------------------------------------------------- this device's index uploads (hash-map.json)
struct Mesh {std::string ib,variant,character,component,renderer,mesh,avatar,build;unsigned vertices=0,bones=0;std::vector<std::pair<uint32_t,uint32_t>> subs;};
struct DeviceMap {std::map<std::string,Mesh> byIb;};
inline DeviceMap parseMap(const std::string& text){Json j=JsonParser(text).parse();DeviceMap m;
 require(j.type==Json::Object&&j.at("format").string=="gibox-hash-map"&&j.at("version").type==Json::Number&&j.at("version").number==1,"not a GIBox hash map");
 const auto& a=j.at("meshes");require(a.type==Json::Array&&a.array.size()<=8192,"hash map meshes");
 for(auto& x:a.array){require(x.type==Json::Object,"hash map mesh");Mesh e;e.ib=lower(x.at("ib").string);require(hex8(e.ib),"hash map ib");
  e.variant=x.at("variant").string;e.character=x.at("character").string;e.component=x.at("component").string;e.renderer=x.at("renderer").string;e.mesh=x.at("mesh").string;e.avatar=x.at("avatar").string;e.build=x.at("build").string;
  e.vertices=unsigned(count(x.at("vertices"),"vertices"));e.bones=x.at("bones").type==Json::Number?unsigned(count(x.at("bones"),"bones")):0;require(!e.mesh.empty()&&e.vertices>0,"hash map mesh "+e.ib);
  const auto& s=x.at("submeshes");require(s.type==Json::Array&&!s.array.empty()&&s.array.size()<=64,"hash map submeshes");
  for(auto& p:s.array){require(p.type==Json::Array&&p.array.size()==2,"hash map submesh");e.subs.push_back({uint32_t(count(p.array[0],"start")),uint32_t(count(p.array[1],"count"))});}
  m.byIb[e.ib]=std::move(e);}
 return m;}
inline std::string writeMap(const DeviceMap& m){std::string o="{\"format\":\"gibox-hash-map\",\"version\":1,\"meshes\":[";bool first=true;
 for(auto& kv:m.byIb){auto& e=kv.second;o+=std::string(first?"":",")+"\n{\"ib\":"+quote(e.ib)+",\"variant\":"+quote(e.variant)+",\"character\":"+quote(e.character)+",\"component\":"+quote(e.component)+",\"renderer\":"+quote(e.renderer)
   +",\"mesh\":"+quote(e.mesh)+",\"avatar\":"+quote(e.avatar)+",\"build\":"+quote(e.build)+",\"vertices\":"+std::to_string(e.vertices)+",\"bones\":"+std::to_string(e.bones)+",\"submeshes\":[";
  for(size_t i=0;i<e.subs.size();i++){o+=std::string(i?",":"")+"["+std::to_string(e.subs[i].first)+","+std::to_string(e.subs[i].second)+"]";}o+="]}";first=false;}
 return o+"\n]}\n";}
inline uint64_t indices(const std::vector<std::pair<uint32_t,uint32_t>>& subs){uint64_t n=0;for(auto& s:subs)n=std::max<uint64_t>(n,uint64_t(s.first)+s.second);return n;}
// Do these submesh starts carry the PC draw starts? equal: the same set; else every PC start is one of them.
inline bool startsEqual(const std::vector<std::pair<uint32_t,uint32_t>>& subs,const std::vector<uint64_t>& pc){if(subs.size()!=pc.size())return false;
 std::vector<uint64_t> a;for(auto& s:subs)a.push_back(s.first);std::sort(a.begin(),a.end());auto b=pc;std::sort(b.begin(),b.end());return a==b;}
}  // namespace ms::pcdb
