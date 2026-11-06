// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Texture-only mods, phase 1: read-only diagnostics (nothing here or in its Runtime wiring writes to the game).
//
// The App writes GIBox/texture-diag/<model>.json for a mod that only changes textures (TextureDiag.java): its
// TextureOverride rules as the INI states them. The Runtime reads the controlled character's renderers (their game
// mesh and materials, also under a GIBox model) and this file matches the two and words the log lines.
//
// What a rule is matched by, strongest first; the hash itself never takes part: a 3DMigoto hash is taken from the PC
// game's D3D11 buffer or texture, which Android does not have, and nothing here recomputes one.
//  * index start: match_first_index is where the PC draw starts in its index buffer; a submesh whose indexStart
//    (Mesh.GetIndexStart, read by name through IL2CPP) equals it, on exactly one renderer, with every other
//    match_first_index of the same hash also a submesh start of that renderer, is "confirmed" when the renderer has as
//    many materials as submeshes (slot = submesh). It holds only where the Android mesh keeps the PC index order,
//    which these logs are there to show.
//  * the part name the INI gives the section (DressA -> dress) against the renderer's name, counts (segments of the
//    hash, submeshes, materials) and the segment's place among them: "likely" at best, never "confirmed".
//  * the size of the mod's texture against the game's (texture-hash overrides): candidates only.
// A PC segment that starts inside an Android submesh (not at its start) means PC draws were merged into one submesh
// and one material slot: reported as such, ambiguous when the merged segments ask for different textures.
#include "core.hpp"
#include "json.hpp"
#include "pc_hashes.hpp"
#include "render_probe.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace ms::texdiag {

struct Binding {std::string slot,resource,role,condition,via;};
// What the PC database says a rule's hash is (annotate): a component's index buffer, or a texture of some of its draws.
struct PcPart {std::string character,component,ib,slot,role;size_t submesh=0;std::vector<uint64_t> indexes,counts;};
struct Rule {std::string section,part,hint,hash,kind,evidence;int slotLetter=-1;long long first=-1;int priority=0;bool textureName=false;
 std::vector<Binding> textures;std::vector<std::string> external,conditions,draws;
 bool pcHasIb=false;PcPart pcIb;std::vector<PcPart> pcTextures;std::string pcNote;};
struct Resource {std::string name,file,kind,format,error;bool exists=false;long long width=-1,height=-1,mips=-1;};
struct Doc {std::string path,stamp,name,ini,characterName,pc;unsigned character=0;std::vector<Rule> rules;std::map<std::string,Resource> resources;std::vector<std::string> variables,others,notes;};

inline std::string str(const Json& j,const char* k){const auto& v=j.at(k);return v.type==Json::String?v.string:std::string();}
inline long long num(const Json& j,const char* k,long long fallback){const auto& v=j.at(k);return v.type==Json::Number&&std::isfinite(v.number)&&std::floor(v.number)==v.number&&std::fabs(v.number)<9e15?(long long)v.number:fallback;}
// The App's document (version 1); anything it cannot use is an error naming the field, never a silent skip.
inline Doc parseDoc(const Json& j,const std::string& path){Doc d;d.path=path;
 require(j.type==Json::Object&&str(j,"format")=="gibox-texture-diag","not a GIBox texture diagnostic");require(num(j,"version",0)==1,"texture diagnostic version "+std::to_string(num(j,"version",0))+" is not supported");
 d.name=str(j,"name");d.ini=str(j,"ini");d.characterName=str(j,"characterName");auto c=num(j,"character",0);require(c==0||(c>=10000000&&c<=10001999),"texture diagnostic character");d.character=unsigned(c);
 const auto& rules=j.at("rules");require(rules.type==Json::Array&&rules.array.size()<=4096,"texture diagnostic rules");
 for(auto& r:rules.array){require(r.type==Json::Object,"texture diagnostic rule");Rule x;x.section=str(r,"section");x.part=str(r,"part");x.hint=str(r,"hint");x.hash=str(r,"hash");x.kind=str(r,"kind");x.evidence=str(r,"hashEvidence");
  x.slotLetter=int(num(r,"slotLetter",-1));x.first=num(r,"first",-1);x.priority=int(num(r,"priority",0));x.textureName=r.at("textureName").type==Json::Bool&&r.at("textureName").boolean;
  require(!x.section.empty()&&!x.kind.empty(),"texture diagnostic rule without section or kind");
  const auto& t=r.at("textures");if(t.type==Json::Array)for(auto& b:t.array){require(b.type==Json::Object,"texture binding");x.textures.push_back({str(b,"slot"),str(b,"resource"),str(b,"role"),str(b,"condition"),str(b,"via")});}
  for(auto* k:{"external","conditions"}){const auto& a=r.at(k);if(a.type==Json::Array)for(auto& v:a.array)if(v.type==Json::String)(std::string(k)=="external"?x.external:x.conditions).push_back(v.string);}
  const auto& di=r.at("drawIndexed");if(di.type==Json::Array)for(auto& v:di.array)if(v.type==Json::Object)x.draws.push_back(str(v,"args")+(str(v,"condition").empty()?"":" if "+str(v,"condition")));
  d.rules.push_back(std::move(x));}
 const auto& res=j.at("resources");if(res.type==Json::Array)for(auto& r:res.array){if(r.type!=Json::Object)continue;Resource x;x.name=str(r,"name");x.file=str(r,"file");x.kind=str(r,"kind");x.format=str(r,"format");x.error=str(r,"error");
  x.exists=r.at("exists").type==Json::Bool&&r.at("exists").boolean;x.width=num(r,"width",-1);x.height=num(r,"height",-1);x.mips=num(r,"mips",-1);d.resources[x.name]=x;}
 const auto& vars=j.at("variables");if(vars.type==Json::Array)for(auto& v:vars.array)if(v.type==Json::Object){std::string vals;const auto& a=v.at("values");if(a.type==Json::Array)for(auto& n:a.array)if(n.type==Json::Number)vals+=(vals.empty()?"":",")+std::to_string((long long)n.number);
  d.variables.push_back(str(v,"name")+"="+std::to_string(num(v,"initial",0))+" of ["+vals+"]"+(v.at("persist").boolean?" persist":""));}
 const auto& others=j.at("otherInis");if(others.type==Json::Array)for(auto& o:others.array)if(o.type==Json::Object){std::string h;const auto& a=o.at("sharedHashes");if(a.type==Json::Array)for(auto& n:a.array)if(n.type==Json::String)h+=(h.empty()?"":",")+n.string;d.others.push_back(str(o,"ini")+(h.empty()?"":" shares "+h));}
 const auto& notes=j.at("notes");if(notes.type==Json::Array)for(auto& n:notes.array)if(n.type==Json::String)d.notes.push_back(n.string);
 return d;}
inline Doc parse(const std::string& text,const std::string& path){return parseDoc(JsonParser(text).parse(),path);}

// What the Runtime read of one renderer. A string field starting with "未采集:" was not available, with the reason.
struct Texture {std::string property;int id=0;Ptr texture=0;std::string name,cls,width,height,mips,format,status;};
struct Material {Ptr material=0;std::string name,shader,error;std::vector<Texture> textures;std::vector<std::string> sharedWith;};
struct Renderer {Ptr renderer=0,native=0,mesh=0;std::string name,meshName,avatar,takenBy,swap,error;bool lod=false;unsigned vertices=0,submeshes=0,bones=0;
 renderprobe::UnityReader::MeshLayout layout;std::vector<Material> materials;
 bool known()const{if(layout.subs.empty()||int(layout.subs.size())!=layout.submeshes)return false;for(auto& s:layout.subs)if(!s.known)return false;return true;}};

enum class Verdict{Confirmed,Likely,Ambiguous,None,Skipped};
inline const char* verdictName(Verdict v){switch(v){case Verdict::Confirmed:return "确定匹配";case Verdict::Likely:return "高可信候选";case Verdict::Ambiguous:return "存在歧义";case Verdict::None:return "无法匹配";default:return "不需匹配";}}
struct Match {size_t rule=0;Verdict verdict=Verdict::None;int renderer=-1,submesh=-1,slot=-1;std::vector<std::string> reasons,candidates;};

inline std::string letters(const std::string& s){std::string o;for(unsigned char c:s)if(std::isalnum(c))o+=char(std::tolower(c));return o;}
// How well a part hint names a renderer: the length of the renderer's name when the hint ends or starts with it.
inline size_t nameScore(const std::string& hint,const std::string& renderer){auto h=letters(hint),r=letters(normalize(renderer));if(r.empty()||h.empty())return 0;
 bool end=h.size()>=r.size()&&h.compare(h.size()-r.size(),r.size(),r)==0,start=h.compare(0,r.size(),r)==0;return end||start?r.size():0;}
// The part a game material is made for: "Avatar_Girl_Sword_PlayerGirl_Mat_Hair (Instance) #0" -> "hair"; also for
// the game's instance of a copy, "..._Mat_Hair(Clone) (Instance) #1".
inline std::string materialPart(const std::string& name){std::string n;for(unsigned char c:name)n+=char(std::tolower(c));
 for(const char* cut:{" (instance)","(clone)","#"}){auto p=n.find(cut);if(p!=std::string::npos)n.resize(p);}auto p=n.rfind("_mat_");return letters(p==std::string::npos?std::string():n.substr(p+5));}
// How well a part hint names a material's part. GIMI calls the game's hair part "Head" (its Head draws use the game's
// Hair textures): that naming convention is the one alias, and the reason says so.
inline size_t partScore(const std::string& hint,const std::string& part,bool* alias=nullptr){if(part.empty())return 0;auto s=nameScore(hint,part);if(alias)*alias=false;
 if(!s&&part=="hair"&&nameScore(hint,"head")){if(alias)*alias=true;return 4;}return s;}
// Texture2D.format as this build's TextureFormat enum names it (dump.cs).
inline std::string textureFormat(int f){static const std::map<int,const char*> names{{1,"Alpha8"},{2,"ARGB4444"},{3,"RGB24"},{4,"RGBA32"},{5,"ARGB32"},{7,"RGB565"},{9,"R16"},{10,"DXT1"},{12,"DXT5"},
  {13,"RGBA4444"},{14,"BGRA32"},{15,"RHalf"},{16,"RGHalf"},{17,"RGBAHalf"},{18,"RFloat"},{19,"RGFloat"},{20,"RGBAFloat"},{21,"YUY2"},{22,"RGB9e5Float"},{24,"BC6H"},{25,"BC7"},{26,"BC4"},{27,"BC5"},
  {28,"DXT1Crunched"},{29,"DXT5Crunched"},{30,"PVRTC_RGB2"},{31,"PVRTC_RGBA2"},{32,"PVRTC_RGB4"},{33,"PVRTC_RGBA4"},{34,"ETC_RGB4"},{35,"ATC_RGB4"},{36,"ATC_RGBA8"},{41,"EAC_R"},{42,"EAC_R_SIGNED"},
  {43,"EAC_RG"},{44,"EAC_RG_SIGNED"},{45,"ETC2_RGB"},{46,"ETC2_RGBA1"},{47,"ETC2_RGBA8"},{48,"ASTC_RGB_4x4"},{49,"ASTC_RGB_5x5"},{50,"ASTC_RGB_6x6"},{51,"ASTC_RGB_8x8"},{52,"ASTC_RGB_10x10"},
  {53,"ASTC_RGB_12x12"},{54,"ASTC_RGBA_4x4"},{55,"ASTC_RGBA_5x5"},{56,"ASTC_RGBA_6x6"},{57,"ASTC_RGBA_8x8"},{58,"ASTC_RGBA_10x10"},{59,"ASTC_RGBA_12x12"},{60,"ETC_RGB4_3DS"},{61,"ETC_RGBA8_3DS"},
  {62,"RG16"},{63,"R8"},{64,"ETC_RGB4Crunched"},{65,"ETC2_RGBA8Crunched"},{66,"R16Unorm"},{67,"RUInt"},{68,"R8UInt"},{70,"RGBAUInt"}};
 auto it=names.find(f);return std::to_string(f)+" ("+(it==names.end()?"not in this build's TextureFormat":it->second)+")";}
inline std::string hexOf(Ptr p){char b[24];std::snprintf(b,sizeof b,"0x%llx",(unsigned long long)p);return b;}
inline std::string label(const Renderer& r){return r.name+"#"+hexOf(r.native);}
// The game properties phase 2 would put a role on, among those the material has; "待诊断" when none or unknown.
inline const std::vector<const char*>& candidates(const std::string& role){
 static const std::vector<const char*> diffuse{"_MainTex"},light{"_LightMapTex","_LightMap","_LightmapTex","_Lightmap"},normal{"_BumpMap","_NormalMap","_BaseNormalMap","_NormalTex"},ramp{"_ShadowRampTex","_RampTex"},none{};
 if(role=="diffuse"){return diffuse;}if(role=="lightmap"){return light;}if(role=="normal"){return normal;}if(role=="ramp"){return ramp;}return none;}
inline std::string orDash(const std::string& s){return s.empty()?std::string("-"):s;}
inline std::string textureText(const Texture& t){return t.property+(t.texture?"="+(t.name.empty()?std::string("(unnamed)"):t.name)+(t.width.empty()?"":" "+t.width+"x"+t.height)+(t.format.empty()?"":" "+t.format)+(t.mips.empty()?"":" mips "+t.mips):std::string("=null"))+(t.status.empty()?"":" ["+t.status+"]");}
inline std::string plan(const Doc& d,const Rule& r,const Material* m){std::string out;
 for(auto& b:r.textures){std::string target="待诊断";
  if(m)for(auto* p:candidates(b.role)){for(auto& t:m->textures)if(t.property==p){target=std::string(p)+" (material has it; current "+(t.texture?t.name+(t.width.empty()?"":" "+t.width+"x"+t.height)+(t.format.empty()||t.format.find("未采集:")==0?"":" "+t.format):std::string("null"))+")";break;}if(target!="待诊断")break;}
  if(target=="待诊断")target+=b.role=="unknown"?" (slot role unknown)":m?" (no candidate property on this material)":" (no material)";
  auto res=d.resources.find(b.resource);std::string size=res==d.resources.end()?"":res->second.width>=0?" "+std::to_string(res->second.width)+"x"+std::to_string(res->second.height)+" "+res->second.format:res->second.error.empty()?"":" ("+res->second.error+")";
  out+=(out.empty()?"":"; ")+b.slot+" "+b.resource+size+" ["+b.role+"]"+(b.condition.empty()?"":" if "+b.condition)+" -> "+target;}
 return out.empty()?"no texture bindings":out;}

// The PC database's word on each rule (0.7.82): an index-buffer hash gives the component and all its draw starts, a
// texture hash (restricted to the PC characters the mod is for) the component, draw and slot it is in. A texture rule's
// "this" binding takes the role of that slot (a LightMap hash replaces the light map whatever its file is called).
inline std::string joined(const std::vector<uint64_t>& v){std::string o;for(auto x:v)o+=(o.empty()?"":",")+std::to_string(x);return "["+o+"]";}
inline void annotate(Doc& d,const pcdb::Db& db){std::vector<std::string> hashes;for(auto& r:d.rules)if(!r.hash.empty())hashes.push_back(r.hash);
 std::string how;auto chars=pcdb::charactersFor(db,d.character,hashes,&how);std::string names;for(auto& c:chars)names+=(names.empty()?"":", ")+c;
 d.pc="PC database "+db.commit.substr(0,7)+": "+(chars.empty()?std::string("no PC character for this mod"):names)+" ("+how+")";
 for(auto& r:d.rules){auto h=pcdb::lower(r.hash);r.pcHasIb=false;r.pcIb=PcPart{};r.pcTextures.clear();r.pcNote.clear();
  auto ib=db.byIb.find(h);if(ib!=db.byIb.end())for(size_t i:ib->second){auto& c=db.comps[i];if(!chars.empty()&&!chars.count(c.character))continue;r.pcHasIb=true;r.pcIb={c.character,c.name,c.ib,"","",0,c.indexes,c.counts};break;}
  if(r.pcHasIb&&r.first>=0&&std::find(r.pcIb.indexes.begin(),r.pcIb.indexes.end(),uint64_t(r.first))==r.pcIb.indexes.end())
   r.pcNote="match_first_index "+std::to_string(r.first)+" is not a PC draw start of "+r.pcIb.character+" "+r.pcIb.component+" "+joined(r.pcIb.indexes)+" (a mod for another game version?)";
  auto tx=db.byTexture.find(h);if(tx==db.byTexture.end()||chars.empty())continue;std::set<std::string> roles;
  for(auto& u:tx->second){auto& c=db.comps[u.component];if(!chars.count(c.character))continue;PcPart x{c.character,c.name,c.ib,u.slot,pcdb::role(u.slot),u.submesh,c.indexes,c.counts};
   bool dup=false;for(auto& q:r.pcTextures)dup=dup||(q.component==x.component&&q.ib==x.ib&&q.submesh==x.submesh&&q.slot==x.slot);if(!dup)r.pcTextures.push_back(x);roles.insert(x.role);}
  if(roles.size()==1){auto role=*roles.begin();for(auto& b:r.textures)if(b.slot=="this"&&b.role!=role){r.pcNote+=(r.pcNote.empty()?"":"; ")+std::string("this = ")+(b.resource.empty()?"null":b.resource)+": role "+b.role+" by its name, "+role+" by the PC slot; the PC slot is used";b.role=role;}}
  else if(roles.size()>1){std::string list;for(auto& x:roles)list+=(list.empty()?"":"/")+x;r.pcNote+=std::string(r.pcNote.empty()?"":"; ")+"the texture is in several kinds of slot on PC ("+list+"); roles left as named";}}}

inline std::vector<Match> match(const Doc& d,const std::vector<Renderer>& rs,const pcdb::DeviceMap* device=nullptr){
 std::vector<Match> out;std::map<std::string,std::vector<long long>> firsts;
 for(auto& r:d.rules){if(r.first>=0)firsts[r.hash].push_back(r.first);}for(auto& f:firsts)std::sort(f.second.begin(),f.second.end());
 auto ruleAt=[&](long long a,const std::string& hash)->const Rule*{for(auto& r:d.rules)if(r.hash==hash&&r.first==a)return &r;return nullptr;};
 auto sameTextures=[&](long long a,long long b,const std::string& hash){const Rule* x=ruleAt(a,hash);const Rule* y=ruleAt(b,hash);
  if(!x||!y||x->textures.size()!=y->textures.size()){return false;}for(size_t i=0;i<x->textures.size();i++)if(x->textures[i].slot!=y->textures[i].slot||x->textures[i].resource!=y->textures[i].resource||x->textures[i].condition!=y->textures[i].condition)return false;return true;};
 auto hitsFor=[&](const std::string& hint){std::vector<int> hits;size_t best=0;for(size_t i=0;i<rs.size();i++){auto s=nameScore(hint,rs[i].name);if(!s)continue;if(s>best){best=s;hits.clear();}if(s==best)hits.push_back(int(i));}return hits;};
 // The part name the INI gives the section; failing that, the PC component the hash is.
 auto nameHits=[&](const Rule& r){auto h=hitsFor(r.hint);if(h.empty()&&r.pcHasIb&&!r.pcIb.component.empty())h=hitsFor(pcdb::lower(r.pcIb.component));return h;};
 // A layout comparable with the PC index buffer: read, and not a lower LOD (whose index buffer is its own).
 auto comparable=[&](size_t i){return rs[i].known()&&!rs[i].lod;};
 auto slotOf=[&](const Renderer& r,int sub,Match& m){size_t mats=r.materials.size(),subs=size_t(r.layout.submeshes>=0?r.layout.submeshes:int(r.submeshes));
  if(mats==subs){m.reasons.push_back("materials = submeshes ("+std::to_string(mats)+"): submesh "+std::to_string(sub)+" draws with material slot "+std::to_string(sub));return true;}
  if(size_t(sub)<mats&&size_t(sub)+1<subs){m.reasons.push_back(std::to_string(mats)+" materials, "+std::to_string(subs)+" submeshes: slot "+std::to_string(sub)+" by position (counts differ)");return false;}
  if(size_t(sub)>=mats){m.reasons.push_back("submesh "+std::to_string(sub)+" has no material slot ("+std::to_string(mats)+" materials, "+std::to_string(subs)+" submeshes)");m.slot=-1;return false;}
  m.reasons.push_back("last submesh with "+std::to_string(mats)+" materials for "+std::to_string(subs)+" submeshes: standard Unity draws the extra materials over it too; unverified on this engine");return false;};
 // The material slot named for the part (Mat_Hair, Mat_Body, ...), when exactly one is.
 auto slotByName=[&](const Renderer& R,const std::string& hint,bool& alias){int hit=-1;size_t best=0;unsigned tie=0;
  for(size_t i=0;i<R.materials.size();i++){bool a=false;auto sc=partScore(hint,materialPart(R.materials[i].name),&a);if(!sc)continue;if(sc>best){best=sc;hit=int(i);tie=0;alias=a;}else if(sc==best)tie++;}
  return tie?-1:hit;};
 // The renderer the other segments of a hash name, when they name exactly one: a PC index buffer is one Android mesh (assumed).
 auto groupRenderer=[&](const std::string& hash){std::set<int> named;for(auto& x:d.rules)if(x.hash==hash&&x.kind=="segment"){auto h=nameHits(x);if(h.size()==1)named.insert(h[0]);}return named.size()==1?*named.begin():-1;};
 // The mesh this device's upload of an index-buffer hash was tied to (hash-map.json), when one renderer shows it now.
 auto deviceRenderer=[&](const std::string& hash,std::string& why)->int{if(!device)return -1;auto it=device->byIb.find(pcdb::lower(hash));if(it==device->byIb.end())return -1;const auto& e=it->second;std::vector<int> hit;
  for(size_t i=0;i<rs.size();i++){if(!comparable(i)||rs[i].meshName!=e.mesh||rs[i].vertices!=e.vertices||rs[i].layout.subs.size()!=e.subs.size())continue;bool same=true;
   for(size_t k=0;same&&k<e.subs.size();k++){same=rs[i].layout.subs[k].start==e.subs[k].first&&rs[i].layout.subs[k].count==e.subs[k].second;}if(same)hit.push_back(int(i));}
  if(hit.size()==1){why="index-buffer hash "+e.ib+" was reproduced on this device by the upload of mesh "+e.mesh+" ("+std::to_string(e.vertices)+" vertices, "+std::to_string(pcdb::indices(e.subs))+" indices"+(e.build.empty()?"":", build "+e.build.substr(0,8))+"); only "+label(rs[size_t(hit[0])])+" shows that mesh";return hit[0];}
  why=hit.empty()?"":"index-buffer hash "+e.ib+" was tied on this device to mesh "+e.mesh+", which "+std::to_string(hit.size())+" renderers show";return -1;};
 // A segment: rule r's draw starting at first (match_first_index, or the PC's only draw start of the hash).
 auto segment=[&](const Rule& r,size_t k,long long first)->Match{Match m;m.rule=k;
  auto names=nameHits(r);std::string nameText;for(int i:names)nameText+=(nameText.empty()?"":", ")+label(rs[size_t(i)]);
  std::vector<long long> group=firsts[r.hash];if(std::find(group.begin(),group.end(),first)==group.end()){group.push_back(first);std::sort(group.begin(),group.end());}
  if(r.pcHasIb){m.reasons.push_back("PC: hash "+r.hash+" is the index buffer of "+r.pcIb.character+" "+r.pcIb.component+", draws start at "+joined(r.pcIb.indexes));
   // A draw start the PC has: the hash's group is the PC one (a mod's other match_first_index that is no PC draw start,
   // stale from another game version, is that rule's own problem, not this one's).
   if(std::find(r.pcIb.indexes.begin(),r.pcIb.indexes.end(),uint64_t(first))!=r.pcIb.indexes.end()){group.clear();for(auto i:r.pcIb.indexes)group.push_back((long long)i);std::sort(group.begin(),group.end());}
   else m.reasons.push_back(r.pcNote);}
  // The PC index count of this draw, where the database has it.
  long long pcCount=-1;if(r.pcHasIb&&r.pcIb.counts.size()==r.pcIb.indexes.size())for(size_t i=0;i<r.pcIb.indexes.size();i++)if((long long)r.pcIb.indexes[i]==first)pcCount=(long long)r.pcIb.counts[i];
  std::string devWhy;int dev=deviceRenderer(r.hash,devWhy);if(dev<0&&!devWhy.empty())m.reasons.push_back(devWhy);
  // The renderer by name, or by the hash's other segments. When its index layout cannot be compared with the PC one
  // (not readable, or a lower LOD with an index buffer of its own), the material names and the segment order decide;
  // never more than likely.
  int target=names.size()==1?names[0]:-1;bool viaGroup=false;if(target<0&&names.empty()){target=groupRenderer(r.hash);viaGroup=target>=0;}
  if(dev<0&&target>=0&&!comparable(size_t(target))){const auto& R=rs[size_t(target)];size_t pos=size_t(std::find(group.begin(),group.end(),first)-group.begin());
   size_t subs=R.layout.submeshes>=0?size_t(R.layout.submeshes):R.submeshes;m.renderer=target;
   m.reasons.push_back(R.known()?label(R)+" shows a lower LOD ("+R.meshName+"): its index layout is its own, not comparable with the PC full mesh":"indexStart not readable on "+label(R)+(R.layout.status.empty()?"":" ("+R.layout.status+")"));
   m.reasons.push_back(viaGroup?"the part name '"+r.hint+"' names no renderer; another segment of hash "+r.hash+" names "+label(R)+" (one PC index buffer taken as one Android mesh)":"the part name '"+r.hint+"' names it");
   m.reasons.push_back("segment "+std::to_string(pos)+" of "+std::to_string(group.size())+", "+std::to_string(subs)+" submeshes, "+std::to_string(R.materials.size())+" materials");
   bool counts=group.size()==subs&&subs==R.materials.size(),alias=false;int byName=slotByName(R,r.hint,alias);
   if(byName>=0){m.slot=byName;m.reasons.push_back("material "+R.materials[size_t(byName)].name+" (slot "+std::to_string(byName)+") is named for the part"+(alias?" (GIMI Head = game Hair, a naming convention)":""));
    if(counts&&size_t(byName)==pos){m.submesh=byName;m.verdict=Verdict::Likely;m.reasons.push_back("material name and segment order agree (still no index evidence)");}
    else if(counts){m.verdict=Verdict::Ambiguous;m.reasons.push_back("material name says slot "+std::to_string(byName)+", segment order says "+std::to_string(pos));}
    else{m.verdict=Verdict::Likely;m.reasons.push_back("counts differ; slot by material name only");}}
   else if(counts){m.submesh=int(pos);m.slot=int(pos);m.verdict=Verdict::Likely;m.reasons.push_back("counts agree: slot "+std::to_string(pos)+" by order only (order alone is not proof)");}
   else{m.verdict=Verdict::Ambiguous;m.reasons.push_back("counts differ and no material is named for the part: no slot");}
   if(R.lod)m.reasons.push_back(R.known()?std::string("read again when the full mesh is shown"):"renderer shows a lower LOD ("+R.meshName+"): read again when the full mesh is shown");
   return m;}
  std::vector<std::pair<int,int>> exact,inside;bool unknown=false;
  for(size_t i=0;i<rs.size();i++){if(!comparable(i)){unknown=true;continue;}const auto& subs=rs[i].layout.subs;
   for(size_t j=0;j<subs.size();j++){if(subs[j].start==uint64_t(first))exact.push_back({int(i),int(j)});else if(uint64_t(first)>subs[j].start&&uint64_t(first)<uint64_t(subs[j].start)+subs[j].count)inside.push_back({int(i),int(j)});}}
  auto groupExact=[&](int i){unsigned n=0;for(long long f:group)for(auto& s:rs[size_t(i)].layout.subs)if(s.start==uint64_t(f)){n++;break;}return n;};
  bool pcGroup=r.pcHasIb&&std::find(r.pcIb.indexes.begin(),r.pcIb.indexes.end(),uint64_t(first))!=r.pcIb.indexes.end();
  const std::string starts=pcGroup?"PC draw starts":"match_first_index values",next1=pcGroup?"PC draw start":"match_first_index";
  bool identified=false;   // the index buffer itself is known to be this renderer's (device upload, or the PC draw count)
  // This device's upload of the hash names the mesh: nothing else needs to decide.
  if(dev>=0){std::vector<std::pair<int,int>> mine;for(auto& h:exact)if(h.first==dev)mine.push_back(h);
   if(!mine.empty()){m.reasons.push_back(devWhy);exact=mine;identified=true;}
   else m.reasons.push_back(devWhy+", but none of its submeshes starts at "+std::to_string(first));}
  // Index 0 starts a submesh on every renderer: the other segments of the hash decide (the mod's and the PC's), when
  // they all start submeshes of only one of them (index starts, not order); then the PC index count of the draw;
  // otherwise the part name, which only makes it likely.
  if(exact.size()>1&&group.size()>1){std::vector<std::pair<int,int>> whole;for(auto& h:exact)if(groupExact(h.first)==group.size())whole.push_back(h);
   if(whole.size()==1){m.reasons.push_back(std::to_string(exact.size())+" renderers have a submesh starting at "+std::to_string(first)+"; only "+label(rs[size_t(whole[0].first)])+" has all "+std::to_string(group.size())+" of the hash's "+starts+" as submesh starts");exact=whole;}}
  if(exact.size()>1&&pcCount>=0){std::vector<std::pair<int,int>> fit;for(auto& h:exact){const auto& R=rs[size_t(h.first)];if(R.layout.subs[size_t(h.second)].count==uint64_t(pcCount)&&R.layout.subs.size()==r.pcIb.indexes.size())fit.push_back(h);}
   if(fit.size()==1){m.reasons.push_back(std::to_string(exact.size())+" renderers have a submesh starting at "+std::to_string(first)+"; only "+label(rs[size_t(fit[0].first)])+" has "+std::to_string(r.pcIb.indexes.size())+" submeshes and "+std::to_string(pcCount)+" indices in it, as the PC draw");exact=fit;identified=true;}}
  if(exact.size()>1){std::vector<int> pick=names;bool byGroup=false;if(pick.empty()){int g=groupRenderer(r.hash);if(g>=0){pick.push_back(g);byGroup=true;}}
   std::vector<std::pair<int,int>> named;for(auto& h:exact)if(std::find(pick.begin(),pick.end(),h.first)!=pick.end())named.push_back(h);
   if(named.size()==1){m.reasons.push_back(std::to_string(exact.size())+" renderers have a submesh starting at "+std::to_string(first)+"; "+(byGroup?"the hash's other segments name ":"the part name picks ")+label(rs[size_t(named[0].first)]));exact=named;m.verdict=Verdict::Likely;}}
  if(exact.size()==1){auto [i,j]=exact[0];const auto& R=rs[size_t(i)];const auto& sm=R.layout.subs[size_t(j)];m.renderer=i;m.submesh=j;m.slot=j;
   m.reasons.push_back("submesh "+std::to_string(j)+" indexStart "+std::to_string(sm.start)+" = "+(r.first>=0?"match_first_index":"the PC draw start"));
   unsigned all=groupExact(i);bool complete=all==group.size();m.reasons.push_back(std::to_string(all)+" of the hash's "+std::to_string(group.size())+" "+starts+" are submesh starts of this renderer");
   auto next=std::upper_bound(group.begin(),group.end(),first);bool countOk=true;
   if(next!=group.end()){long long pc=*next-first;if(pc==(long long)sm.count)m.reasons.push_back("index count "+std::to_string(sm.count)+" = distance to the next "+next1);
    else{countOk=false;m.reasons.push_back("index count "+std::to_string(sm.count)+" vs "+std::to_string(pc)+" to the next "+next1);}}
   if(pcCount>=0){if(pcCount==(long long)sm.count)m.reasons.push_back("index count "+std::to_string(sm.count)+" = the PC draw's");else{countOk=false;m.reasons.push_back("index count "+std::to_string(sm.count)+" vs the PC draw's "+std::to_string(pcCount));}}
   bool merged=false;for(long long f:group)if(f>first&&uint64_t(f)<uint64_t(sm.start)+sm.count){merged=true;bool own=ruleAt(f,r.hash)!=nullptr;bool same=own&&sameTextures(first,f,r.hash);
    m.reasons.push_back("PC segment "+std::to_string(f)+" lies inside this submesh too (PC draws merged into one slot): "+(!own?"a PC draw the mod leaves as it is, one material cannot carry both":same?"same textures, one material covers both":"different textures, one material cannot carry both"));if(!same)countOk=false;}
   bool slotSure=slotOf(R,j,m);
   {bool alias=false;int byName=slotByName(R,r.hint,alias);if(byName==j)m.reasons.push_back("material "+R.materials[size_t(j)].name+" is named for the part"+(alias?" (GIMI Head = game Hair)":""));
    else if(byName>=0){m.reasons.push_back("but material "+R.materials[size_t(byName)].name+" (slot "+std::to_string(byName)+") is the one named for the part");slotSure=false;}}
   if(!names.empty()&&std::find(names.begin(),names.end(),i)==names.end()){m.reasons.push_back("the part name '"+r.hint+"' points at "+nameText+" instead");if(!identified)slotSure=false;}
   else if(!names.empty())m.reasons.push_back("the part name '"+r.hint+"' agrees");
   if(m.verdict!=Verdict::Likely)m.verdict=complete&&countOk&&slotSure?Verdict::Confirmed:(merged&&!countOk?Verdict::Ambiguous:Verdict::Likely);
   return m;}
  if(exact.size()>1){m.verdict=Verdict::Ambiguous;for(auto& h:exact)m.candidates.push_back(label(rs[size_t(h.first)])+" submesh "+std::to_string(h.second));m.reasons.push_back("several renderers have a submesh starting at "+std::to_string(first)+" and the part name does not decide");return m;}
  if(!inside.empty()){m.verdict=Verdict::Ambiguous;for(auto& h:inside){m.candidates.push_back(label(rs[size_t(h.first)])+" submesh "+std::to_string(h.second));}
   m.reasons.push_back((r.first>=0?"match_first_index ":"the PC draw start ")+std::to_string(first)+" lies inside an Android submesh: the PC segment was merged with others into one submesh / material slot");
   // The renderer where the hash's other segments start submeshes is the one it most likely went into.
   std::vector<std::pair<int,int>> fit;unsigned most=0;for(auto& h:inside){unsigned n=groupExact(h.first);if(n>most){most=n;fit.clear();}if(n&&n==most)fit.push_back(h);}
   if(fit.size()==1){m.renderer=fit[0].first;m.submesh=fit[0].second;m.slot=fit[0].second;m.reasons.push_back("most of the hash's other segments ("+std::to_string(most)+") start submeshes of "+label(rs[size_t(m.renderer)])+": most likely merged into its submesh "+std::to_string(m.submesh));}
   return m;}
  if(names.size()>1){m.verdict=Verdict::Ambiguous;for(int i:names)m.candidates.push_back(label(rs[size_t(i)]));m.reasons.push_back("no submesh starts at "+std::to_string(first)+" where the layout is comparable, and the part name fits several renderers");return m;}
  m.reasons.push_back(std::string("no renderer has a submesh starting at ")+std::to_string(first)+(unknown?" among those whose layout is comparable":"")+(target<0?"; the part name '"+r.hint+"' names no renderer":"; "+label(rs[size_t(target)])+(viaGroup?" (by the hash's other segments)":" (by name)")+" starts none there"));
  m.verdict=Verdict::None;m.reasons.push_back("so either Android's index order differs from the PC one, or the hash belongs to another character / outfit");return m;};
 // A texture hash the PC database places: per draw it is in, the renderer of that component and the draw's slot.
 // The renderer by this device's upload of the component's index buffer, by submesh starts equal to all its PC draw
 // starts (more than one), by the PC index count of its only draw, or by the segment rules of the same hash; a
 // component without an index buffer of its own in the database (Face) by its name on one full-LOD renderer. A name
 // for a component that has an index buffer makes it likely only.
 auto texture=[&](const Rule& r,size_t k,const PcPart& u)->Match{Match m;m.rule=k;
  m.reasons.push_back("PC: texture hash "+r.hash+" is "+u.character+" "+(u.component.empty()?std::string("(unnamed component)"):u.component)+" draw "+std::to_string(u.submesh)+" "+u.slot);
  int target=-1;bool strong=false;std::string how;
  if(!u.ib.empty()){std::string why;int dev=deviceRenderer(u.ib,why);if(dev>=0){target=dev;strong=true;how=why;}else if(!why.empty())m.reasons.push_back(why);
   if(target<0&&u.indexes.size()>1){std::vector<int> hit;for(size_t i=0;i<rs.size();i++){if(!comparable(i))continue;std::vector<std::pair<uint32_t,uint32_t>> subs;for(auto& s:rs[i].layout.subs)subs.push_back({uint32_t(s.start),uint32_t(s.count)});if(pcdb::startsEqual(subs,u.indexes))hit.push_back(int(i));}
    if(hit.size()==1){target=hit[0];strong=true;how="the submesh starts of "+label(rs[size_t(target)])+" are the PC draw starts "+joined(u.indexes)+" of "+u.component+" (no other renderer's are)";}
    else if(hit.size()>1)m.reasons.push_back(std::to_string(hit.size())+" renderers have the PC draw starts "+joined(u.indexes));}
   if(target<0&&u.indexes.size()==1&&u.counts.size()==1){std::vector<int> hit;for(size_t i=0;i<rs.size();i++)if(comparable(i)&&rs[i].layout.subs.size()==1&&rs[i].layout.subs[0].start==u.indexes[0]&&rs[i].layout.subs[0].count==u.counts[0])hit.push_back(int(i));
    if(hit.size()==1){target=hit[0];strong=true;how=label(rs[size_t(target)])+" is the only renderer with one submesh of "+std::to_string(u.counts[0])+" indices, as the PC draw of "+u.component;}}
   if(target<0){for(auto& x:out){const Rule& o=d.rules[x.rule];if(pcdb::lower(o.hash)!=u.ib||x.renderer<0||(x.verdict!=Verdict::Confirmed&&x.verdict!=Verdict::Likely))continue;
     if(target<0||target==x.renderer){target=x.renderer;strong=strong||x.verdict==Verdict::Confirmed;how="the rule "+o.section+" on the index buffer "+u.ib+" matched "+label(rs[size_t(x.renderer)])+" ("+verdictName(x.verdict)+")";}else{target=-2;break;}}
    if(target==-2){target=-1;strong=false;m.reasons.push_back("the rules on index buffer "+u.ib+" matched several renderers");}}
   if(target<0&&!u.component.empty()){auto h=hitsFor(pcdb::lower(u.component));std::vector<int> full;for(int i:h)if(!rs[size_t(i)].lod)full.push_back(i);if(full.size()==1){target=full[0];how="the component name "+u.component+" names "+label(rs[size_t(target)])+" (a name only)";}}}
  else if(!u.component.empty()){auto h=hitsFor(pcdb::lower(u.component));std::vector<int> full;for(int i:h)if(!rs[size_t(i)].lod)full.push_back(i);
   if(full.size()==1){target=full[0];strong=true;how="the component "+u.component+" has no index buffer of its own in the PC database; its name names "+label(rs[size_t(target)])+", the only full-LOD renderer it names";}
   else if(full.size()>1){for(int i:full)m.candidates.push_back(label(rs[size_t(i)]));m.reasons.push_back("the component name "+u.component+" names several renderers");}}
  if(target<0){m.verdict=m.candidates.empty()?Verdict::None:Verdict::Ambiguous;m.reasons.push_back("no renderer here is "+u.character+" "+(u.component.empty()?std::string("(unnamed)"):u.component)+(u.ib.empty()?"":" (index buffer "+u.ib+")"));return m;}
  const auto& R=rs[size_t(target)];m.renderer=target;m.reasons.push_back(how);
  if(R.lod){m.verdict=Verdict::Likely;m.reasons.push_back(label(R)+" shows a lower LOD ("+R.meshName+"): written from the full mesh only");return m;}
  size_t subs=R.layout.submeshes>=0?size_t(R.layout.submeshes):R.submeshes;
  if(u.submesh>=subs){m.verdict=Verdict::Ambiguous;m.reasons.push_back("the PC draw "+std::to_string(u.submesh)+" but "+label(R)+" has "+std::to_string(subs)+" submeshes");return m;}
  m.submesh=int(u.submesh);m.slot=int(u.submesh);bool slotSure=slotOf(R,int(u.submesh),m);
  if(!u.indexes.empty()&&R.known()&&u.submesh<u.indexes.size()&&u.submesh<R.layout.subs.size()){if(R.layout.subs[u.submesh].start==u.indexes[u.submesh])m.reasons.push_back("its submesh "+std::to_string(u.submesh)+" starts at "+std::to_string(u.indexes[u.submesh])+", as the PC draw");
   else{slotSure=false;m.reasons.push_back("its submesh "+std::to_string(u.submesh)+" starts at "+std::to_string(R.layout.subs[u.submesh].start)+", the PC draw at "+std::to_string(u.indexes[u.submesh]));}}
  m.verdict=strong&&slotSure?Verdict::Confirmed:Verdict::Likely;return m;};
 // Segments first (texture hashes on a component follow what its index buffer matched), then texture hashes, then
 // rules without match_first_index for a hash others split: they follow what the segments matched.
 std::vector<size_t> order;for(int pass=0;pass<3;pass++)for(size_t k=0;k<d.rules.size();k++){auto& kd=d.rules[k].kind;int p=kd=="whole-ib"?2:kd=="hash-unknown"?1:0;if(p==pass)order.push_back(k);}
 for(size_t k:order){const Rule& r=d.rules[k];Match m;m.rule=k;
  if(r.kind=="control"){m.verdict=Verdict::Skipped;m.reasons.push_back("binds no texture (sets variables only)");out.push_back(m);continue;}
  if(r.kind=="segment"){out.push_back(segment(r,k,r.first));continue;}
  if(r.kind=="whole-ib"){m.reasons.push_back("no match_first_index: 3DMigoto applies it to every draw of hash "+r.hash);
   std::set<int> hit;for(auto& x:out)if(x.renderer>=0&&d.rules[x.rule].hash==r.hash&&(x.verdict==Verdict::Confirmed||x.verdict==Verdict::Likely))hit.insert(x.renderer);
   if(hit.size()==1){m.renderer=*hit.begin();m.verdict=Verdict::Likely;m.reasons.push_back("the hash's segments match "+label(rs[size_t(m.renderer)])+": every slot of it");}
   else{m.verdict=hit.empty()?Verdict::None:Verdict::Ambiguous;for(int i:hit)m.candidates.push_back(label(rs[size_t(i)]));m.reasons.push_back(hit.empty()?"the hash's segments match no renderer":"the hash's segments match several renderers");}
   out.push_back(m);continue;}
  // hash-unknown, which the PC database may name: an index buffer (every draw of it: one match per PC draw start) or
  // a texture (one match per draw it is in).
  if(r.pcHasIb&&!r.pcIb.indexes.empty()){for(auto f:r.pcIb.indexes){auto x=segment(r,k,(long long)f);x.reasons.insert(x.reasons.begin(),"no match_first_index; PC: "+std::to_string(r.pcIb.indexes.size())+" draw(s) of this index buffer, this one at "+std::to_string(f));out.push_back(x);}continue;}
  if(!r.pcTextures.empty()){std::set<std::pair<int,int>> taken;for(auto& u:r.pcTextures){auto x=texture(r,k,u);if(!r.pcNote.empty())x.reasons.push_back(r.pcNote);if(x.renderer>=0&&x.slot>=0&&!taken.insert({x.renderer,x.slot}).second)continue;out.push_back(x);}continue;}
  // Neither: an IB, VB or texture hash GIBox cannot tell. Name and texture size give candidates.
  auto names=nameHits(r);
  m.reasons.push_back(r.evidence.empty()?"hash kind unknown":r.evidence);m.reasons.push_back(d.pc.empty()?"a 3DMigoto hash cannot be recomputed on Android and the PC hash database is not installed; no confirmation possible":"not in the PC hash database for this character ("+d.pc+"); a 3DMigoto hash cannot be recomputed on Android; no confirmation possible");
  for(int i:names)m.candidates.push_back(label(rs[size_t(i)])+" (by part name)");
  if(r.textureName||!names.empty()){const Resource* mine=nullptr;for(auto& b:r.textures){auto it=d.resources.find(b.resource);if(it!=d.resources.end()&&it->second.width>0){mine=&it->second;break;}}
   if(mine){unsigned n=0;for(size_t i=0;i<rs.size()&&n<8;i++)for(size_t s=0;s<rs[i].materials.size()&&n<8;s++)for(auto& t:rs[i].materials[s].textures)
     if(t.texture&&t.width==std::to_string(mine->width)&&t.height==std::to_string(mine->height)&&n<8){m.candidates.push_back(label(rs[i])+" slot "+std::to_string(s)+" "+t.property+"="+t.name+" (same size "+t.width+"x"+t.height+")");n++;}
    if(!n)m.reasons.push_back("no game texture of the mod texture's size "+std::to_string(mine->width)+"x"+std::to_string(mine->height)+" on this character (Android textures may be smaller)");}}
  m.verdict=m.candidates.empty()?Verdict::None:Verdict::Ambiguous;out.push_back(m);}
 // Several rules on one slot: written together when they set different textures (a texture hash's light map next to
 // a segment's diffuse); two that set the same one cannot both be put on one material unless their conditions keep
 // them apart.
 auto roles=[&](size_t rule){std::set<std::string> s;for(auto& b:d.rules[rule].textures)s.insert(b.role);return s;};
 // Only what may be written counts: an ambiguous guess on the slot is not.
 std::map<std::tuple<int,int>,std::vector<size_t>> bySlot;for(size_t i=0;i<out.size();i++)if(out[i].renderer>=0&&out[i].slot>=0&&(out[i].verdict==Verdict::Confirmed||out[i].verdict==Verdict::Likely))bySlot[{out[i].renderer,out[i].slot}].push_back(i);
 for(auto& kv:bySlot)if(kv.second.size()>1){for(size_t i:kv.second){std::string clash,beside;auto mine=roles(out[i].rule);
   for(size_t j:kv.second){if(j==i||out[j].rule==out[i].rule)continue;auto theirs=roles(out[j].rule);bool overlap=mine.count("unknown")||theirs.count("unknown");for(auto& x:mine)overlap=overlap||theirs.count(x);
    (overlap?clash:beside)+=std::string((overlap?clash:beside).empty()?"":", ")+d.rules[out[j].rule].section;}
   if(!clash.empty()){std::string all=d.rules[out[i].rule].section+", "+clash;out[i].reasons.push_back("rules on the same slot: "+all);if(out[i].verdict==Verdict::Confirmed)out[i].verdict=Verdict::Likely;}
   if(!beside.empty())out[i].reasons.push_back("also on this slot, setting other textures: "+beside);}}
 std::stable_sort(out.begin(),out.end(),[](const Match& a,const Match& b){return a.rule<b.rule;});
 return out;}

inline std::string matchLine(const Doc& d,const Match& m,const std::vector<Renderer>& rs){const Rule& r=d.rules[m.rule];
 std::string s=d.name+": "+r.section+" hash="+(r.hash.empty()?"-":r.hash)+" match_first_index="+(r.first>=0?std::to_string(r.first):"-")+" -> "+verdictName(m.verdict);
 const Material* mat=nullptr;
 if(m.renderer>=0){const auto& R=rs[size_t(m.renderer)];s+=" renderer="+label(R)+" mesh="+R.meshName;if(m.submesh>=0)s+=" submesh="+std::to_string(m.submesh);
  if(m.slot>=0){s+=" slot="+std::to_string(m.slot);if(size_t(m.slot)<R.materials.size()){mat=&R.materials[size_t(m.slot)];s+=" material="+mat->name;}}}
 std::string why;for(auto& x:m.reasons)why+=(why.empty()?"":"; ")+x;s+=" | why: "+why;
 if(!m.candidates.empty()){std::string c;for(auto& x:m.candidates)c+=(c.empty()?"":"; ")+x;s+=" | candidates: "+c;}
 if(m.verdict!=Verdict::Skipped)s+=" | textures (phase-2 plan, not applied): "+plan(d,r,mat);
 return s;}
inline std::string ruleLine(const Doc& d,const Rule& r){std::string t;for(auto& b:r.textures)t+=(t.empty()?"":", ")+b.slot+"="+(b.resource.empty()?"null":b.resource)+"["+b.role+"]"+(b.condition.empty()?"":" if "+b.condition)+(b.via.empty()?"":" via "+b.via);
 std::string x;for(auto& e:r.external)x+=(x.empty()?"":", ")+e;std::string dr;for(auto& e:r.draws)dr+=(dr.empty()?"":", ")+e;
 return d.name+": "+r.section+" hash="+(r.hash.empty()?"-":r.hash)+" match_first_index="+(r.first>=0?std::to_string(r.first):"-")+" priority="+std::to_string(r.priority)+" kind="+r.kind+" part="+r.hint+(r.slotLetter>=0?std::string(" letter=")+char('A'+r.slotLetter):"")
  +" textures=["+t+"]"+(dr.empty()?"":" drawindexed=["+dr+"]")+(x.empty()?"":" external=["+x+"]");}
inline std::string rendererLine(const Renderer& r,unsigned character){
 return "character="+std::to_string(character)+" renderer="+label(r)+" mesh="+r.meshName+"#"+hexOf(r.mesh)+" lod="+(r.lod?"lower (by name)":"full (by name)")+" vertices="+std::to_string(r.vertices)
  +" submeshes(native)="+std::to_string(r.submeshes)+" subMeshCount="+orDash(r.layout.submeshCount)+" vertexCount="+orDash(r.layout.vertexCount)+" bones="+std::to_string(r.bones)+" readable="+orDash(r.layout.readable)+" indexFormat="+orDash(r.layout.indexFormat)
  +" materials="+std::to_string(r.materials.size())+" avatar="+(r.avatar.empty()?"-":r.avatar)+(r.takenBy.empty()?"":" taken-over-by="+r.takenBy+" (game mesh/materials read)")+(r.swap.empty()?"":" texture-swap="+r.swap)
  +(r.layout.status=="read"?" layout-source="+orDash(r.layout.source):" layout="+r.layout.status)+(r.error.empty()?"":" error="+r.error);}
inline std::vector<std::string> submeshLines(const Renderer& r){std::vector<std::string> out;
 for(size_t i=0;i<r.layout.subs.size();i++){auto& s=r.layout.subs[i];std::string slot=i<r.materials.size()?std::to_string(i)+" ("+r.materials[i].name+")":"none";
  out.push_back(label(r)+" submesh="+std::to_string(i)+(s.known?" indexStart="+std::to_string(s.start)+" indexCount="+std::to_string(s.count):" range="+s.error)+" baseVertex="+s.base+" topology="+s.topology+" material-slot-by-position="+slot);}
 return out;}
inline std::vector<std::string> materialLines(const Renderer& r){std::vector<std::string> out;
 for(size_t i=0;i<r.materials.size();i++){auto& m=r.materials[i];std::string t;for(auto& x:m.textures)t+=(t.empty()?"":", ")+textureText(x);std::string shared;for(auto& x:m.sharedWith)shared+=(shared.empty()?"":",")+x;
  out.push_back(label(r)+" slot="+std::to_string(i)+" material="+m.name+"#"+hexOf(m.material)+" shader="+m.shader+(shared.empty()?"":" shared-with="+shared)+" textures=["+t+"]"+(m.error.empty()?"":" error="+m.error));}
 return out;}
// ---------------------------------------------------------------- texture-only mods (0.7.52)
// models/<name>/texture.json (TextureMod.java): the rules (the diagnostic document as "diag"), the variants and the
// decoded textures (textures/<n>.rgba, RGBA8, rows top to bottom as on the PC).
struct ModTexture {std::string resource,file;unsigned width=0,height=0;};
struct TextureMod {std::string name,generation;unsigned character=0;std::map<std::string,std::vector<int>> vars;std::map<std::string,int> defaults;std::vector<ModTexture> textures;Doc doc;};
inline TextureMod parseMod(const std::string& text,const std::string& dir){Json j=JsonParser(text).parse();
 require(j.type==Json::Object&&str(j,"format")=="gibox-texture-mod","not a GIBox texture-only mod");require(num(j,"version",0)==1,"texture-only mod version "+std::to_string(num(j,"version",0))+" is not supported");
 TextureMod m;m.name=str(j,"name");m.generation=str(j,"generation");require(!m.name.empty()&&!m.generation.empty(),"texture-only mod name / generation");
 auto c=num(j.at("character"),"id",0);require(c>=10000000&&c<=10001999,"texture-only mod character");m.character=unsigned(c);
 const auto& v=j.at("variants");require(v.type==Json::Object,"texture-only mod variants");const auto& d=v.at("default");
 if(v.at("vars").type==Json::Array)for(auto& x:v.at("vars").array){auto n=str(x,"name");require(n.size()>1&&n[0]=='$'&&!m.vars.count(n),"texture-only mod variable");auto& vals=m.vars[n];
  const auto& a=x.at("values");require(a.type==Json::Array&&!a.array.empty()&&a.array.size()<=64,"texture-only mod values "+n);for(auto& k:a.array){require(k.type==Json::Number&&std::floor(k.number)==k.number&&std::fabs(k.number)<=1000000,"texture-only mod value "+n);vals.push_back(int(k.number));}
  auto def=num(d,n.c_str(),vals[0]);require(std::find(vals.begin(),vals.end(),int(def))!=vals.end(),"texture-only mod default "+n);m.defaults[n]=int(def);}
 const auto& t=j.at("textures");require(t.type==Json::Array&&t.array.size()<=256,"texture-only mod textures");
 for(auto& x:t.array){ModTexture mt;mt.resource=str(x,"resource");mt.file=str(x,"file");auto w=num(x,"width",0),h=num(x,"height",0);
  bool safe=mt.file.size()>14&&mt.file.compare(0,9,"textures/")==0&&mt.file.compare(mt.file.size()-5,5,".rgba")==0&&mt.file.find_first_not_of("0123456789",9)==mt.file.size()-5;
  require(!mt.resource.empty()&&safe&&w>0&&h>0&&w<=4096&&h<=4096,"texture-only mod texture "+mt.resource);mt.width=unsigned(w);mt.height=unsigned(h);m.textures.push_back(mt);}
 const auto& diag=j.at("diag");require(diag.type==Json::Object,"texture-only mod rules (diag)");m.doc=parseDoc(diag,dir+"/texture.json");m.doc.name=m.name;m.doc.character=m.character;
 return m;}

// A condition as TextureDiag writes it ("$a == 1 && !($b == 2)"; || and parentheses too): variables and integer
// numbers compared with == != < > <= >=. Anything else, or a variable the mod does not declare, cannot be evaluated
// here: false, with why set (so the binding is reported, not written).
inline bool condition(const std::string& text,const std::map<std::string,int>& vars,std::string& why){
 if(text.empty()){return true;}size_t p=0;bool bad=false;
 auto ws=[&]{while(p<text.size()&&text[p]==' ')p++;};auto take=[&](const char* t){ws();size_t n=std::strlen(t);if(text.compare(p,n,t)==0){p+=n;return true;}return false;};
 auto fail=[&](const std::string& w){if(!bad){bad=true;why=w;}return 0.0;};
 std::function<double()> expr;
 auto operand=[&]()->double{ws();if(p<text.size()&&text[p]=='$'){size_t q=p+1;while(q<text.size()&&(std::isalnum((unsigned char)text[q])||text[q]=='_'))q++;auto n=text.substr(p,q-p);p=q;auto it=vars.find(n);if(it==vars.end())return fail("unknown variable "+n);return it->second;}
  size_t q=p;if(q<text.size()&&(text[q]=='-'||text[q]=='+'))q++;size_t digits=q;while(q<text.size()&&std::isdigit((unsigned char)text[q]))q++;if(q==digits)return fail("not a number at '"+text.substr(p)+"'");double v=std::stod(text.substr(p,q-p));p=q;return v;};
 std::function<double()> unary=[&]()->double{if(take("!")&&!take("=")){return unary()!=0?0:1;}
  ws();if(p<text.size()&&text[p]=='('){p++;double v=expr();if(!take(")"))return fail("missing )");return v;}
  double a=operand();for(const char* op:{"==","!=","<=",">=","<",">"}){if(take(op)){double b=operand();std::string o=op;return o=="=="?a==b:o=="!="?a!=b:o=="<="?a<=b:o==">="?a>=b:o=="<"?a<b:a>b;}}return a;};
 auto conj=[&]()->double{double v=unary();while(take("&&")){double b=unary();v=(v!=0&&b!=0)?1:0;}return v;};
 expr=[&]()->double{double v=conj();while(take("||")){double b=conj();v=(v!=0||b!=0)?1:0;}return v;};
 double v=expr();ws();if(!bad&&p!=text.size())fail("unexpected '"+text.substr(p)+"'");return !bad&&v!=0;}

// What one rule writes for these variable values: diffuse -> _MainTex, lightmap -> _LightMapTex, the last binding of a
// property winning (3DMigoto runs the lines in order; a null binding takes it off). Every other binding is reported.
struct Write {std::string property,resource,role,slot;bool linear=false;};
inline std::vector<Write> writes(const Rule& r,const std::map<std::string,int>& vars,std::vector<std::string>& skipped){
 std::map<std::string,Write> byProperty;std::vector<std::string> order;
 for(auto& b:r.textures){std::string why;if(!condition(b.condition,vars,why)){if(!why.empty())skipped.push_back(b.slot+" "+b.resource+": condition '"+b.condition+"' not evaluable ("+why+")");continue;}
  const char* prop=b.role=="diffuse"?"_MainTex":b.role=="lightmap"?"_LightMapTex":nullptr;
  if(!prop){skipped.push_back(b.slot+" "+(b.resource.empty()?"null":b.resource)+" ["+b.role+"]: no confirmed game property (待诊断)");continue;}
  if(b.resource.empty()){byProperty.erase(prop);continue;}
  if(!byProperty.count(prop)){order.push_back(prop);}byProperty[prop]={prop,b.resource,b.role,b.slot,b.role!="diffuse"};}
 std::vector<Write> out;for(auto& p:order){auto it=byProperty.find(p);if(it!=byProperty.end())out.push_back(it->second);}return out;}
// Several rules on one slot (a texture hash's light map next to a segment's diffuse): texture-hash rules first, then
// the others, each property taken by the last that sets it, so a rule on the index buffer wins over a texture hash
// (3DMigoto's draw-level override binds its own resource in that slot).
inline std::vector<size_t> planOrder(const Doc& d,std::vector<size_t> rules){std::stable_sort(rules.begin(),rules.end(),[&](size_t a,size_t b){auto t=[&](size_t k){return d.rules[k].kind=="hash-unknown"?0:1;};return t(a)<t(b);});return rules;}
inline std::vector<Write> writesFor(const Doc& d,const std::vector<size_t>& rules,const std::map<std::string,int>& vars,std::vector<std::string>& skipped){std::vector<Write> out;
 for(size_t k:planOrder(d,rules)){std::vector<std::string> sk;for(auto& w:writes(d.rules[k],vars,sk)){bool replaced=false;for(auto& o:out)if(o.property==w.property){o=w;replaced=true;}if(!replaced)out.push_back(w);}
  for(auto& x:sk)skipped.push_back(rules.size()>1?d.rules[k].section+": "+x:x);}
 return out;}
inline std::string sections(const Doc& d,const std::vector<size_t>& rules){std::string o;for(size_t k:planOrder(d,rules))o+=(o.empty()?"":" + ")+d.rules[k].section;return o;}
}  // namespace ms::texdiag
