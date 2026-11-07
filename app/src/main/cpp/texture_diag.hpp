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
#include "runtime.hpp"
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
 require(j.type==Json::Object&&str(j,"format")=="gibox-texture-diag","不是 GIBox 贴图诊断文件");require(num(j,"version",0)==1,"贴图诊断文件版本 "+std::to_string(num(j,"version",0))+" 不受支持");
 d.name=str(j,"name");d.ini=str(j,"ini");d.characterName=str(j,"characterName");auto c=num(j,"character",0);require(c==0||(c>=10000000&&c<=10001999),"贴图诊断文件的角色无效");d.character=unsigned(c);
 const auto& rules=j.at("rules");require(rules.type==Json::Array&&rules.array.size()<=4096,"贴图诊断文件的规则列表无效");
 for(auto& r:rules.array){require(r.type==Json::Object,"贴图诊断文件的规则无效");Rule x;x.section=str(r,"section");x.part=str(r,"part");x.hint=str(r,"hint");x.hash=str(r,"hash");x.kind=str(r,"kind");x.evidence=str(r,"hashEvidence");
  x.slotLetter=int(num(r,"slotLetter",-1));x.first=num(r,"first",-1);x.priority=int(num(r,"priority",0));x.textureName=r.at("textureName").type==Json::Bool&&r.at("textureName").boolean;
  require(!x.section.empty()&&!x.kind.empty(),"贴图诊断规则缺少段名或类型");
  const auto& t=r.at("textures");if(t.type==Json::Array)for(auto& b:t.array){require(b.type==Json::Object,"贴图绑定无效");x.textures.push_back({str(b,"slot"),str(b,"resource"),str(b,"role"),str(b,"condition"),str(b,"via")});}
  for(auto* k:{"external","conditions"}){const auto& a=r.at(k);if(a.type==Json::Array)for(auto& v:a.array)if(v.type==Json::String)(std::string(k)=="external"?x.external:x.conditions).push_back(v.string);}
  const auto& di=r.at("drawIndexed");if(di.type==Json::Array)for(auto& v:di.array)if(v.type==Json::Object)x.draws.push_back(str(v,"args")+(str(v,"condition").empty()?"":" if "+str(v,"condition")));
  d.rules.push_back(std::move(x));}
 const auto& res=j.at("resources");if(res.type==Json::Array)for(auto& r:res.array){if(r.type!=Json::Object)continue;Resource x;x.name=str(r,"name");x.file=str(r,"file");x.kind=str(r,"kind");x.format=str(r,"format");x.error=str(r,"error");
  x.exists=r.at("exists").type==Json::Bool&&r.at("exists").boolean;x.width=num(r,"width",-1);x.height=num(r,"height",-1);x.mips=num(r,"mips",-1);d.resources[x.name]=x;}
 const auto& vars=j.at("variables");if(vars.type==Json::Array)for(auto& v:vars.array)if(v.type==Json::Object){std::string vals;const auto& a=v.at("values");if(a.type==Json::Array)for(auto& n:a.array)if(n.type==Json::Number)vals+=(vals.empty()?"":",")+std::to_string((long long)n.number);
  d.variables.push_back(str(v,"name")+"="+std::to_string(num(v,"initial",0))+"（可选 ["+vals+"]"+(v.at("persist").boolean?"，持久":"")+"）");}
 const auto& others=j.at("otherInis");if(others.type==Json::Array)for(auto& o:others.array)if(o.type==Json::Object){std::string h;const auto& a=o.at("sharedHashes");if(a.type==Json::Array)for(auto& n:a.array)if(n.type==Json::String)h+=(h.empty()?"":",")+n.string;d.others.push_back(str(o,"ini")+(h.empty()?"":"（共用 hash "+h+"）"));}
 const auto& notes=j.at("notes");if(notes.type==Json::Array)for(auto& n:notes.array)if(n.type==Json::String)d.notes.push_back(n.string);
 return d;}
inline Doc parse(const std::string& text,const std::string& path){return parseDoc(JsonParser(text).parse(),path);}

// What the Runtime read of one renderer. A string field starting with "未采集:" was not available, with the reason.
// A value that could not be read, and why.
inline std::string missing(const std::string& why){return "未采集: "+why;}
// Per-submesh layout of a Mesh, from the engine's own Mesh getters (the Runtime's texDiagMesh).
struct Submesh {bool known=false;uint32_t start=0,count=0;std::string error;};
struct MeshLayout {std::string status,submeshCount,vertexCount,readable;int submeshes=-1;std::vector<Submesh> subs;};
struct Texture {std::string property;int id=0;Ptr texture=0;std::string name,cls,width,height,mips,format,status;};
struct Material {Ptr material=0;std::string name,shader,error;std::vector<Texture> textures;std::vector<std::string> sharedWith;};
struct Renderer {Ptr renderer=0,native=0,mesh=0;std::string name,meshName,avatar,takenBy,swap,error;bool lod=false;unsigned vertices=0,submeshes=0,bones=0;
 MeshLayout layout;std::vector<Material> materials;
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
 auto it=names.find(f);return std::to_string(f)+" ("+(it==names.end()?"不在本版本的 TextureFormat 中":it->second)+")";}
inline std::string hexOf(Ptr p){char b[24];std::snprintf(b,sizeof b,"0x%llx",(unsigned long long)p);return b;}
inline std::string label(const Renderer& r){return r.name+"#"+hexOf(r.native);}
// The game properties phase 2 would put a role on, among those the material has; "待诊断" when none or unknown.
inline const std::vector<const char*>& candidates(const std::string& role){
 static const std::vector<const char*> diffuse{"_MainTex"},light{"_LightMapTex","_LightMap","_LightmapTex","_Lightmap"},normal{"_BumpMap","_NormalMap","_BaseNormalMap","_NormalTex"},ramp{"_ShadowRampTex","_RampTex"},none{};
 if(role=="diffuse"){return diffuse;}if(role=="lightmap"){return light;}if(role=="normal"){return normal;}if(role=="ramp"){return ramp;}return none;}
inline std::string orDash(const std::string& s){return s.empty()?std::string("-"):s;}
inline std::string textureText(const Texture& t){return t.property+(t.texture?"="+(t.name.empty()?std::string("（无名称）"):t.name)+(t.width.empty()?"":" "+t.width+"x"+t.height)+(t.format.empty()?"":" "+t.format)+(t.mips.empty()?"":"，mip "+t.mips):std::string("=空"))+(t.status.empty()?"":" ["+t.status+"]");}
inline std::string plan(const Doc& d,const Rule& r,const Material* m){std::string out;
 for(auto& b:r.textures){std::string target="待诊断";
  if(m)for(auto* p:candidates(b.role)){for(auto& t:m->textures)if(t.property==p){target=std::string(p)+"（材质有此属性，当前为 "+(t.texture?t.name+(t.width.empty()?"":" "+t.width+"x"+t.height)+(t.format.empty()||t.format.find("未采集:")==0?"":" "+t.format):std::string("空"))+"）";break;}if(target!="待诊断")break;}
  if(target=="待诊断")target+=b.role=="unknown"?"（槽位用途未知）":m?"（材质上没有候选属性）":"（没有材质）";
  auto res=d.resources.find(b.resource);std::string size=res==d.resources.end()?"":res->second.width>=0?" "+std::to_string(res->second.width)+"x"+std::to_string(res->second.height)+" "+res->second.format:res->second.error.empty()?"":" ("+res->second.error+")";
  out+=(out.empty()?"":"；")+b.slot+" "+b.resource+size+" ["+b.role+"]"+(b.condition.empty()?"":" 条件 "+b.condition)+" → "+target;}
 return out.empty()?"没有贴图绑定":out;}

// The PC database's word on each rule (0.7.82): an index-buffer hash gives the component and all its draw starts, a
// texture hash (restricted to the PC characters the mod is for) the component, draw and slot it is in. A texture rule's
// "this" binding takes the role of that slot (a LightMap hash replaces the light map whatever its file is called).
inline std::string joined(const std::vector<uint64_t>& v){std::string o;for(auto x:v)o+=(o.empty()?"":",")+std::to_string(x);return "["+o+"]";}
inline void annotate(Doc& d,const pcdb::Db& db){std::vector<std::string> hashes;for(auto& r:d.rules)if(!r.hash.empty())hashes.push_back(r.hash);
 std::string how;auto chars=pcdb::charactersFor(db,d.character,hashes,&how);std::string names;for(auto& c:chars)names+=(names.empty()?"":"、")+c;
 d.pc="PC 数据库 "+db.commit.substr(0,7)+"："+(chars.empty()?std::string("没有对应的 PC 角色"):names)+"（"+how+"）";
 for(auto& r:d.rules){auto h=pcdb::lower(r.hash);r.pcHasIb=false;r.pcIb=PcPart{};r.pcTextures.clear();r.pcNote.clear();
  auto ib=db.byIb.find(h);if(ib!=db.byIb.end())for(size_t i:ib->second){auto& c=db.comps[i];if(!chars.empty()&&!chars.count(c.character))continue;r.pcHasIb=true;r.pcIb={c.character,c.name,c.ib,"","",0,c.indexes,c.counts};break;}
  if(r.pcHasIb&&r.first>=0&&std::find(r.pcIb.indexes.begin(),r.pcIb.indexes.end(),uint64_t(r.first))==r.pcIb.indexes.end())
   r.pcNote="match_first_index "+std::to_string(r.first)+" 不是 "+r.pcIb.character+" "+r.pcIb.component+" 的 PC 绘制起点 "+joined(r.pcIb.indexes)+"（可能是其他游戏版本的 MOD）";
  auto tx=db.byTexture.find(h);if(tx==db.byTexture.end()||chars.empty())continue;std::set<std::string> roles;
  for(auto& u:tx->second){auto& c=db.comps[u.component];if(!chars.count(c.character))continue;PcPart x{c.character,c.name,c.ib,u.slot,pcdb::role(u.slot),u.submesh,c.indexes,c.counts};
   bool dup=false;for(auto& q:r.pcTextures)dup=dup||(q.component==x.component&&q.ib==x.ib&&q.submesh==x.submesh&&q.slot==x.slot);if(!dup)r.pcTextures.push_back(x);roles.insert(x.role);}
  if(roles.size()==1){auto role=*roles.begin();for(auto& b:r.textures)if(b.slot=="this"&&b.role!=role){r.pcNote+=(r.pcNote.empty()?"":"；")+std::string("this = ")+(b.resource.empty()?"空":b.resource)+"：按名称用途为 "+b.role+"，按 PC 槽位为 "+role+"，采用 PC 槽位";b.role=role;}}
  else if(roles.size()>1){std::string list;for(auto& x:roles)list+=(list.empty()?"":"/")+x;r.pcNote+=std::string(r.pcNote.empty()?"":"；")+"这张贴图在 PC 上用于多种槽位（"+list+"），用途按名称保留";}}}

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
  if(mats==subs){m.reasons.push_back("材质数等于子网格数（"+std::to_string(mats)+"）：子网格 "+std::to_string(sub)+" 使用材质槽 "+std::to_string(sub));return true;}
  if(size_t(sub)<mats&&size_t(sub)+1<subs){m.reasons.push_back("材质 "+std::to_string(mats)+" 个，子网格 "+std::to_string(subs)+" 个：按位置取槽 "+std::to_string(sub)+"（数量不同）");return false;}
  if(size_t(sub)>=mats){m.reasons.push_back("子网格 "+std::to_string(sub)+" 没有对应的材质槽（材质 "+std::to_string(mats)+" 个，子网格 "+std::to_string(subs)+" 个）");m.slot=-1;return false;}
  m.reasons.push_back("最后一个子网格：材质 "+std::to_string(mats)+" 个，子网格 "+std::to_string(subs)+" 个，标准 Unity 会把多出的材质也画在它上面；未在这个引擎上验证");return false;};
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
  if(hit.size()==1){why="本机的网格 "+e.mesh+" 上传（"+std::to_string(e.vertices)+" 顶点，"+std::to_string(pcdb::indices(e.subs))+" 索引"+(e.build.empty()?"":"，游戏版本 "+e.build.substr(0,8))+"）得到了相同的索引缓冲 hash "+e.ib+"；只有 "+label(rs[size_t(hit[0])])+" 显示这个网格";return hit[0];}
  why=hit.empty()?"":"索引缓冲 hash "+e.ib+" 在本机对应网格 "+e.mesh+"，有 "+std::to_string(hit.size())+" 个渲染器显示它";return -1;};
 // A segment: rule r's draw starting at first (match_first_index, or the PC's only draw start of the hash).
 auto segment=[&](const Rule& r,size_t k,long long first)->Match{Match m;m.rule=k;
  auto names=nameHits(r);std::string nameText;for(int i:names)nameText+=(nameText.empty()?"":"、")+label(rs[size_t(i)]);
  std::vector<long long> group=firsts[r.hash];if(std::find(group.begin(),group.end(),first)==group.end()){group.push_back(first);std::sort(group.begin(),group.end());}
  if(r.pcHasIb){m.reasons.push_back("PC：hash "+r.hash+" 是 "+r.pcIb.character+" "+r.pcIb.component+" 的索引缓冲，绘制起点 "+joined(r.pcIb.indexes));
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
   m.reasons.push_back(R.known()?label(R)+" 显示的是低精度网格（"+R.meshName+"），索引布局与 PC 的完整网格不可比":"无法读取 "+label(R)+" 的 indexStart"+(R.layout.status.empty()?"":"（"+R.layout.status+"）"));
   m.reasons.push_back(viaGroup?"部件名 '"+r.hint+"' 不对应任何渲染器；hash "+r.hash+" 的另一段对应 "+label(R)+"（把一个 PC 索引缓冲视为一个安卓网格）":"部件名 '"+r.hint+"' 对应它");
   m.reasons.push_back("第 "+std::to_string(pos)+" 段（共 "+std::to_string(group.size())+" 段），子网格 "+std::to_string(subs)+" 个，材质 "+std::to_string(R.materials.size())+" 个");
   bool counts=group.size()==subs&&subs==R.materials.size(),alias=false;int byName=slotByName(R,r.hint,alias);
   if(byName>=0){m.slot=byName;m.reasons.push_back("材质 "+R.materials[size_t(byName)].name+"（槽 "+std::to_string(byName)+"）以部件命名"+(alias?"（GIMI 的 Head 即游戏的 Hair，命名惯例）":""));
    if(counts&&size_t(byName)==pos){m.submesh=byName;m.verdict=Verdict::Likely;m.reasons.push_back("材质名与段顺序一致（仍无索引依据）");}
    else if(counts){m.verdict=Verdict::Ambiguous;m.reasons.push_back("按材质名为槽 "+std::to_string(byName)+"，按段顺序为 "+std::to_string(pos));}
    else{m.verdict=Verdict::Likely;m.reasons.push_back("数量不同，只按材质名取槽");}}
   else if(counts){m.submesh=int(pos);m.slot=int(pos);m.verdict=Verdict::Likely;m.reasons.push_back("数量一致：只按顺序取槽 "+std::to_string(pos)+"（仅凭顺序不足以确定）");}
   else{m.verdict=Verdict::Ambiguous;m.reasons.push_back("数量不同，且没有以部件命名的材质，无法取槽");}
   if(R.lod)m.reasons.push_back(R.known()?std::string("完整网格出现后重新读取"):"渲染器显示低精度网格（"+R.meshName+"），完整网格出现后重新读取");
   return m;}
  std::vector<std::pair<int,int>> exact,inside;bool unknown=false;
  for(size_t i=0;i<rs.size();i++){if(!comparable(i)){unknown=true;continue;}const auto& subs=rs[i].layout.subs;
   for(size_t j=0;j<subs.size();j++){if(subs[j].start==uint64_t(first))exact.push_back({int(i),int(j)});else if(uint64_t(first)>subs[j].start&&uint64_t(first)<uint64_t(subs[j].start)+subs[j].count)inside.push_back({int(i),int(j)});}}
  auto groupExact=[&](int i){unsigned n=0;for(long long f:group)for(auto& s:rs[size_t(i)].layout.subs)if(s.start==uint64_t(f)){n++;break;}return n;};
  bool pcGroup=r.pcHasIb&&std::find(r.pcIb.indexes.begin(),r.pcIb.indexes.end(),uint64_t(first))!=r.pcIb.indexes.end();
  const std::string starts=pcGroup?"个 PC 绘制起点":"个 match_first_index",next1=pcGroup?"PC 绘制起点":"match_first_index";
  bool identified=false;   // the index buffer itself is known to be this renderer's (device upload, or the PC draw count)
  // This device's upload of the hash names the mesh: nothing else needs to decide.
  if(dev>=0){std::vector<std::pair<int,int>> mine;for(auto& h:exact)if(h.first==dev)mine.push_back(h);
   if(!mine.empty()){m.reasons.push_back(devWhy);exact=mine;identified=true;}
   else m.reasons.push_back(devWhy+"，但它没有从 "+std::to_string(first)+" 开始的子网格");}
  // Index 0 starts a submesh on every renderer: the other segments of the hash decide (the mod's and the PC's), when
  // they all start submeshes of only one of them (index starts, not order); then the PC index count of the draw;
  // otherwise the part name, which only makes it likely.
  if(exact.size()>1&&group.size()>1){std::vector<std::pair<int,int>> whole;for(auto& h:exact)if(groupExact(h.first)==group.size())whole.push_back(h);
   if(whole.size()==1){m.reasons.push_back(std::to_string(exact.size())+" 个渲染器有从 "+std::to_string(first)+" 开始的子网格；只有 "+label(rs[size_t(whole[0].first)])+" 的子网格起点包含这个 hash 的全部 "+std::to_string(group.size())+" "+starts);exact=whole;}}
  if(exact.size()>1&&pcCount>=0){std::vector<std::pair<int,int>> fit;for(auto& h:exact){const auto& R=rs[size_t(h.first)];if(R.layout.subs[size_t(h.second)].count==uint64_t(pcCount)&&R.layout.subs.size()==r.pcIb.indexes.size())fit.push_back(h);}
   if(fit.size()==1){m.reasons.push_back(std::to_string(exact.size())+" 个渲染器有从 "+std::to_string(first)+" 开始的子网格；只有 "+label(rs[size_t(fit[0].first)])+" 与 PC 绘制一致：子网格 "+std::to_string(r.pcIb.indexes.size())+" 个，这一段 "+std::to_string(pcCount)+" 个索引");exact=fit;identified=true;}}
  if(exact.size()>1){std::vector<int> pick=names;bool byGroup=false;if(pick.empty()){int g=groupRenderer(r.hash);if(g>=0){pick.push_back(g);byGroup=true;}}
   std::vector<std::pair<int,int>> named;for(auto& h:exact)if(std::find(pick.begin(),pick.end(),h.first)!=pick.end())named.push_back(h);
   if(named.size()==1){m.reasons.push_back(std::to_string(exact.size())+" 个渲染器有从 "+std::to_string(first)+" 开始的子网格；"+(byGroup?"这个 hash 的其他段对应 ":"按部件名选 ")+label(rs[size_t(named[0].first)]));exact=named;m.verdict=Verdict::Likely;}}
  if(exact.size()==1){auto [i,j]=exact[0];const auto& R=rs[size_t(i)];const auto& sm=R.layout.subs[size_t(j)];m.renderer=i;m.submesh=j;m.slot=j;
   m.reasons.push_back("子网格 "+std::to_string(j)+" 的 indexStart "+std::to_string(sm.start)+" = "+(r.first>=0?"match_first_index":"PC 绘制起点"));
   unsigned all=groupExact(i);bool complete=all==group.size();m.reasons.push_back("这个 hash 的 "+std::to_string(group.size())+" "+starts+"中有 "+std::to_string(all)+" 个是这个渲染器的子网格起点");
   auto next=std::upper_bound(group.begin(),group.end(),first);bool countOk=true;
   if(next!=group.end()){long long pc=*next-first;if(pc==(long long)sm.count)m.reasons.push_back("索引数 "+std::to_string(sm.count)+" = 到下一个"+next1+"的距离");
    else{countOk=false;m.reasons.push_back("索引数 "+std::to_string(sm.count)+"，到下一个"+next1+"的距离为 "+std::to_string(pc));}}
   if(pcCount>=0){if(pcCount==(long long)sm.count)m.reasons.push_back("索引数 "+std::to_string(sm.count)+" 与 PC 绘制相同");else{countOk=false;m.reasons.push_back("索引数 "+std::to_string(sm.count)+"，PC 绘制为 "+std::to_string(pcCount));}}
   bool merged=false;for(long long f:group)if(f>first&&uint64_t(f)<uint64_t(sm.start)+sm.count){merged=true;bool own=ruleAt(f,r.hash)!=nullptr;bool same=own&&sameTextures(first,f,r.hash);
    m.reasons.push_back("PC 的第 "+std::to_string(f)+" 段也在这个子网格内（PC 的多次绘制合并为一个槽）："+(!own?"MOD 没有改这一段，一个材质无法同时承载两者":same?"贴图相同，一个材质即可覆盖":"贴图不同，一个材质无法同时承载两者"));if(!same)countOk=false;}
   bool slotSure=slotOf(R,j,m);
   {bool alias=false;int byName=slotByName(R,r.hint,alias);if(byName==j)m.reasons.push_back("材质 "+R.materials[size_t(j)].name+" 以部件命名"+(alias?"（GIMI 的 Head 即游戏的 Hair）":""));
    else if(byName>=0){m.reasons.push_back("但以部件命名的是材质 "+R.materials[size_t(byName)].name+"（槽 "+std::to_string(byName)+"）");slotSure=false;}}
   if(!names.empty()&&std::find(names.begin(),names.end(),i)==names.end()){m.reasons.push_back("部件名 '"+r.hint+"' 指向的是 "+nameText);if(!identified)slotSure=false;}
   else if(!names.empty())m.reasons.push_back("部件名 '"+r.hint+"' 一致");
   if(m.verdict!=Verdict::Likely)m.verdict=complete&&countOk&&slotSure?Verdict::Confirmed:(merged&&!countOk?Verdict::Ambiguous:Verdict::Likely);
   return m;}
  if(exact.size()>1){m.verdict=Verdict::Ambiguous;for(auto& h:exact)m.candidates.push_back(label(rs[size_t(h.first)])+" 子网格 "+std::to_string(h.second));m.reasons.push_back("多个渲染器有从 "+std::to_string(first)+" 开始的子网格，部件名也无法确定");return m;}
  if(!inside.empty()){m.verdict=Verdict::Ambiguous;for(auto& h:inside){m.candidates.push_back(label(rs[size_t(h.first)])+" 子网格 "+std::to_string(h.second));}
   m.reasons.push_back((r.first>=0?"match_first_index ":"PC 绘制起点 ")+std::to_string(first)+" 落在安卓的某个子网格内：PC 的这一段与其他段合并成了一个子网格和材质槽");
   // The renderer where the hash's other segments start submeshes is the one it most likely went into.
   std::vector<std::pair<int,int>> fit;unsigned most=0;for(auto& h:inside){unsigned n=groupExact(h.first);if(n>most){most=n;fit.clear();}if(n&&n==most)fit.push_back(h);}
   if(fit.size()==1){m.renderer=fit[0].first;m.submesh=fit[0].second;m.slot=fit[0].second;m.reasons.push_back("这个 hash 的其他段中有 "+std::to_string(most)+" 段是 "+label(rs[size_t(m.renderer)])+" 的子网格起点，最可能合并在它的子网格 "+std::to_string(m.submesh)+" 中");}
   return m;}
  if(names.size()>1){m.verdict=Verdict::Ambiguous;for(int i:names)m.candidates.push_back(label(rs[size_t(i)]));m.reasons.push_back("布局可比的渲染器中没有从 "+std::to_string(first)+" 开始的子网格，且部件名对应多个渲染器");return m;}
  m.reasons.push_back(std::string(unknown?"布局可比的渲染器中":"")+"没有从 "+std::to_string(first)+" 开始的子网格"+(target<0?"；部件名 '"+r.hint+"' 不对应任何渲染器":"；"+label(rs[size_t(target)])+(viaGroup?"（按这个 hash 的其他段）":"（按名称）")+" 在这里也没有子网格起点"));
  m.verdict=Verdict::None;m.reasons.push_back("因此可能是安卓的索引顺序与 PC 不同，或这个 hash 属于其他角色或外观");return m;};
 // A texture hash the PC database places: per draw it is in, the renderer of that component and the draw's slot.
 // The renderer by this device's upload of the component's index buffer, by submesh starts equal to all its PC draw
 // starts (more than one), by the PC index count of its only draw, or by the segment rules of the same hash; a
 // component without an index buffer of its own in the database (Face) by its name on one full-LOD renderer. A name
 // for a component that has an index buffer makes it likely only.
 auto texture=[&](const Rule& r,size_t k,const PcPart& u)->Match{Match m;m.rule=k;
  m.reasons.push_back("PC：贴图 hash "+r.hash+" 属于 "+u.character+" "+(u.component.empty()?std::string("（无名部件）"):u.component)+" 第 "+std::to_string(u.submesh)+" 次绘制的 "+u.slot);
  int target=-1;bool strong=false;std::string how;
  if(!u.ib.empty()){std::string why;int dev=deviceRenderer(u.ib,why);if(dev>=0){target=dev;strong=true;how=why;}else if(!why.empty())m.reasons.push_back(why);
   if(target<0&&u.indexes.size()>1){std::vector<int> hit;for(size_t i=0;i<rs.size();i++){if(!comparable(i))continue;std::vector<std::pair<uint32_t,uint32_t>> subs;for(auto& s:rs[i].layout.subs)subs.push_back({uint32_t(s.start),uint32_t(s.count)});if(pcdb::startsEqual(subs,u.indexes))hit.push_back(int(i));}
    if(hit.size()==1){target=hit[0];strong=true;how=label(rs[size_t(target)])+" 的子网格起点就是 "+u.component+" 的 PC 绘制起点 "+joined(u.indexes)+"（其他渲染器都不是）";}
    else if(hit.size()>1)m.reasons.push_back(std::to_string(hit.size())+" 个渲染器的子网格起点都是 PC 绘制起点 "+joined(u.indexes));}
   if(target<0&&u.indexes.size()==1&&u.counts.size()==1){std::vector<int> hit;for(size_t i=0;i<rs.size();i++)if(comparable(i)&&rs[i].layout.subs.size()==1&&rs[i].layout.subs[0].start==u.indexes[0]&&rs[i].layout.subs[0].count==u.counts[0])hit.push_back(int(i));
    if(hit.size()==1){target=hit[0];strong=true;how=label(rs[size_t(target)])+" 是唯一只有一个子网格且索引数为 "+std::to_string(u.counts[0])+" 的渲染器，与 "+u.component+" 的 PC 绘制一致";}}
   if(target<0){for(auto& x:out){const Rule& o=d.rules[x.rule];if(pcdb::lower(o.hash)!=u.ib||x.renderer<0||(x.verdict!=Verdict::Confirmed&&x.verdict!=Verdict::Likely))continue;
     if(target<0||target==x.renderer){target=x.renderer;strong=strong||x.verdict==Verdict::Confirmed;how="索引缓冲 "+u.ib+" 上的规则 "+o.section+" 匹配到 "+label(rs[size_t(x.renderer)])+"（"+verdictName(x.verdict)+"）";}else{target=-2;break;}}
    if(target==-2){target=-1;strong=false;m.reasons.push_back("索引缓冲 "+u.ib+" 上的规则匹配到多个渲染器");}}
   if(target<0&&!u.component.empty()){auto h=hitsFor(pcdb::lower(u.component));std::vector<int> full;for(int i:h)if(!rs[size_t(i)].lod)full.push_back(i);if(full.size()==1){target=full[0];how="部件名 "+u.component+" 对应 "+label(rs[size_t(target)])+"（仅凭名称）";}}}
  else if(!u.component.empty()){auto h=hitsFor(pcdb::lower(u.component));std::vector<int> full;for(int i:h)if(!rs[size_t(i)].lod)full.push_back(i);
   if(full.size()==1){target=full[0];strong=true;how="PC 数据库中部件 "+u.component+" 没有自己的索引缓冲；按名称对应 "+label(rs[size_t(target)])+"，是它对应的唯一完整精度渲染器";}
   else if(full.size()>1){for(int i:full)m.candidates.push_back(label(rs[size_t(i)]));m.reasons.push_back("部件名 "+u.component+" 对应多个渲染器");}}
  if(target<0){m.verdict=m.candidates.empty()?Verdict::None:Verdict::Ambiguous;m.reasons.push_back("这里没有 "+u.character+" "+(u.component.empty()?std::string("（无名部件）"):u.component)+(u.ib.empty()?"":"（索引缓冲 "+u.ib+"）")+" 的渲染器");return m;}
  const auto& R=rs[size_t(target)];m.renderer=target;m.reasons.push_back(how);
  if(R.lod){m.verdict=Verdict::Likely;m.reasons.push_back(label(R)+" 显示低精度网格（"+R.meshName+"），只按完整网格写入");return m;}
  size_t subs=R.layout.submeshes>=0?size_t(R.layout.submeshes):R.submeshes;
  if(u.submesh>=subs){m.verdict=Verdict::Ambiguous;m.reasons.push_back("PC 第 "+std::to_string(u.submesh)+" 次绘制，但 "+label(R)+" 只有 "+std::to_string(subs)+" 个子网格");return m;}
  m.submesh=int(u.submesh);m.slot=int(u.submesh);bool slotSure=slotOf(R,int(u.submesh),m);
  if(!u.indexes.empty()&&R.known()&&u.submesh<u.indexes.size()&&u.submesh<R.layout.subs.size()){if(R.layout.subs[u.submesh].start==u.indexes[u.submesh])m.reasons.push_back("它的子网格 "+std::to_string(u.submesh)+" 起点为 "+std::to_string(u.indexes[u.submesh])+"，与 PC 绘制一致");
   else{slotSure=false;m.reasons.push_back("它的子网格 "+std::to_string(u.submesh)+" 起点为 "+std::to_string(R.layout.subs[u.submesh].start)+"，PC 绘制起点为 "+std::to_string(u.indexes[u.submesh]));}}
  m.verdict=strong&&slotSure?Verdict::Confirmed:Verdict::Likely;return m;};
 // Segments first (texture hashes on a component follow what its index buffer matched), then texture hashes, then
 // rules without match_first_index for a hash others split: they follow what the segments matched.
 std::vector<size_t> order;for(int pass=0;pass<3;pass++)for(size_t k=0;k<d.rules.size();k++){auto& kd=d.rules[k].kind;int p=kd=="whole-ib"?2:kd=="hash-unknown"?1:0;if(p==pass)order.push_back(k);}
 for(size_t k:order){const Rule& r=d.rules[k];Match m;m.rule=k;
  if(r.kind=="control"){m.verdict=Verdict::Skipped;m.reasons.push_back("不绑定贴图（只设置变量）");out.push_back(m);continue;}
  if(r.kind=="segment"){out.push_back(segment(r,k,r.first));continue;}
  if(r.kind=="whole-ib"){m.reasons.push_back("没有 match_first_index：3DMigoto 对 hash "+r.hash+" 的每次绘制都生效");
   std::set<int> hit;for(auto& x:out)if(x.renderer>=0&&d.rules[x.rule].hash==r.hash&&(x.verdict==Verdict::Confirmed||x.verdict==Verdict::Likely))hit.insert(x.renderer);
   if(hit.size()==1){m.renderer=*hit.begin();m.verdict=Verdict::Likely;m.reasons.push_back("这个 hash 的各段匹配到 "+label(rs[size_t(m.renderer)])+"：作用于它的每个槽");}
   else{m.verdict=hit.empty()?Verdict::None:Verdict::Ambiguous;for(int i:hit)m.candidates.push_back(label(rs[size_t(i)]));m.reasons.push_back(hit.empty()?"这个 hash 的各段没有匹配到渲染器":"这个 hash 的各段匹配到多个渲染器");}
   out.push_back(m);continue;}
  // hash-unknown, which the PC database may name: an index buffer (every draw of it: one match per PC draw start) or
  // a texture (one match per draw it is in).
  if(r.pcHasIb&&!r.pcIb.indexes.empty()){for(auto f:r.pcIb.indexes){auto x=segment(r,k,(long long)f);x.reasons.insert(x.reasons.begin(),"没有 match_first_index；PC：这个索引缓冲有 "+std::to_string(r.pcIb.indexes.size())+" 次绘制，这一次起点为 "+std::to_string(f));out.push_back(x);}continue;}
  if(!r.pcTextures.empty()){std::set<std::pair<int,int>> taken;for(auto& u:r.pcTextures){auto x=texture(r,k,u);if(!r.pcNote.empty())x.reasons.push_back(r.pcNote);if(x.renderer>=0&&x.slot>=0&&!taken.insert({x.renderer,x.slot}).second)continue;out.push_back(x);}continue;}
  // Neither: an IB, VB or texture hash GIBox cannot tell. Name and texture size give candidates.
  auto names=nameHits(r);
  m.reasons.push_back(r.evidence.empty()?"hash 类型未知":r.evidence);m.reasons.push_back(d.pc.empty()?"安卓上无法重新计算 3DMigoto hash，且未安装 PC hash 数据库，无法确认":"PC hash 数据库中这个角色没有这个 hash（"+d.pc+"）；安卓上无法重新计算 3DMigoto hash，无法确认");
  for(int i:names)m.candidates.push_back(label(rs[size_t(i)])+"（按部件名）");
  if(r.textureName||!names.empty()){const Resource* mine=nullptr;for(auto& b:r.textures){auto it=d.resources.find(b.resource);if(it!=d.resources.end()&&it->second.width>0){mine=&it->second;break;}}
   if(mine){unsigned n=0;for(size_t i=0;i<rs.size()&&n<8;i++)for(size_t s=0;s<rs[i].materials.size()&&n<8;s++)for(auto& t:rs[i].materials[s].textures)
     if(t.texture&&t.width==std::to_string(mine->width)&&t.height==std::to_string(mine->height)&&n<8){m.candidates.push_back(label(rs[i])+" 槽 "+std::to_string(s)+" "+t.property+"="+t.name+"（尺寸相同 "+t.width+"x"+t.height+"）");n++;}
    if(!n)m.reasons.push_back("这个角色上没有与 MOD 贴图尺寸 "+std::to_string(mine->width)+"x"+std::to_string(mine->height)+" 相同的游戏贴图（安卓贴图可能更小）");}}
  m.verdict=m.candidates.empty()?Verdict::None:Verdict::Ambiguous;out.push_back(m);}
 // Several rules on one slot: written together when they set different textures (a texture hash's light map next to
 // a segment's diffuse); two that set the same one cannot both be put on one material unless their conditions keep
 // them apart.
 auto roles=[&](size_t rule){std::set<std::string> s;for(auto& b:d.rules[rule].textures)s.insert(b.role);return s;};
 // Only what may be written counts: an ambiguous guess on the slot is not.
 std::map<std::tuple<int,int>,std::vector<size_t>> bySlot;for(size_t i=0;i<out.size();i++)if(out[i].renderer>=0&&out[i].slot>=0&&(out[i].verdict==Verdict::Confirmed||out[i].verdict==Verdict::Likely))bySlot[{out[i].renderer,out[i].slot}].push_back(i);
 for(auto& kv:bySlot)if(kv.second.size()>1){for(size_t i:kv.second){std::string clash,beside;auto mine=roles(out[i].rule);
   for(size_t j:kv.second){if(j==i||out[j].rule==out[i].rule)continue;auto theirs=roles(out[j].rule);bool overlap=mine.count("unknown")||theirs.count("unknown");for(auto& x:mine)overlap=overlap||theirs.count(x);
    (overlap?clash:beside)+=std::string((overlap?clash:beside).empty()?"":"、")+d.rules[out[j].rule].section;}
   if(!clash.empty()){std::string all=d.rules[out[i].rule].section+"、"+clash;out[i].reasons.push_back("同一槽上有多条规则："+all);if(out[i].verdict==Verdict::Confirmed)out[i].verdict=Verdict::Likely;}
   if(!beside.empty())out[i].reasons.push_back("同一槽上还有设置其他贴图的规则："+beside);}}
 std::stable_sort(out.begin(),out.end(),[](const Match& a,const Match& b){return a.rule<b.rule;});
 return out;}

inline std::string matchLine(const Doc& d,const Match& m,const std::vector<Renderer>& rs){const Rule& r=d.rules[m.rule];
 std::string s=d.name+"："+r.section+"，hash "+(r.hash.empty()?"-":r.hash)+"，match_first_index "+(r.first>=0?std::to_string(r.first):"-")+" → "+verdictName(m.verdict);
 const Material* mat=nullptr;
 if(m.renderer>=0){const auto& R=rs[size_t(m.renderer)];s+="，渲染器 "+label(R)+"，网格 "+R.meshName;if(m.submesh>=0)s+="，子网格 "+std::to_string(m.submesh);
  if(m.slot>=0){s+="，槽 "+std::to_string(m.slot);if(size_t(m.slot)<R.materials.size()){mat=&R.materials[size_t(m.slot)];s+="，材质 "+mat->name;}}}
 std::string why;for(auto& x:m.reasons)why+=(why.empty()?"":"；")+x;s+="｜依据："+why;
 if(!m.candidates.empty()){std::string c;for(auto& x:m.candidates)c+=(c.empty()?"":"；")+x;s+="｜候选："+c;}
 if(m.verdict!=Verdict::Skipped)s+="｜贴图写入计划（诊断阶段，未执行）："+plan(d,r,mat);
 return s;}
inline std::string ruleLine(const Doc& d,const Rule& r){std::string t;for(auto& b:r.textures)t+=(t.empty()?"":"，")+b.slot+"="+(b.resource.empty()?"空":b.resource)+"["+b.role+"]"+(b.condition.empty()?"":" 条件 "+b.condition)+(b.via.empty()?"":" 经由 "+b.via);
 std::string x;for(auto& e:r.external)x+=(x.empty()?"":"，")+e;std::string dr;for(auto& e:r.draws)dr+=(dr.empty()?"":"，")+e;
 return d.name+"："+r.section+"，hash "+(r.hash.empty()?"-":r.hash)+"，match_first_index "+(r.first>=0?std::to_string(r.first):"-")+"，优先级 "+std::to_string(r.priority)+"，类型 "+r.kind+"，部件 "+r.hint+(r.slotLetter>=0?std::string("，槽字母 ")+char('A'+r.slotLetter):"")
  +"，贴图 ["+t+"]"+(dr.empty()?"":"，drawindexed ["+dr+"]")+(x.empty()?"":"，外部命令 ["+x+"]");}
inline std::string rendererLine(const Renderer& r,unsigned character){
 return "角色 "+std::to_string(character)+"，渲染器 "+label(r)+"，网格 "+r.meshName+"#"+hexOf(r.mesh)+"，精度 "+(r.lod?"低精度（按名称）":"完整（按名称）")+"，顶点 "+std::to_string(r.vertices)
  +"，子网格（原生）"+std::to_string(r.submeshes)+"，subMeshCount "+orDash(r.layout.submeshCount)+"，vertexCount "+orDash(r.layout.vertexCount)+"，骨骼 "+std::to_string(r.bones)+"，可读 "+orDash(r.layout.readable)
  +"，材质 "+std::to_string(r.materials.size())+"，avatar "+(r.avatar.empty()?"-":r.avatar)+(r.takenBy.empty()?"":"，已由 "+r.takenBy+" 接管（读取的是游戏原网格和材质）")+(r.swap.empty()?"":"，脸部贴图替换 "+r.swap)
  +(r.layout.status=="read"?"":"，布局 "+r.layout.status)+(r.error.empty()?"":"，错误 "+r.error);}
inline std::vector<std::string> submeshLines(const Renderer& r){std::vector<std::string> out;
 for(size_t i=0;i<r.layout.subs.size();i++){auto& s=r.layout.subs[i];std::string slot=i<r.materials.size()?std::to_string(i)+"（"+r.materials[i].name+"）":"无";
  out.push_back(label(r)+"：子网格 "+std::to_string(i)+(s.known?"，indexStart "+std::to_string(s.start)+"，indexCount "+std::to_string(s.count):"，范围 "+s.error)+"，按位置对应材质槽 "+slot);}
 return out;}
inline std::vector<std::string> materialLines(const Renderer& r){std::vector<std::string> out;
 for(size_t i=0;i<r.materials.size();i++){auto& m=r.materials[i];std::string t;for(auto& x:m.textures)t+=(t.empty()?"":"，")+textureText(x);std::string shared;for(auto& x:m.sharedWith)shared+=(shared.empty()?"":",")+x;
  out.push_back(label(r)+"：槽 "+std::to_string(i)+"，材质 "+m.name+"#"+hexOf(m.material)+"，着色器 "+m.shader+(shared.empty()?"":"，同时用于 "+shared)+"，贴图 ["+t+"]"+(m.error.empty()?"":"，错误 "+m.error));}
 return out;}
// ---------------------------------------------------------------- texture-only mods (0.7.52)
// models/<name>/texture.json (TextureMod.java): the rules (the diagnostic document as "diag"), the variants and the
// decoded textures (textures/<n>.rgba, RGBA8, rows top to bottom as on the PC).
struct ModTexture {std::string resource,file;unsigned width=0,height=0;};
struct TextureMod {std::string name,generation;unsigned character=0;std::map<std::string,std::vector<int>> vars;std::map<std::string,int> defaults;std::vector<ModTexture> textures;Doc doc;};
inline TextureMod parseMod(const std::string& text,const std::string& dir){Json j=JsonParser(text).parse();
 require(j.type==Json::Object&&str(j,"format")=="gibox-texture-mod","不是 GIBox 纯贴图 MOD");require(num(j,"version",0)==1,"纯贴图 MOD 版本 "+std::to_string(num(j,"version",0))+" 不受支持");
 TextureMod m;m.name=str(j,"name");m.generation=str(j,"generation");require(!m.name.empty()&&!m.generation.empty(),"纯贴图 MOD 的名称或生成号无效");
 auto c=num(j.at("character"),"id",0);require(c>=10000000&&c<=10001999,"纯贴图 MOD 的角色无效");m.character=unsigned(c);
 const auto& v=j.at("variants");require(v.type==Json::Object,"纯贴图 MOD 的款式无效");const auto& d=v.at("default");
 if(v.at("vars").type==Json::Array)for(auto& x:v.at("vars").array){auto n=str(x,"name");require(n.size()>1&&n[0]=='$'&&!m.vars.count(n),"纯贴图 MOD 的变量无效");auto& vals=m.vars[n];
  const auto& a=x.at("values");require(a.type==Json::Array&&!a.array.empty()&&a.array.size()<=64,"纯贴图 MOD 的取值无效："+n);for(auto& k:a.array){require(k.type==Json::Number&&std::floor(k.number)==k.number&&std::fabs(k.number)<=1000000,"纯贴图 MOD 的值无效："+n);vals.push_back(int(k.number));}
  auto def=num(d,n.c_str(),vals[0]);require(std::find(vals.begin(),vals.end(),int(def))!=vals.end(),"纯贴图 MOD 的默认值无效："+n);m.defaults[n]=int(def);}
 const auto& t=j.at("textures");require(t.type==Json::Array&&t.array.size()<=256,"纯贴图 MOD 的贴图列表无效");
 for(auto& x:t.array){ModTexture mt;mt.resource=str(x,"resource");mt.file=str(x,"file");auto w=num(x,"width",0),h=num(x,"height",0);
  bool safe=mt.file.size()>14&&mt.file.compare(0,9,"textures/")==0&&mt.file.compare(mt.file.size()-5,5,".rgba")==0&&mt.file.find_first_not_of("0123456789",9)==mt.file.size()-5;
  require(!mt.resource.empty()&&safe&&w>0&&h>0&&w<=4096&&h<=4096,"纯贴图 MOD 的贴图无效："+mt.resource);mt.width=unsigned(w);mt.height=unsigned(h);m.textures.push_back(mt);}
 const auto& diag=j.at("diag");require(diag.type==Json::Object,"纯贴图 MOD 的规则（diag）无效");m.doc=parseDoc(diag,dir+"/texture.json");m.doc.name=m.name;m.doc.character=m.character;
 return m;}

// A condition as TextureDiag writes it ("$a == 1 && !($b == 2)"; || and parentheses too): variables and integer
// numbers compared with == != < > <= >=. Anything else, or a variable the mod does not declare, cannot be evaluated
// here: false, with why set (so the binding is reported, not written).
inline bool condition(const std::string& text,const std::map<std::string,int>& vars,std::string& why){
 if(text.empty()){return true;}size_t p=0;bool bad=false;
 auto ws=[&]{while(p<text.size()&&text[p]==' ')p++;};auto take=[&](const char* t){ws();size_t n=std::strlen(t);if(text.compare(p,n,t)==0){p+=n;return true;}return false;};
 auto fail=[&](const std::string& w){if(!bad){bad=true;why=w;}return 0.0;};
 std::function<double()> expr;
 auto operand=[&]()->double{ws();if(p<text.size()&&text[p]=='$'){size_t q=p+1;while(q<text.size()&&(std::isalnum((unsigned char)text[q])||text[q]=='_'))q++;auto n=text.substr(p,q-p);p=q;auto it=vars.find(n);if(it==vars.end())return fail("未知变量 "+n);return it->second;}
  size_t q=p;if(q<text.size()&&(text[q]=='-'||text[q]=='+'))q++;size_t digits=q;while(q<text.size()&&std::isdigit((unsigned char)text[q]))q++;if(q==digits)return fail("此处不是数字：'"+text.substr(p)+"'");double v=std::stod(text.substr(p,q-p));p=q;return v;};
 std::function<double()> unary=[&]()->double{if(take("!")&&!take("=")){return unary()!=0?0:1;}
  ws();if(p<text.size()&&text[p]=='('){p++;double v=expr();if(!take(")"))return fail("缺少右括号");return v;}
  double a=operand();for(const char* op:{"==","!=","<=",">=","<",">"}){if(take(op)){double b=operand();std::string o=op;return o=="=="?a==b:o=="!="?a!=b:o=="<="?a<=b:o==">="?a>=b:o=="<"?a<b:a>b;}}return a;};
 auto conj=[&]()->double{double v=unary();while(take("&&")){double b=unary();v=(v!=0&&b!=0)?1:0;}return v;};
 expr=[&]()->double{double v=conj();while(take("||")){double b=conj();v=(v!=0||b!=0)?1:0;}return v;};
 double v=expr();ws();if(!bad&&p!=text.size())fail("多余的内容：'"+text.substr(p)+"'");return !bad&&v!=0;}

// What one rule writes for these variable values: diffuse -> _MainTex, lightmap -> _LightMapTex, the last binding of a
// property winning (3DMigoto runs the lines in order; a null binding takes it off). Every other binding is reported.
struct Write {std::string property,resource,role,slot;bool linear=false;};
inline std::vector<Write> writes(const Rule& r,const std::map<std::string,int>& vars,std::vector<std::string>& skipped){
 std::map<std::string,Write> byProperty;std::vector<std::string> order;
 for(auto& b:r.textures){std::string why;if(!condition(b.condition,vars,why)){if(!why.empty())skipped.push_back(b.slot+" "+b.resource+"：条件 '"+b.condition+"' 无法求值（"+why+"）");continue;}
  const char* prop=b.role=="diffuse"?"_MainTex":b.role=="lightmap"?"_LightMapTex":nullptr;
  if(!prop){skipped.push_back(b.slot+" "+(b.resource.empty()?"空":b.resource)+" ["+b.role+"]：没有确认的游戏属性（待诊断）");continue;}
  if(b.resource.empty()){byProperty.erase(prop);continue;}
  if(!byProperty.count(prop)){order.push_back(prop);}byProperty[prop]={prop,b.resource,b.role,b.slot,b.role!="diffuse"};}
 std::vector<Write> out;for(auto& p:order){auto it=byProperty.find(p);if(it!=byProperty.end())out.push_back(it->second);}return out;}
// Several rules on one slot (a texture hash's light map next to a segment's diffuse): texture-hash rules first, then
// the others, each property taken by the last that sets it, so a rule on the index buffer wins over a texture hash
// (3DMigoto's draw-level override binds its own resource in that slot).
inline std::vector<size_t> planOrder(const Doc& d,std::vector<size_t> rules){std::stable_sort(rules.begin(),rules.end(),[&](size_t a,size_t b){auto t=[&](size_t k){return d.rules[k].kind=="hash-unknown"?0:1;};return t(a)<t(b);});return rules;}
inline std::vector<Write> writesFor(const Doc& d,const std::vector<size_t>& rules,const std::map<std::string,int>& vars,std::vector<std::string>& skipped){std::vector<Write> out;
 for(size_t k:planOrder(d,rules)){std::vector<std::string> sk;for(auto& w:writes(d.rules[k],vars,sk)){bool replaced=false;for(auto& o:out)if(o.property==w.property){o=w;replaced=true;}if(!replaced)out.push_back(w);}
  for(auto& x:sk)skipped.push_back(rules.size()>1?d.rules[k].section+"："+x:x);}
 return out;}
inline std::string sections(const Doc& d,const std::vector<size_t>& rules){std::string o;for(size_t k:planOrder(d,rules))o+=(o.empty()?"":" + ")+d.rules[k].section;return o;}
}  // namespace ms::texdiag
