#pragma once
// GIBox model format, versions 1–4 — GIBox's own model folder, written by the converter (ModelConverter.java) and read here.
// Version 2 gives each draw the variable values it is drawn for ("when") instead of one draw list per combination.
//
//   models/<name>/model.json    manifest: character, parts, materials, variants, and where each array sits in model.bin
//   models/<name>/model.bin     every binary array, each starting on a 16-byte boundary; CRC-32 of the whole file in the manifest
//   models/<name>/learned.json  written by the runtime: meshes identified in the game, the character ID found for a named model
//
// Every count, range and value is checked before anything reaches the engine (limits below). The skeleton library
// (skeletons/<id>/skeleton.json) is written here (skeletonJson) and read by the converter.
#include "core.hpp"
#include "json.hpp"
#include <array>
#include <cstdio>
#include <memory>
namespace ms {
namespace model {
inline constexpr unsigned maxParts=64,maxBones=512,maxVertices=500000,maxIndices=15000000,maxDraws=64,maxSlot=64,maxMaterials=256,maxTexture=4096,maxCombos=32,maxVars=16,maxPartDraws=1024,maxAllow=4096;
inline constexpr uint64_t maxBlob=768ull*1024*1024;
}

inline uint32_t crc32(const uint8_t* p,size_t n,uint32_t crc=0){
 static const auto table=[]{std::array<uint32_t,256> t{};for(uint32_t i=0;i<256;i++){uint32_t c=i;for(int k=0;k<8;k++)c=(c&1)?0xedb88320u^(c>>1):c>>1;t[i]=c;}return t;}();
 crc=~crc;for(size_t i=0;i<n;i++)crc=table[(crc^p[i])&0xff]^(crc>>8);return ~crc;}

// ---------------------------------------------------------------- JSON field access with limits
// Keys are const char*: a std::string temporary in the call would make every returned reference look dangling to GCC.
inline const Json& jfield(const Json& o,const char* k){require(o.type==Json::Object,std::string("Expected object around \"")+k+"\"");const auto& v=o.at(k);require(v.type!=Json::Null,std::string("Missing \"")+k+"\"");return v;}
inline std::string jstr(const Json& o,const char* k,size_t maxLen){const auto& v=jfield(o,k);require(v.type==Json::String&&!v.string.empty()&&v.string.size()<=maxLen,std::string("Invalid \"")+k+"\"");return v.string;}
inline uint64_t jnum(const Json& v,uint64_t lo,uint64_t hi,const std::string& what){require(v.type==Json::Number&&v.number>=0&&std::floor(v.number)==v.number&&v.number<=9007199254740991.,"Invalid "+what);auto n=uint64_t(v.number);require(n>=lo&&n<=hi,what+" out of range");return n;}
inline uint64_t juint(const Json& o,const char* k,uint64_t lo,uint64_t hi){return jnum(jfield(o,k),lo,hi,std::string("\"")+k+"\"");}
inline int jint(const Json& v,const std::string& what){require(v.type==Json::Number&&std::floor(v.number)==v.number&&std::abs(v.number)<=1000000,"Invalid "+what);return int(v.number);}
inline bool jbool(const Json& o,const char* k,bool fallback){const auto& v=o.at(k);if(v.type==Json::Null)return fallback;require(v.type==Json::Bool,std::string("Invalid \"")+k+"\"");return v.boolean;}
inline const std::vector<Json>& jarr(const Json& o,const char* k,size_t lo,size_t hi){const auto& v=jfield(o,k);require(v.type==Json::Array&&v.array.size()>=lo&&v.array.size()<=hi,std::string("Invalid \"")+k+"\" list");return v.array;}
inline bool safeName(const std::string& s){return !s.empty()&&s.size()<=64&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==s.npos;}
inline bool nameToken(const std::string& s){return s.size()>=3&&s.size()<=40&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789")==s.npos;}
inline std::string jnumber(double v){char b[32];std::snprintf(b,sizeof(b),"%.9g",v);return b;}

// ---------------------------------------------------------------- mesh identity (targets)
inline Fingerprint fingerprintOf(const Json& j){Fingerprint f;require(j.type==Json::Object,"Target object");f.renderer=jstr(j,"renderer",256);
 const auto& m=j.at("mesh");if(m.type!=Json::Null){require(m.type==Json::String&&m.string.size()<=256,"Target mesh");f.mesh=m.string;}
 f.vertices=unsigned(juint(j,"vertices",1,model::maxVertices));f.submeshes=unsigned(juint(j,"submeshes",1,model::maxDraws));f.bones=unsigned(juint(j,"bones",1,model::maxBones));return f;}
inline std::string fingerprintJson(const Fingerprint& f){return "{\"renderer\":"+quote(f.renderer)+",\"mesh\":"+quote(f.mesh)+",\"vertices\":"+std::to_string(f.vertices)+",\"submeshes\":"+std::to_string(f.submeshes)+",\"bones\":"+std::to_string(f.bones)+"}";}

// ---------------------------------------------------------------- variants
inline const std::vector<int>* valuesOf(const Variants& v,const std::string& n){for(auto& y:v.vars)if(y.first==n)return &y.second;return nullptr;}
// Version 1: "combos" (1–32, each sets every variable) and "default" (an index). Version 2: "default" sets every variable.
inline Variants variantsOf(const Json& o,unsigned version){Variants v;
 if(o.type==Json::Null){if(version==1)v.combos.push_back({});return v;}
 require(o.type==Json::Object,"Variants object");
 for(auto& x:jarr(o,"vars",0,model::maxVars)){auto n=jstr(x,"name",64);require(n.size()>1&&n[0]=='$',"Variant name: "+n);for(auto& y:v.vars)require(y.first!=n,"Duplicate variant "+n);
  std::vector<int> values;for(auto& k:jarr(x,"values",1,64)){int value=jint(k,"variant value");require(std::find(values.begin(),values.end(),value)==values.end(),"Duplicate value of "+n);values.push_back(value);}v.vars.push_back({n,values});}
 auto assignment=[&](const Json& c,const char* what){require(c.type==Json::Object,what);Assignment m;
  for(auto& kv:c.object){auto* values=valuesOf(v,kv.first);require(values!=nullptr,"Variant undeclared: "+kv.first);int x=jint(kv.second,"variant value");require(std::find(values->begin(),values->end(),x)!=values->end(),"Variant value not listed: "+kv.first);m[kv.first]=x;}
  require(m.size()==v.vars.size(),std::string(what)+" must set every variable");return m;};
 if(version==1){for(auto& c:jarr(o,"combos",1,model::maxCombos)){auto m=assignment(c,"Variant combination");for(auto& y:v.combos)require(y!=m,"Duplicate variant combination");v.combos.push_back(m);}
  v.defaults=v.combos[size_t(juint(o,"default",0,v.combos.size()-1))];}
 else v.defaults=assignment(jfield(o,"default"),"Variant default");
 return v;}
// Version 2 "when": declared variables, each once; tuples of their listed values.
inline When whenOf(const Json& w,const Variants& v,const std::string& what){When out;if(w.type==Json::Null)return out;require(w.type==Json::Object,what+": when");
 for(auto& n:jarr(w,"vars",1,model::maxVars)){require(n.type==Json::String&&valuesOf(v,n.string)!=nullptr,what+": when names an undeclared variable");require(std::find(out.vars.begin(),out.vars.end(),n.string)==out.vars.end(),what+": when repeats a variable");out.vars.push_back(n.string);}
 for(auto& t:jarr(w,"allow",1,model::maxAllow)){require(t.type==Json::Array&&t.array.size()==out.vars.size(),what+": when tuple size");std::vector<int> tuple;
  for(size_t i=0;i<t.array.size();i++){int x=jint(t.array[i],what+" when value");auto* values=valuesOf(v,out.vars[i]);require(std::find(values->begin(),values->end(),x)!=values->end(),what+": when value not listed for "+out.vars[i]);tuple.push_back(x);}
  out.allow.insert(tuple);}
 return out;}

// A compiled Present loop and frame manifests. Geometry is read per frame, never as a vertex union.
struct AnimationFrame {int value=0;Json parts;};
// kind "present-loop": the counter / modulo / wrap program the converter matched (variable .. counterStep).
// "present-sequence" (0.7.38): what the logic core's rehearsal found; frame values are their indexes, sequence is the
// frame of the start and of each Present after it, and after its end it goes on from loop.
struct AnimationSpec {bool stableWeights=false,sequenced=false;std::string variable;int first=0,last=0,step=1,initial=0;float counter=0,period=1,counterStep=1;std::vector<AnimationFrame> frames;uint64_t bytes=0;
 std::vector<size_t> sequence;size_t loop=0;
 bool present()const{return !frames.empty();}size_t index(int value)const{for(size_t i=0;i<frames.size();i++)if(frames[i].value==value)return i;throw std::runtime_error("Animation frame value is not listed");}};
inline AnimationSpec animationOf(const Json& m){AnimationSpec a;const auto& j=m.at("animation");auto version=juint(m,"version",1,4);
 if(version<4){require(j.type==Json::Null,"Animation requires model version 4");return a;}
 auto kind=jstr(j,"kind",32);require(kind=="present-loop"||kind=="present-sequence","Unsupported animation program");a.stableWeights=jbool(j,"stableWeights",false);a.sequenced=kind=="present-sequence";
 if(!a.sequenced){a.variable=jstr(j,"variable",64);require(a.variable[0]=='$',"Animation variable");
  a.first=jint(jfield(j,"first"),"animation first");a.last=jint(jfield(j,"last"),"animation last");a.step=jint(jfield(j,"step"),"animation step");a.initial=jint(jfield(j,"initial"),"animation initial");
  auto number=[&](const char* key){const auto& n=jfield(j,key);require(n.type==Json::Number&&std::isfinite(n.number)&&std::abs(n.number)<=1000000,std::string("Animation ")+key);return float(n.number);};
  a.counter=number("counter");a.period=number("period");a.counterStep=number("counterStep");require(a.first<=a.last&&a.step>0&&a.period>0&&a.counterStep>0,"Animation loop bounds/period");}
 const auto& variants=m.at("variants");if(variants.type!=Json::Null)require(jarr(variants,"vars",0,0).empty(),"Animation with independent variants is not supported");
 const auto& root=jarr(m,"parts",1,model::maxParts);int expected=a.sequenced?0:a.initial;
 for(auto& f:jarr(j,"frames",1,64)){AnimationFrame frame;frame.value=jint(jfield(f,"value"),"animation frame value");require(frame.value==expected,"Animation frame sequence");frame.parts=jfield(f,"parts");require(frame.parts.type==Json::Array&&frame.parts.array.size()==root.size(),"Animation part count");
  for(size_t i=0;i<root.size();i++){const auto& p=frame.parts.array[i];require(jstr(p,"name",64)==jstr(root[i],"name",64)&&jstr(p,"renderer",256)==jstr(root[i],"renderer",256),"Animation part identity");
   auto n=juint(p,"vertices",1,model::maxVertices),ni=juint(p,"indices",3,model::maxIndices);require(n==juint(root[i],"vertices",1,model::maxVertices)&&juint(p,"bones",1,model::maxBones)==juint(root[i],"bones",1,model::maxBones),"Animation vertex/bone layout");
   const auto& data=jfield(p,"data");uint64_t bytes=n*64+ni*4;for(auto ch:{std::pair<const char*,unsigned>{"uv2",8},{"uv3",8},{"uv4",8},{"colors",4},{"tangents",16}})if(data.at(ch.first).type!=Json::Null)bytes+=n*ch.second;
   a.bytes+=bytes;require(a.bytes<=256ull*1024*1024,"Animation geometry exceeds 256 MiB budget");}
  a.frames.push_back(std::move(frame));expected=a.sequenced?expected+1:expected<a.last?expected+a.step:a.first;}
 if(a.sequenced){for(auto& x:jarr(j,"sequence",1,1u<<20))a.sequence.push_back(size_t(jnum(x,0,a.frames.size()-1,"animation sequence")));a.loop=size_t(juint(j,"loop",0,a.sequence.size()-1));return a;}
 require(expected==a.initial,"Animation frame sequence is not closed");return a;}
struct FrameLoop {AnimationSpec spec;float counter=0;size_t current=0,step=0;uint32_t last=0;bool clock=false;uint64_t advances=0,gaps=0;
 explicit FrameLoop(AnimationSpec a={}):spec(std::move(a)),counter(spec.counter){if(spec.present())current=spec.sequenced?spec.sequence.at(0):spec.index(spec.initial);}
 // Go on from a frame shown by something else (the logic core): in a sequence, from its place in the loop if it has
 // one (else the step stays where it was).
 void showFrom(size_t frame){current=frame;if(!spec.sequenced)return;for(size_t i=spec.loop;i<spec.sequence.size();i++)if(spec.sequence[i]==frame){step=i;return;}for(size_t i=0;i<spec.loop;i++)if(spec.sequence[i]==frame){step=i;return;}}
 bool tick(uint32_t engineFrame){if(!clock){last=engineFrame;clock=true;return false;}uint32_t elapsed=engineFrame-last;if(!elapsed)return false;last=engineFrame;
  if(elapsed>240){gaps++;return false;}bool changed=false;
  // present-sequence: one step of the rehearsed order per Present.
  if(spec.sequenced){for(uint32_t n=0;n<elapsed;n++){step=step+1<spec.sequence.size()?step+1:spec.loop;size_t next=spec.sequence[step];changed|=next!=current;current=next;advances++;}return changed;}
  for(uint32_t n=0;n<elapsed;n++){if(std::fmod(counter,spec.period)==0){int value=spec.frames[current].value;value=value<spec.last?value+spec.step:spec.first;size_t next=spec.index(value);changed|=next!=current;current=next;advances++;}counter+=spec.counterStep;require(std::isfinite(counter),"Animation counter overflow");}return changed;}};

// ---------------------------------------------------------------- manifest summary (what the runtime scans every second)
struct PartSummary {std::string name,renderer;unsigned bones=0;bool hasTarget=false,indexedBones=false;Fingerprint target;};
struct ModelSummary {std::string name,generation;unsigned id=0;bool animated=false;std::vector<std::string> names;std::vector<PartSummary> parts;Variants variants;When replaceWhen;};
inline void checkFormat(const Json& m){require(m.type==Json::Object,"model.json: not an object");require(m.at("format").type==Json::String&&m.at("format").string=="gibox-model","model.json: not a GIBox model");
 auto version=juint(m,"version",1,1000000);require(version>=1&&version<=4,"model.json: unsupported version "+std::to_string(version)+" (made by a newer GIBox?)");}
inline void characterOf(const Json& m,unsigned& id,std::vector<std::string>& names){const auto& c=jfield(m,"character");id=unsigned(juint(c,"id",0,10001999));require(id==0||id>=10000000,"Character ID out of range");
 const auto& n=c.at("names");if(n.type!=Json::Null){require(n.type==Json::Array&&n.array.size()<=16,"Character names");for(auto& x:n.array){require(x.type==Json::String&&nameToken(x.string),"Character name");names.push_back(x.string);}}
 require(id!=0||!names.empty(),"A model needs a character ID or names");}
inline ModelSummary summaryOf(const Json& m){checkFormat(m);ModelSummary s;s.animated=animationOf(m).present();s.name=jstr(m,"name",512);s.generation=jstr(m,"generation",64);characterOf(m,s.id,s.names);std::set<std::string> seen;
 auto version=unsigned(juint(m,"version",1,4));s.variants=variantsOf(m.at("variants"),version);require(version>=3||m.at("replaceWhen").type==Json::Null,"replaceWhen requires model version 3");s.replaceWhen=whenOf(m.at("replaceWhen"),s.variants,"replacement");
 for(auto& p:jarr(m,"parts",1,model::maxParts)){PartSummary ps;ps.name=jstr(p,"name",64);require(safeName(ps.name)&&seen.insert(ps.name).second,"Part name: "+ps.name);ps.renderer=jstr(p,"renderer",256);
  ps.bones=unsigned(juint(p,"bones",1,model::maxBones));ps.indexedBones=p.at("skeleton").type==Json::Null;if(p.at("target").type!=Json::Null){ps.target=fingerprintOf(p.at("target"));ps.hasTarget=true;}s.parts.push_back(ps);}
 return s;}
inline std::string manifestText(const std::string& dir){auto b=read(dir+"/model.json",4*1024*1024);return std::string(b.begin(),b.end());}
inline ModelSummary readSummary(const std::string& dir){return summaryOf(JsonParser(manifestText(dir)).parse());}

// ---------------------------------------------------------------- full model
struct Geometry {unsigned n=0,ni=0;Bytes positions,normals,uvs,indices;
 const uint8_t* pos()const{return positions.data();}const uint8_t* norm()const{return normals.data();}const uint8_t* uv()const{return uvs.data();}const uint8_t* idx()const{return indices.data();}};
// One draw of a part: an index range, the material in a renderer slot, and when it is drawn.
struct DrawSpec {Submesh sub;Material row;When when;};
// The draws in effect for an assignment, in file order; none: one empty draw, so the part shows nothing (at most 64).
inline void activeDraws(const std::vector<DrawSpec>& draws,const Assignment& a,std::vector<Submesh>& subs,std::vector<Material>& rows){subs.clear();rows.clear();
 for(auto& d:draws)if(drawn(d.when,a)){subs.push_back(d.sub);rows.push_back(d.row);}
 if(subs.empty()&&!draws.empty()){subs.push_back({0,0});rows.push_back({draws[0].row.name,0});}
 require(subs.size()<=64,"More than 64 draws at once");}
// One submesh of a part: the index ranges it draws, in draw order, and the material row of the renderer slot it uses.
struct MergedDraw {Material row;std::vector<Submesh> ranges;unsigned count=0;};
// The draws in effect as submeshes. merge: neighbours with the same row (material name and slot, so the same Material on
// the renderer) share one submesh, their ranges kept in draw order; draws hidden by the assignment are not in `subs`, so
// what lay between two merged ranges is never drawn. Only neighbours merge (A B A stays three submeshes), so no draw
// moves past another. Ranges that continue each other in the index buffer become one range.
inline std::vector<MergedDraw> mergeDraws(const std::vector<Submesh>& subs,const std::vector<Material>& rows,bool merge){require(subs.size()==rows.size(),"Draws and material rows differ");
 std::vector<MergedDraw> out;
 for(size_t i=0;i<subs.size();i++){
  if(!merge||out.empty()||out.back().row.name!=rows[i].name||out.back().row.slot!=rows[i].slot){out.push_back({rows[i],{subs[i]},subs[i].count});continue;}
  auto& d=out.back();if(!subs[i].count)continue;auto& last=d.ranges.back();
  if(!last.count)last=subs[i];else if(last.start+last.count==subs[i].start)last.count+=subs[i].count;else d.ranges.push_back(subs[i]);
  d.count+=subs[i].count;}
 return out;}
// A submesh's indices (4 bytes each), its ranges joined in order.
inline Bytes joinedIndices(const Bytes& indices,const MergedDraw& d){Bytes out;out.reserve(4ull*d.count);
 for(auto& r:d.ranges){require(4ull*(uint64_t(r.start)+r.count)<=indices.size(),"Draw outside the index buffer");out.insert(out.end(),indices.begin()+4ull*r.start,indices.begin()+4ull*(uint64_t(r.start)+r.count));}
 return out;}
// Historical index-only builds recorded names only for the indices they used. Unused rows are deliberately empty.
// This is an observation of a successful mapping, not a complete skeleton or a bindpose backup.
inline std::vector<std::string> readLegacyBoneNames(const std::string& file,unsigned palette){
 require(palette>0&&palette<=model::maxBones,"Legacy bone palette size");auto bytes=read(file,model::maxBones*320);std::istringstream input(std::string(bytes.begin(),bytes.end()));std::string row;
 require(bool(std::getline(input,row)),"Legacy bonemap header missing");if(!row.empty()&&row.back()=='\r')row.pop_back();require(row=="source_index\ttarget_index\tsource_name\ttarget_name","Legacy bonemap header");
 std::vector<std::string> names(palette);std::vector<bool> seen(palette);unsigned count=0;
 while(std::getline(input,row)){if(!row.empty()&&row.back()=='\r')row.pop_back();if(row.empty())continue;std::array<std::string,4> fields;size_t start=0;
  for(unsigned c=0;c<3;c++){auto at=row.find('\t',start);require(at!=row.npos,"Legacy bonemap columns");fields[c]=row.substr(start,at-start);start=at+1;}fields[3]=row.substr(start);require(fields[3].find('\t')==row.npos,"Legacy bonemap extra column");
  auto index=decimal(fields[0]);require(index<palette&&!seen[index]&&fields[2]=="#"+std::to_string(index),"Legacy bonemap source index");seen[index]=true;count++;
  if(fields[1]=="-1"){require(fields[3].empty(),"Legacy unmapped bone name");continue;}
  require(decimal(fields[1])==index&&!trim(fields[3]).empty()&&fields[3].size()<=256&&fields[3].find('\0')==row.npos,"Legacy bonemap target");names[index]=fields[3];
 }
 require(count==palette,"Legacy bonemap incomplete palette");return names;}
struct PartFile {Part spec;Geometry geo;Bytes weights;std::set<unsigned> used;std::vector<DrawSpec> draws;std::vector<std::pair<std::string,Bytes>> channels;std::vector<std::string> sourceBones;std::vector<int> sourceParents;Bytes sourceBinds;
 std::vector<std::string> legacyBoneNames;std::string legacyBoneError;};
// Where a texture sits in model.bin (checked against the file when the manifest is read); its pixels are read only for
// the materials an assignment draws (readTextures), so a model with many variants does not hold every texture at once.
struct TexRef {unsigned w=0,h=0;uint64_t off=0,len=0;bool present()const{return len>0;}};
struct MaterialFile {bool original=true;std::shared_ptr<Texture> main,light,normal;TexRef mainRef,lightRef,normalRef;
 bool loaded()const{return original||main!=nullptr;}};
// A texture swap (model.json "textures", optional): the main texture of the character's renderer of that name becomes the
// material's, its mesh stays the game's (a mod's face texture, which on PC replaces the game's texture wherever it is drawn).
struct TextureSwap {std::string renderer,material;};
// model.json "logic" (optional, animation models): for the 3DMigoto logic core in the game. Signature 2 (0.7.35):
// the mod's INI files (copied to logic/, each with the namespace XXMI gives it); every [Resource] by the core's key
// with its identity (file under the mod and layout, resolved by the converter); the draws replayed every frame (slot
// -> game hash, the order the slots are checked, draw values); per part where each field of its draws is read
// ([pass, slot]; the part's own pass means each of its issued draws); and the choices the model was converted with,
// each with every part's signature: per animation frame ("frames"), or per distinct outfit with its values of the
// variant variables ("variants", static models). The runtime shows the choice whose signatures all match. Older logic data (0.7.34, no "signature") loads
// but is never used: it names resources by section only and has no draw ranges (unsupported says so).
struct LogicSpec {
 struct File {std::string ns,file,text;};
 struct Pass {std::string name;std::map<std::string,uint32_t> bindings;std::vector<std::string> checks;std::map<std::string,float> call;};
 struct Field {std::string name,pass,slot;};
 std::vector<File> files;std::map<std::string,std::string> resources;std::vector<Pass> passes;
 std::map<std::string,std::vector<Field>> parts;            // part -> its fields; the part's pass is the one named like it
 std::vector<std::map<std::string,std::string>> choices;    // per animation frame, or per outfit: part -> signature
 std::vector<Assignment> assignments;                       // outfits: each choice's values of the variant variables
 std::string mainNs;                                        // the main INI's namespace (variant variables live there)
 // The [Key] sections the App offers as buttons (cycle and toggle; 0.7.37): the file they are in, the section as
 // that file writes it, their type. A press goes to the core as a key event, never straight to a variable.
 struct Key {size_t file=0;std::string section,type;bool back=false;};std::vector<Key> keys;   // back: a cycle with back = (the App's "previous")
 // An outfit model whose Present program follows the game (clock, its draws; 0.7.40): the converter could not show
 // it finite, converted every outfit its draws can show, and the core picks among them by real time and keys.
 bool driven=false;
 std::string unsupported;
 bool present()const{return !files.empty()||!unsupported.empty();}
};
inline const std::vector<std::string>& logicFields(){static const std::vector<std::string> f={"ib","position","blend","texcoord","ps-t0","ps-t1","ps-t2"};return f;}
inline bool logicSlot(const std::string& s){return !s.empty()&&s.size()<=16&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==s.npos;}
inline LogicSpec logicOf(const Json& m,const std::string& dir,const AnimationSpec& animation,const Variants& variants){LogicSpec l;const auto& j=m.at("logic");if(j.type==Json::Null)return l;
 require(j.type==Json::Object,"logic: not an object");
 if(j.at("signature").type==Json::Null){require(animation.present(),"logic: only for animation models");l.unsupported="logic data of 0.7.34 (resources by section name only, no draw ranges or hidden parts); convert the mod again";return l;}
 require(juint(j,"signature",2,2)==2,"logic: signature version");uint64_t total=0;std::set<std::string> files;l.mainNs=jstr(j,"main",512);
 for(auto& f:jarr(j,"files",1,64)){LogicSpec::File x{jstr(f,"namespace",512),jstr(f,"file",32),""};
  require(x.file.compare(0,6,"logic/")==0&&x.file.size()>10&&x.file.compare(x.file.size()-4,4,".ini")==0&&x.file.find_first_not_of("0123456789",6)==x.file.size()-4,"logic: file name "+x.file);
  require(files.insert(x.file).second,"logic: file listed twice");auto b=read(dir+"/"+x.file,4*1024*1024);x.text.assign(b.begin(),b.end());total+=x.text.size();require(total<=8*1024*1024,"logic: INI files over 8 MiB");
  l.files.push_back(std::move(x));}
 const auto& res=jfield(j,"resources");require(res.type==Json::Object&&res.object.size()<=16384,"logic: resources");
 for(auto& kv:res.object){require(!kv.first.empty()&&kv.first.size()<=600&&kv.second.type==Json::String&&!kv.second.string.empty()&&kv.second.string.size()<=1024,"logic: resource "+kv.first);l.resources[kv.first]=kv.second.string;}
 std::set<std::string> names;
 for(auto& p:jarr(j,"passes",1,256)){LogicSpec::Pass x;x.name=jstr(p,"name",128);require(names.insert(x.name).second,"logic: pass "+x.name+" listed twice");
  const auto& b=jfield(p,"bindings");require(b.type==Json::Object&&b.object.size()<=32,"logic: bindings of "+x.name);
  for(auto& kv:b.object){require(logicSlot(kv.first)&&kv.second.type==Json::String&&!kv.second.string.empty()&&kv.second.string.size()<=8&&kv.second.string.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos,"logic: binding "+kv.first+" of "+x.name);
   x.bindings[kv.first]=uint32_t(std::stoul(kv.second.string,nullptr,16));}
  for(auto& c:jarr(p,"checks",1,32)){require(c.type==Json::String&&logicSlot(c.string),"logic: checks of "+x.name);x.checks.push_back(c.string);}
  const auto& call=p.at("call");if(call.type!=Json::Null){require(call.type==Json::Object&&call.object.size()<=6,"logic: call of "+x.name);
   static const std::set<std::string> keys={"vertex_count","index_count","instance_count","first_vertex","first_index","first_instance"};
   for(auto& kv:call.object){require(keys.count(kv.first),"logic: call value "+kv.first);x.call[kv.first]=float(jnum(kv.second,0,0xffffffffull,"logic: "+kv.first));}}
  l.passes.push_back(std::move(x));}
 const auto& parts=jfield(j,"fields");require(parts.type==Json::Object&&!parts.object.empty()&&parts.object.size()<=256,"logic: fields");
 for(auto& d:parts.object){require(names.count(d.first)&&d.second.type==Json::Object,"logic: part "+d.first+" has no pass of its own");auto& list=l.parts[d.first];
  for(auto& f:logicFields()){const auto& at=jfield(d.second,f.c_str());require(at.type==Json::Array&&at.array.size()==2&&at.array[0].type==Json::String&&names.count(at.array[0].string)&&at.array[1].type==Json::String&&logicSlot(at.array[1].string),"logic: "+d.first+" "+f);
   list.push_back({f,at.array[0].string,at.array[1].string});}
  require(d.second.object.size()==logicFields().size(),"logic: unknown field of "+d.first);}
 auto partsOf=[&](const Json& d,const std::string& what){require(d.type==Json::Object&&d.object.size()==l.parts.size(),"logic: parts of "+what);std::map<std::string,std::string> sig;
  for(auto& kv:d.object){require(l.parts.count(kv.first)&&kv.second.type==Json::String&&!kv.second.string.empty()&&kv.second.string.size()<=65536,"logic: part "+kv.first+" of "+what);sig[kv.first]=kv.second.string;}
  return sig;};
 if(animation.present()){require(j.at("variants").type==Json::Null,"logic: an animation has frames, not variants");
  const auto& frames=jarr(j,"frames",animation.frames.size(),animation.frames.size());
  for(size_t i=0;i<frames.size();i++){require(jint(jfield(frames[i],"value"),"logic frame value")==animation.frames[i].value,"logic: frame values differ from the animation");l.choices.push_back(partsOf(jfield(frames[i],"draws"),"frame "+std::to_string(i)));}}
 else{require(!variants.vars.empty(),"logic: only for animation or outfit models");
  for(auto& v:jarr(j,"variants",1,256)){const auto& a=jfield(v,"assignment");require(a.type==Json::Object&&a.object.size()==variants.vars.size(),"logic: outfit assignment");Assignment x;
   for(auto& var:variants.vars){const auto& n=a.at(var.first);int value=jint(n,"logic: "+var.first);require(std::find(var.second.begin(),var.second.end(),value)!=var.second.end(),"logic: "+var.first+" = "+std::to_string(value)+" is not a converted value");x[var.first]=value;}
   l.assignments.push_back(x);l.choices.push_back(partsOf(jfield(v,"draws"),"outfit "+std::to_string(l.choices.size())));}}
 l.driven=jbool(j,"driven",false);require(!l.driven||!animation.present(),"logic: driven is for outfit models");
 const auto& keys=j.at("keys");if(keys.type!=Json::Null)for(auto& k:jarr(j,"keys",0,256)){LogicSpec::Key x;x.file=size_t(juint(k,"file",0,l.files.size()-1));x.section=jstr(k,"section",256);x.type=jstr(k,"type",16);x.back=jbool(k,"back",false);
  require(x.section.size()>3&&(x.section[0]|32)=='k'&&(x.section[1]|32)=='e'&&(x.section[2]|32)=='y'&&x.section.find_first_of("\\[]\r\n")==std::string::npos,"logic: key section "+x.section);require(x.type=="cycle"||x.type=="toggle"||x.type=="activate"||x.type=="hold","logic: key type "+x.type);l.keys.push_back(std::move(x));}
 return l;}
struct ModelFile {std::string name,path,generation;unsigned id=0;std::vector<std::string> names;bool originalMaterials=false,diffuseOnly=false;Variants variants;std::vector<PartFile> parts;std::map<std::string,MaterialFile> materials;uint64_t blobSize=0;uint32_t blobCrc=0;AnimationSpec animation;std::vector<TextureSwap> swaps;LogicSpec logic;};

namespace detail {
// model.bin, checked whole (size, CRC-32 streamed in 1 MiB steps) before any view is read from it; views are then read
// one by one, so a model never sits in memory twice.
class Blob {std::ifstream f;uint64_t n=0;public:
 // verify=false: the same file (inode, time, size) was checked whole before (runtime cache); size still checked.
 Blob(const std::string& path,uint64_t size,uint32_t crc,bool verify=true):f(path,std::ios::binary|std::ios::ate){
  require(bool(f),"Cannot read: "+path);auto end=f.tellg();require(end>=0&&uint64_t(end)==size,"model.bin: size differs from model.json (copied incompletely?)");n=size;f.seekg(0);if(!verify)return;
  std::vector<uint8_t> buf(1<<20);uint32_t c=0;for(uint64_t left=n;left;){auto k=size_t(std::min<uint64_t>(left,buf.size()));f.read(reinterpret_cast<char*>(buf.data()),k);require(bool(f),"Short read: "+path);c=crc32(buf.data(),k,c);left-=k;}
  require(c==crc,"model.bin: checksum mismatch (damaged file)");}
 uint64_t size()const{return n;}
 Bytes read(uint64_t off,uint64_t len){Bytes b(size_t(len),0);if(len){f.seekg(std::streamoff(off));f.read(reinterpret_cast<char*>(b.data()),std::streamsize(len));require(bool(f),"Short read: model.bin");}return b;}
};
// [offset, length] into model.bin: 16-byte aligned, exact length, inside the file.
inline Bytes view(const Json& v,Blob& blob,uint64_t expect,const std::string& what){
 require(v.type==Json::Array&&v.array.size()==2,"View "+what);auto off=jnum(v.array[0],0,blob.size(),what+" offset"),len=jnum(v.array[1],0,blob.size(),what+" length");
 require(off%16==0,what+": misaligned");require(len==expect,what+": wrong length");require(off+len<=blob.size(),what+": outside model.bin");return blob.read(off,len);}
inline void finite(const Bytes& b,const std::string& what){for(size_t i=0;i+4<=b.size();i+=4)require(std::isfinite(f32(b.data()+i)),what+": not a finite number");}
inline TexRef textureRef(const Json& t,uint64_t size,const std::string& what){
 auto w=unsigned(juint(t,"width",1,model::maxTexture)),h=unsigned(juint(t,"height",1,model::maxTexture));const auto& v=jfield(t,"data");
 require(v.type==Json::Array&&v.array.size()==2,"View "+what);auto off=jnum(v.array[0],0,size,what+" offset"),len=jnum(v.array[1],0,size,what+" length");
 require(off%16==0,what+": misaligned");require(len==4ull*w*h,what+": wrong length");require(off+len<=size,what+": outside model.bin");return {w,h,off,len};}
inline std::shared_ptr<Texture> texture(const Json& t,Blob& blob,const std::string& what){
 auto w=unsigned(juint(t,"width",1,model::maxTexture)),h=unsigned(juint(t,"height",1,model::maxTexture));
 return std::make_shared<Texture>(Texture{w,h,view(jfield(t,"data"),blob,4ull*w*h,what)});}
}

/** Reads and checks a model folder. Nothing partially valid is returned: any problem throws with what and where. */
// allTextures=false: geometry and texture positions only; readTextures() fetches the pixels that are needed.
inline std::shared_ptr<ModelFile> loadModelFile(const std::string& dir,bool allTextures=true,bool verify=true,int animationFrame=-1,const std::string& expectedGeneration=""){
 auto m=JsonParser(manifestText(dir)).parse();checkFormat(m);
 if(!expectedGeneration.empty())require(jstr(m,"generation",64)==expectedGeneration,"Animation conversion changed while loading");
 auto animation=animationOf(m);if(animationFrame>=0){require(size_t(animationFrame)<animation.frames.size(),"Animation frame index");m.object["parts"]=animation.frames[size_t(animationFrame)].parts;}
 auto summary=summaryOf(m);
 auto out=std::make_shared<ModelFile>();out->name=summary.name;out->path=dir;out->generation=summary.generation;out->id=summary.id;out->names=summary.names;
 const auto& flags=m.at("flags");if(flags.type!=Json::Null){out->originalMaterials=jbool(flags,"originalMaterials",false);out->diffuseOnly=jbool(flags,"diffuseOnly",false);}
 auto version=unsigned(juint(m,"version",1,4));out->variants=variantsOf(m.at("variants"),version);
 // The data file: exact size and CRC-32 before any view is taken from it.
 const auto& blobInfo=jfield(m,"blob");require(jstr(blobInfo,"file",64)=="model.bin","blob.file must be model.bin");
 auto size=juint(blobInfo,"size",1,model::maxBlob);auto crc=uint32_t(juint(blobInfo,"crc32",0,0xffffffffull));
 detail::Blob blob(dir+"/model.bin",size,crc,verify);
 // Materials first: draws refer to them by name.
 for(auto& mt:jarr(m,"materials",1,model::maxMaterials)){auto name=jstr(mt,"name",64);require(safeName(name)&&!out->materials.count(name),"Material name: "+name);MaterialFile f;f.original=jbool(mt,"original",false);
  if(!f.original){f.mainRef=detail::textureRef(jfield(mt,"main"),size,name+".main");
   if(mt.at("lightmap").type!=Json::Null){f.lightRef=detail::textureRef(mt.at("lightmap"),size,name+".lightmap");}
   if(mt.at("normalmap").type!=Json::Null){f.normalRef=detail::textureRef(mt.at("normalmap"),size,name+".normalmap");}
   if(out->diffuseOnly){f.lightRef={};f.normalRef={};}
   if(allTextures){f.main=std::make_shared<Texture>(Texture{f.mainRef.w,f.mainRef.h,blob.read(f.mainRef.off,f.mainRef.len)});
    if(f.lightRef.present())f.light=std::make_shared<Texture>(Texture{f.lightRef.w,f.lightRef.h,blob.read(f.lightRef.off,f.lightRef.len)});
    if(f.normalRef.present())f.normal=std::make_shared<Texture>(Texture{f.normalRef.w,f.normalRef.h,blob.read(f.normalRef.off,f.normalRef.len)});}}
  out->materials[name]=f;}
 if(m.at("textures").type!=Json::Null)for(auto& t:jarr(m,"textures",1,8)){TextureSwap x{jstr(t,"renderer",64),jstr(t,"material",64)};auto it=out->materials.find(x.material);
  require(safeName(x.renderer),"Texture swap renderer: "+x.renderer);require(it!=out->materials.end()&&!it->second.original,"Texture swap material: "+x.material);
  for(auto& y:out->swaps){require(y.renderer!=x.renderer,"Texture swap renderer listed twice: "+x.renderer);}
  out->swaps.push_back(x);}
 out->blobSize=size;out->blobCrc=crc;if(animationFrame<0){out->logic=logicOf(m,dir,animation,out->variants);out->animation=std::move(animation);}
 const auto& parts=jarr(m,"parts",1,model::maxParts);
 for(size_t pi=0;pi<parts.size();pi++){const auto& p=parts[pi];const auto& ps=summary.parts[pi];PartFile f;
  unsigned n=unsigned(juint(p,"vertices",1,model::maxVertices)),ni=unsigned(juint(p,"indices",3,model::maxIndices));require(ni%3==0,ps.name+": index count not a multiple of 3");
  f.spec={ps.name,ps.renderer,0,ps.bones};f.geo.n=n;f.geo.ni=ni;const auto& d=jfield(p,"data");auto at=[&](const char* k,uint64_t len){return detail::view(jfield(d,k),blob,len,ps.name+"."+k);};
  f.geo.positions=at("positions",12ull*n);f.geo.normals=at("normals",12ull*n);f.geo.uvs=at("uv",8ull*n);f.geo.indices=at("indices",4ull*ni);f.weights=at("weights",32ull*n);
  detail::finite(f.geo.positions,ps.name+".positions");detail::finite(f.geo.normals,ps.name+".normals");detail::finite(f.geo.uvs,ps.name+".uv");
  for(unsigned i=0;i<ni;i++)require(u32(f.geo.idx()+4ull*i)<n,ps.name+".indices: vertex index out of range");
  // Skin: four weights (finite, non-negative, sum 1) then four bone indices (inside the part's skeleton) per vertex.
  for(unsigned i=0;i<n;i++){const uint8_t* w=f.weights.data()+32ull*i;float sum=0;
   for(int j=0;j<4;j++){float x=f32(w+4*j);unsigned b=u32(w+16+4*j);require(std::isfinite(x)&&x>=0,ps.name+".weights: invalid weight");require(b<ps.bones,ps.name+".weights: bone index out of range");sum+=x;if(x>0)f.used.insert(b);}
   require(sum>=.999f&&sum<=1.001f,ps.name+".weights: weights do not sum to 1");}
  for(auto ch:{std::pair<const char*,unsigned>{"uv2",8},{"uv3",8},{"uv4",8},{"colors",4},{"tangents",16}}){
   if(d.at(ch.first).type==Json::Null){continue;}auto b=at(ch.first,uint64_t(ch.second)*n);if(ch.second!=4){detail::finite(b,ps.name+"."+ch.first);}f.channels.push_back({std::string(".")+ch.first,std::move(b)});}
  const auto& sk=p.at("skeleton");
  if(sk.type!=Json::Null){for(auto& b:jarr(sk,"bones",ps.bones,ps.bones)){require(b.type==Json::String&&!b.string.empty()&&b.string.size()<=256&&b.string.find_first_of("\t\r\n")==b.string.npos,ps.name+": bone name");f.sourceBones.push_back(b.string);}
   f.sourceBinds=detail::view(jfield(sk,"bindposes"),blob,64ull*ps.bones,ps.name+".bindposes");detail::finite(f.sourceBinds,ps.name+".bindposes");
   // Optional: each bone's parent in this list (-1: none), so a bone the game lacks can hand its weight to an ancestor.
   if(sk.at("parents").type!=Json::Null){const auto& pa=jarr(sk,"parents",ps.bones,ps.bones);
    for(size_t i=0;i<pa.size();i++){int x=jint(pa[i],ps.name+": bone parent");require(x>=-1&&x<int(ps.bones)&&x!=int(i),ps.name+": bone parent out of range");f.sourceParents.push_back(x);}
    for(size_t i=0;i<f.sourceParents.size();i++){size_t steps=0;for(int k=int(i);k>=0;k=f.sourceParents[size_t(k)])require(++steps<=ps.bones,ps.name+": bone parents form a loop");}}}
  auto drawOf=[&](const Json& dr){auto start=unsigned(juint(dr,"start",0,ni));auto count=unsigned(juint(dr,"count",0,ni));require(count<=ni-start&&count%3==0,ps.name+": draw range");
   auto mat=jstr(dr,"material",64);require(out->materials.count(mat),ps.name+": unknown material "+mat);return DrawSpec{{start,count},{mat,unsigned(juint(dr,"slot",0,model::maxSlot-1))},{}};};
  if(version==1){
   // One draw list per combination; read as draws that each know the combinations they belong to.
   const auto& lists=jarr(p,"draws",out->variants.combos.size(),out->variants.combos.size());std::vector<std::string> names;for(auto& y:out->variants.vars)names.push_back(y.first);
   for(size_t c=0;c<lists.size();c++){const auto& list=lists[c];require(list.type==Json::Array&&!list.array.empty()&&list.array.size()<=model::maxDraws,ps.name+": draws");
    std::vector<int> tuple;for(auto& n:names)tuple.push_back(out->variants.combos[c].at(n));
    for(auto& dr:list.array){auto d=drawOf(dr);DrawSpec* same=nullptr;for(auto& x:f.draws)if(x.sub.start==d.sub.start&&x.sub.count==d.sub.count&&x.row.name==d.row.name&&x.row.slot==d.row.slot)same=&x;
     if(!same){d.when.vars=names;f.draws.push_back(d);same=&f.draws.back();}same->when.allow.insert(tuple);}}
   for(auto& d:f.draws)if(d.when.allow.size()==out->variants.combos.size()){d.when.vars.clear();d.when.allow.clear();}}
  else for(auto& dr:jarr(p,"draws",1,model::maxPartDraws)){auto d=drawOf(dr);d.when=whenOf(dr.at("when"),out->variants,ps.name);f.draws.push_back(d);}
  out->parts.push_back(std::move(f));}
 if(!expectedGeneration.empty())require(jstr(JsonParser(manifestText(dir)).parse(),"generation",64)==expectedGeneration,"Animation conversion changed during frame read");
 return out;}
// Pixels for the named materials, read from model.bin later on (a variant switch, or a build whose variant changed after
// the preload). The folder must still be the same conversion: same generation and the same model.bin size.
inline void readTextures(const std::string& dir,const std::string& generation,uint64_t blobSize,std::map<std::string,MaterialFile>& materials,const std::set<std::string>& names){
 bool any=false;for(auto& n:names){auto it=materials.find(n);if(it!=materials.end()&&!it->second.loaded())any=true;}if(!any)return;
 auto m=JsonParser(manifestText(dir)).parse();require(jstr(m,"generation",64)==generation,"model.json changed: convert again or reload");
 std::ifstream f(dir+"/model.bin",std::ios::binary|std::ios::ate);require(bool(f),"Cannot read: "+dir+"/model.bin");auto end=f.tellg();require(end>=0&&uint64_t(end)==blobSize,"model.bin changed: convert again or reload");
 auto get=[&](const TexRef& r){Bytes b(size_t(r.len),0);f.seekg(std::streamoff(r.off));f.read(reinterpret_cast<char*>(b.data()),std::streamsize(r.len));require(bool(f),"Short read: model.bin");return std::make_shared<Texture>(Texture{r.w,r.h,std::move(b)});};
 for(auto& n:names){auto it=materials.find(n);if(it==materials.end()||it->second.loaded())continue;auto& mf=it->second;
  mf.main=get(mf.mainRef);if(mf.lightRef.present())mf.light=get(mf.lightRef);if(mf.normalRef.present())mf.normal=get(mf.normalRef);}}
// The materials an assignment draws, over all parts.
inline std::set<std::string> materialsDrawn(const std::vector<PartFile>& parts,const Assignment& a){std::set<std::string> out;std::vector<Submesh> subs;std::vector<Material> rows;
 for(auto& p:parts){activeDraws(p.draws,a,subs,rows);for(auto& r:rows)out.insert(r.name);}return out;}
// ...and the textures swapped onto the game's renderers, which every assignment uses.
inline std::set<std::string> materialsNeeded(const ModelFile& m,const Assignment& a){auto out=materialsDrawn(m.parts,a);for(auto& x:m.swaps)out.insert(x.material);return out;}

// ---------------------------------------------------------------- learned.json (runtime-written)
struct Learned {unsigned id=0;std::map<std::string,Fingerprint> targets;};
inline Learned learnedOf(const Json& j){Learned l;require(j.type==Json::Object&&j.at("format").type==Json::String&&j.at("format").string=="gibox-learned","learned.json: format");
 if(j.at("id").type!=Json::Null){l.id=unsigned(juint(j,"id",0,10001999));require(l.id==0||l.id>=10000000,"learned.json: id");}
 const auto& t=j.at("targets");if(t.type!=Json::Null){require(t.type==Json::Object&&t.object.size()<=model::maxParts,"learned.json: targets");for(auto& kv:t.object){require(safeName(kv.first),"learned.json: part");l.targets[kv.first]=fingerprintOf(kv.second);}}
 return l;}
inline Learned readLearned(const std::string& dir){auto f=dir+"/learned.json";if(!exists(f))return {};try{return learnedOf(JsonParser(text(f)).parse());}catch(const std::exception&){return {};}}
inline std::string learnedJson(const Learned& l){std::string s="{\"format\":\"gibox-learned\",\"version\":1,\"id\":"+std::to_string(l.id)+",\"targets\":{";bool first=true;
 for(auto& kv:l.targets){s+=(first?"":",")+quote(kv.first)+":"+fingerprintJson(kv.second);first=false;}return s+"}}\n";}

// ---------------------------------------------------------------- skeleton library (runtime-written, read by the converter)
struct SkeletonRenderer {std::string name;Fingerprint target;std::vector<std::string> bones;std::vector<int> parents;Bytes bindposes;};
// A capture is accepted as a whole. A renderer with a missing bone, cyclic hierarchy or unusable matrix must not
// silently disappear from a library record while another renderer makes the export look successful.
inline void validateSkeleton(unsigned id,const std::vector<SkeletonRenderer>& renderers){
 require(id>=10000000&&id<=10001999&&!renderers.empty()&&renderers.size()<=64,"Skeleton character/renderers");std::set<std::string> names;
 for(auto& r:renderers){size_t n=r.bones.size();require(safeName(r.name)&&names.insert(r.name).second,"Skeleton renderer name");
  require(n>0&&n<=512&&r.parents.size()==n&&r.bindposes.size()==64*n&&r.target.bones==n,"Skeleton bone/bindpose count: "+r.name);
  require(r.target.vertices>0&&r.target.vertices<=model::maxVertices&&r.target.submeshes>0&&r.target.submeshes<=64&&!lowerLod(r.target.mesh),"Skeleton mesh: "+r.name);
  for(size_t i=0;i<n;i++){auto& bn=r.bones[i];require(!bn.empty()&&bn.size()<=256&&bn.find_first_of("\t\r\n")==bn.npos,"Skeleton bone name: "+r.name);
   int p=r.parents[i];require(p>=-1&&(p<0||size_t(p)<n),"Skeleton parent range: "+r.name);
   size_t steps=0;for(int k=int(i);k>=0;k=r.parents[size_t(k)]){require(size_t(k)<n&&++steps<=n,"Skeleton parent cycle: "+r.name);}
   double a[16];for(int j=0;j<16;j++){a[j]=f32(r.bindposes.data()+64*i+4*j);require(std::isfinite(a[j]),"Skeleton non-finite bindpose: "+r.name);}
   double det=a[0]*(a[5]*a[10]-a[9]*a[6])-a[4]*(a[1]*a[10]-a[9]*a[2])+a[8]*(a[1]*a[6]-a[5]*a[2]);
   require(std::isfinite(det)&&std::abs(det)>1e-12&&std::abs(a[15])>1e-12,"Skeleton singular bindpose: "+r.name);
  }
 }
}
// Read back by the runtime: a character showing only a lower level of detail (the world Traveler on some devices) can
// build on it when its bones and bindposes are exactly the full mesh's recorded here.
inline std::vector<SkeletonRenderer> readSkeleton(const std::string& file,std::string* root=nullptr){auto b=read(file,64*1024*1024);auto j=JsonParser(std::string(b.begin(),b.end())).parse();
 if(root){*root=j.at("root").type==Json::String?j.at("root").string:"";}
 require(j.type==Json::Object&&j.at("format").type==Json::String&&j.at("format").string=="gibox-skeleton","skeleton.json: not a GIBox skeleton");std::vector<SkeletonRenderer> out;
 for(auto& r:jarr(j,"renderers",1,64)){SkeletonRenderer x;x.name=jstr(r,"name",256);x.target=fingerprintOf(jfield(r,"target"));
  for(auto& n:jarr(r,"bones",1,512)){require(n.type==Json::String,"skeleton.json: bone name");x.bones.push_back(n.string);}
  for(auto& p:jarr(r,"bindposes",16*x.bones.size(),16*x.bones.size())){require(p.type==Json::Number,"skeleton.json: bindpose");float f=float(p.number);auto at=x.bindposes.size();x.bindposes.resize(at+4);std::memcpy(x.bindposes.data()+at,&f,4);}
  out.push_back(std::move(x));}
 return out;}
// Exact, bounded comparison of successive engine captures; matrix bytes are kept out of JSON work on the Tick.
inline std::string skeletonStamp(const std::string& root,const std::vector<SkeletonRenderer>& renderers){std::string out=quote(root);
 for(auto& r:renderers){out+=quote(r.name)+fingerprintJson(r.target);for(size_t i=0;i<r.bones.size();i++)out+=quote(r.bones[i])+":"+std::to_string(r.parents[i])+";";
  out.append(reinterpret_cast<const char*>(r.bindposes.data()),r.bindposes.size());}return out;}
inline std::string skeletonJson(unsigned id,const std::string& root,const std::vector<SkeletonRenderer>& renderers){
 validateSkeleton(id,renderers);
 std::string s="{\"format\":\"gibox-skeleton\",\"version\":1,\"id\":"+std::to_string(id)+",\"root\":"+quote(root)+",\"renderers\":[";
 for(size_t r=0;r<renderers.size();r++){auto& x=renderers[r];require(x.bindposes.size()==64*x.bones.size()&&x.parents.size()==x.bones.size(),"Skeleton record");
  s+=std::string(r?",":"")+"{\"name\":"+quote(x.name)+",\"target\":"+fingerprintJson(x.target)+",\"bones\":[";
  for(size_t i=0;i<x.bones.size();i++){s+=std::string(i?",":"")+quote(x.bones[i]);}s+="],\"parents\":[";
  for(size_t i=0;i<x.parents.size();i++){s+=std::string(i?",":"")+std::to_string(x.parents[i]);}s+="],\"bindposes\":[";
  for(size_t i=0;i<x.bindposes.size();i+=4){float f=f32(x.bindposes.data()+i);require(std::isfinite(f),"Skeleton bindpose");s+=std::string(i?",":"")+jnumber(f);}s+="]}";}
 return s+"]}\n";}
}
