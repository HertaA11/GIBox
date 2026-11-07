// Texture-only mods, phase 1: the App's diagnostic document as the game reads it, and how its rules are matched
// against renderers (no engine: the renderers are written out as the Runtime would read them).
#include "texture_diag.hpp"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
using ms::Ptr;
using namespace ms::texdiag;
namespace {
const char* doc=R"J({"format":"gibox-texture-diag","version":1,"name":"Mod","ini":"Mod.ini","character":10000107,"characterName":"x","rules":[
 {"section":"TextureOverrideModBangsPosition","part":"ModBangsPosition","hint":"bangsposition","slotLetter":-1,"hash":"0e33fe05","first":-1,"priority":1,"kind":"control","textures":[]},
 {"section":"TextureOverrideModBodyA","part":"ModBodyA","hint":"body","slotLetter":0,"hash":"d5ae71a3","first":0,"kind":"segment","textures":[
  {"slot":"resource\\gimi\\diffuse","resource":"rbody1","role":"diffuse","condition":"$colour == 0","via":""},{"slot":"resource\\gimi\\diffuse","resource":"rbody2","role":"diffuse","condition":"$colour == 1","via":""},
  {"slot":"ps-t3","resource":"rramp","role":"ramp","condition":"","via":""},{"slot":"ps-t7","resource":"rodd","role":"unknown","condition":"","via":""}]},
 {"section":"TextureOverrideModBodyB","part":"ModBodyB","hint":"body","slotLetter":1,"hash":"d5ae71a3","first":25002,"kind":"segment","textures":[{"slot":"resource\\gimi\\diffuse","resource":"rbody1","role":"diffuse","condition":"","via":""}]},
 {"section":"TextureOverrideModBodyAll","part":"ModBodyAll","hint":"bodyall","hash":"d5ae71a3","first":-1,"kind":"whole-ib","textures":[{"slot":"ps-t0","resource":"rbody1","role":"diffuse","condition":"","via":""}]},
 {"section":"TextureOverrideModDressA","part":"ModDressA","hint":"dress","slotLetter":0,"hash":"ede196e7","first":0,"kind":"segment","textures":[{"slot":"resource\\gimi\\diffuse","resource":"rdressa","role":"diffuse","condition":"","via":""}]},
 {"section":"TextureOverrideModDressB","part":"ModDressB","hint":"dress","slotLetter":1,"hash":"ede196e7","first":10764,"kind":"segment","textures":[{"slot":"resource\\gimi\\diffuse","resource":"rdressa","role":"diffuse","condition":"","via":""}]},
 {"section":"TextureOverrideModDressC","part":"ModDressC","hint":"dress","slotLetter":2,"hash":"ede196e7","first":22887,"kind":"segment","textures":[{"slot":"resource\\gimi\\diffuse","resource":"rdressc","role":"diffuse","condition":"","via":""}]},
 {"section":"TextureOverrideModTailA","part":"ModTailA","hint":"tail","slotLetter":0,"hash":"335e2828","first":0,"kind":"segment","textures":[{"slot":"resource\\gimi\\diffuse","resource":"rdressa","role":"diffuse","condition":"","via":""}]},
 {"section":"TextureOverrideModHairA","part":"ModHairA","hint":"hair","slotLetter":0,"hash":"64d28bf2","first":0,"kind":"segment","textures":[{"slot":"resource\\gimi\\diffuse","resource":"rdressa","role":"diffuse","condition":"","via":""}]},
 {"section":"TextureOverrideModHairB","part":"ModHairB","hint":"hair","slotLetter":1,"hash":"64d28bf2","first":900,"kind":"segment","textures":[{"slot":"resource\\gimi\\diffuse","resource":"rdressa","role":"diffuse","condition":"","via":""}]},
 {"section":"TextureOverrideModFaceEyesADiffuse","part":"ModFaceEyesADiffuse","hint":"faceeyesadiffuse","hash":"60512e64","first":-1,"kind":"hash-unknown","textureName":true,"textures":[{"slot":"resource\\gimi\\diffuse","resource":"reyes","role":"diffuse","condition":"","via":""}]}],
 "resources":[{"name":"rbody1","file":"b1.dds","exists":true,"kind":"dds","width":2048,"height":2048,"format":"DXGI 98 (BC7_UNORM)","mips":12},{"name":"reyes","file":"e.png","exists":true,"kind":"png","width":1024,"height":1024,"format":"PNG","mips":1}],
 "variables":[{"name":"$colour","initial":0,"values":[0,1],"persist":true,"keys":["KeySwap"]}],"otherInis":[{"ini":"DISABLED/Other.ini","sharedHashes":["d5ae71a3"]}],"notes":[]})J";
Texture tex(const char* prop,const char* name,const char* w,const char* h){Texture t;t.property=prop;t.texture=0x1000;t.name=name;t.width=w;t.height=h;t.format="4 (RGBA32)";return t;}
Renderer renderer(const char* name,Ptr native,std::vector<std::pair<uint32_t,uint32_t>> subs,unsigned materials,bool readable=true){Renderer r;r.name=name;r.native=native;r.renderer=native+1;r.meshName=std::string(name)+"Mesh";
 r.submeshes=unsigned(subs.size());r.layout.submeshes=int(subs.size());r.layout.submeshCount=std::to_string(subs.size());r.layout.status=readable?"read":"未采集: engine Mesh getters: fake failure";
 if(readable)for(auto& s:subs){ms::texdiag::Submesh x;x.known=true;x.start=s.first;x.count=s.second;r.layout.subs.push_back(x);}
 for(unsigned i=0;i<materials;i++){Material m;m.material=native*16+i;m.name=std::string(name)+"Mat"+std::to_string(i);m.shader="miHoYo/Character";m.textures.push_back(tex("_MainTex","Tex","2048","2048"));m.textures.push_back(tex("_LightMapTex","Light","2048","2048"));r.materials.push_back(m);}
 return r;}
const Match& of(const std::vector<Match>& all,const Doc& d,const char* section){for(auto& m:all)if(d.rules[m.rule].section==section)return m;assert(false);return all[0];}
bool has2(const std::string& s,const char* x){return s.find(x)!=std::string::npos;}
bool has(const std::string& s,const char* x){if(s.find(x)!=std::string::npos)return true;std::cerr<<"missing \""<<x<<"\" in: "<<s<<"\n";return false;}
}
int main(){
 Doc d=parse(doc,"/x/Mod.json");assert(d.rules.size()==11&&d.character==10000107&&d.resources.at("rbody1").width==2048&&d.variables.size()==1&&d.variables[0]=="$colour=0 of [0,1] persist"&&d.others[0]=="DISABLED/Other.ini shares d5ae71a3");
 {bool bad=false;try{parse(R"({"format":"gibox-texture-diag","version":2,"rules":[]})","v2");}catch(const std::exception& e){bad=std::string(e.what()).find("version 2")!=std::string::npos;}assert(bad);}
 // Body: both of its hash's starts are submesh starts, as many materials as submeshes. Dress: 22887 lies inside its
 // second submesh (PC draws merged) with other textures. Hair: two submeshes but a third material. Face: layout unreadable.
 std::vector<Renderer> rs{renderer("Body",0x10,{{0,25002},{25002,3000}},2),renderer("Dress",0x20,{{0,10764},{10764,20000}},2),renderer("Hair",0x30,{{0,900},{900,300}},3),renderer("Face",0x40,{{0,600}},1,false)};
 rs[3].materials[0].textures[0]=tex("_MainTex","Avatar_Face_Tex","1024","1024");
 auto all=match(d,rs);assert(all.size()==d.rules.size());
 auto& bodyA=of(all,d,"TextureOverrideModBodyA");assert(bodyA.verdict==Verdict::Confirmed&&bodyA.renderer==0&&bodyA.slot==0);
 auto lineA=matchLine(d,bodyA,rs);assert(has(lineA,"确定匹配")&&has(lineA,"only Body#0x10 has all 2")&&has(lineA,"index count 25002 = distance to the next match_first_index")&&has(lineA,"-> _MainTex (material has it")&&has(lineA,"ps-t3 rramp [ramp] -> 待诊断 (no candidate property on this material)")&&has(lineA,"ps-t7 rodd [unknown] -> 待诊断 (slot role unknown)")&&has(lineA,"if $colour == 1"));
 assert(of(all,d,"TextureOverrideModBodyB").verdict==Verdict::Confirmed&&of(all,d,"TextureOverrideModBodyB").slot==1);
 auto& whole=of(all,d,"TextureOverrideModBodyAll");assert(whole.verdict==Verdict::Likely&&whole.renderer==0&&whole.slot==-1);
 auto& dressA=of(all,d,"TextureOverrideModDressA");assert(dressA.verdict==Verdict::Likely&&dressA.renderer==1&&has(matchLine(d,dressA,rs),"2 of the hash's 3"));
 auto& dressB=of(all,d,"TextureOverrideModDressB");assert(dressB.verdict==Verdict::Ambiguous&&has(matchLine(d,dressB,rs),"PC segment 22887 lies inside this submesh too")&&has(matchLine(d,dressB,rs),"different textures"));
 auto& dressC=of(all,d,"TextureOverrideModDressC");assert(dressC.verdict==Verdict::Ambiguous&&dressC.renderer==1&&dressC.submesh==1&&has(matchLine(d,dressC,rs),"most likely merged into its submesh 1")&&has(matchLine(d,dressC,rs),"lies inside an Android submesh"));
 // A single segment at index 0 is a submesh start everywhere: no name, no decision.
 auto& tail=of(all,d,"TextureOverrideModTailA");assert(tail.verdict==Verdict::Ambiguous&&tail.candidates.size()==3);
 auto& hair=of(all,d,"TextureOverrideModHairA");assert(hair.verdict==Verdict::Likely&&hair.renderer==2&&has(matchLine(d,hair,rs),"3 materials, 2 submeshes"));
 auto& eyes=of(all,d,"TextureOverrideModFaceEyesADiffuse");assert(eyes.verdict==Verdict::Ambiguous&&has(matchLine(d,eyes,rs),"Face#0x40 slot 0 _MainTex=Avatar_Face_Tex (same size 1024x1024)")&&has(matchLine(d,eyes,rs),"cannot be recomputed"));
 assert(of(all,d,"TextureOverrideModBangsPosition").verdict==Verdict::Skipped);
 // Without any readable layout: name + counts + order make a rule likely at most; never confirmed.
 std::vector<Renderer> blind{renderer("Body",0x10,{{0,25002},{25002,3000}},2,false),renderer("Dress",0x20,{{0,1},{1,1}},2,false)};
 auto b=match(d,blind);for(auto& m:b)assert(m.verdict!=Verdict::Confirmed);
 auto& bb=of(b,d,"TextureOverrideModBodyB");assert(bb.verdict==Verdict::Likely&&bb.slot==1&&has(matchLine(d,bb,blind),"by order only"));
 assert(of(b,d,"TextureOverrideModDressA").verdict==Verdict::Ambiguous);   // 3 segments, 2 submeshes
 // Two rules on one slot are not both confirmed.
 Doc twice=d;twice.rules[2].first=0;twice.rules[2].section="TextureOverrideModBodyA2";auto t=match(twice,rs);assert(of(t,twice,"TextureOverrideModBodyA").verdict!=Verdict::Confirmed&&has(matchLine(twice,of(t,twice,"TextureOverrideModBodyA"),rs),"rules on the same slot"));
 // The lines the Runtime writes.
 assert(has(rendererLine(rs[3],10000107),"layout=未采集: engine Mesh getters: fake failure")&&has(submeshLines(rs[0])[1],"submesh=1 indexStart=25002 indexCount=3000 material-slot-by-position=1 (BodyMat1)"));
 assert(has(materialLines(rs[0])[0],"slot=0 material=BodyMat0#0x100 shader=miHoYo/Character textures=[_MainTex=Tex 2048x2048 4 (RGBA32)"));
 assert(has(ruleLine(d,d.rules[1]),"kind=segment part=body letter=A textures=[resource\\gimi\\diffuse=rbody1[diffuse] if $colour == 0"));
 // The device log of 0.7.49 (anemolumine, Lumine 10000007): one Body renderer (Body_LOD1 at the time) with the materials
 // Mat_Hair / Mat_Body / Mat_Dress, Brow / Face / Face_Eye with Mat_Face; the INI splits hash dfb54407 into Head, ModBody, ModDress.
 Doc lumine=parse(R"J({"format":"gibox-texture-diag","version":1,"name":"anemolumine","character":10000007,"rules":[
  {"section":"TextureOverrideTravelerGirlHead","part":"TravelerGirlHead","hint":"head","hash":"dfb54407","first":0,"kind":"segment","textures":[{"slot":"ps-t0","resource":"rhead","role":"diffuse","condition":"","via":""}]},
  {"section":"TextureOverrideTravelerGirlModBody","part":"TravelerGirlModBody","hint":"modbody","hash":"dfb54407","first":6915,"kind":"segment","textures":[{"slot":"ps-t0","resource":"rbody","role":"diffuse","condition":"","via":""}]},
  {"section":"TextureOverrideTravelerGirlModDress","part":"TravelerGirlModDress","hint":"moddress","hash":"dfb54407","first":40413,"kind":"segment","textures":[{"slot":"ps-t0","resource":"rbody","role":"diffuse","condition":"","via":""}]}],
  "resources":[{"name":"rbody","file":"b.dds","exists":true,"kind":"dds","width":4096,"height":4096,"format":"DXGI 99 (BC7_UNORM_SRGB)","mips":1}]})J","/x/anemolumine.json");
 auto lumineWorld=[&](bool readable,bool lod,std::vector<std::pair<uint32_t,uint32_t>> subs){std::vector<Renderer> w{renderer("Body",0x10,subs,3,readable),renderer("Brow",0x20,{{0,60}},1,readable),renderer("Face",0x30,{{0,3000}},1,readable),renderer("Face_Eye",0x40,{{0,2000}},1,readable)};
  const char* parts[]={"Hair","Body","Dress"};for(int i=0;i<3;i++)w[0].materials[size_t(i)].name=std::string("Avatar_Girl_Sword_PlayerGirl_Mat_")+parts[i]+" (Instance) #"+std::to_string(i);
  for(size_t i=1;i<4;i++){w[i].materials[0].name="Avatar_Girl_Sword_PlayerGirl_Mat_Face (Instance) #"+std::to_string(i+2);}w[0].lod=lod;w[0].meshName=lod?"Body_LOD1":"Body";return w;};
 // As on the device (no layout): every segment on Body, the slot named by its material and by order; never confirmed.
 auto dev=lumineWorld(false,true,{{0,1},{1,1},{2,1}});auto lm=match(lumine,dev);
 for(int k=0;k<3;k++){assert(lm[size_t(k)].verdict==Verdict::Likely&&lm[size_t(k)].renderer==0&&lm[size_t(k)].slot==k);}
 assert(has(matchLine(lumine,lm[0],dev),"another segment of hash dfb54407 names Body#0x10")&&has(matchLine(lumine,lm[0],dev),"(GIMI Head = game Hair, a naming convention)")&&has(matchLine(lumine,lm[0],dev),"material name and segment order agree")&&has(matchLine(lumine,lm[0],dev),"lower LOD (Body_LOD1)"));
 assert(has(matchLine(lumine,lm[2],dev),"Mat_Dress (Instance) #2 (slot 2) is named for the part"));
 // The full mesh keeping the PC index order: confirmed by index starts, material names agreeing.
 auto full=lumineWorld(true,false,{{0,6915},{6915,33498},{40413,9000}});auto fm=match(lumine,full);
 for(int k=0;k<3;k++)assert(fm[size_t(k)].verdict==Verdict::Confirmed&&fm[size_t(k)].renderer==0&&fm[size_t(k)].slot==k);
 assert(has(matchLine(lumine,fm[0],full),"only Body#0x10 has all 3")&&has(matchLine(lumine,fm[0],full),"is named for the part (GIMI Head = game Hair)"));
 // The lower LOD as the 0.7.50 device log read it (Body_LOD1: 0 / 4218 / 20229): its own index layout, not compared
 // with the PC one: never merged segments or a different PC order; material names and order decide, likely at most.
 auto low=lumineWorld(true,true,{{0,4218},{4218,16011},{20229,1944}});auto wm=match(lumine,low);
 for(int k=0;k<3;k++){assert(wm[size_t(k)].verdict==Verdict::Likely&&wm[size_t(k)].renderer==0&&wm[size_t(k)].slot==k&&has(matchLine(lumine,wm[size_t(k)],low),"its index layout is its own, not comparable with the PC full mesh"));}
 assert(!has2(matchLine(lumine,wm[1],low),"merged")&&has(matchLine(lumine,wm[0],low),"material name and segment order agree"));
 assert(textureFormat(47)=="47 (ETC2_RGBA8)"&&textureFormat(99)=="99 (not in this build's TextureFormat)"&&materialPart("Avatar_Girl_Sword_PlayerGirl_Mat_Hair (Instance) #0")=="hair"&&materialPart("Avatar_Girl_Bow_Linnea_Mat_Hair(Clone) (Instance) #1")=="hair");
 // Conditions as TextureDiag writes them, and what a rule writes for given values.
 {std::map<std::string,int> v{{"$colour",1},{"$x",0}};std::string why;
  assert(condition("",v,why)&&condition("$colour == 1",v,why)&&!condition("$colour == 0",v,why)&&condition("!($colour == 0) && $colour == 1",v,why)&&condition("$x == 1 || $colour >= 1",v,why)&&why.empty());
  assert(!condition("$missing == 1",v,why)&&why=="unknown variable $missing");why.clear();assert(!condition("$colour === 1",v,why)&&!why.empty());why.clear();assert(!condition("time > 3",v,why)&&!why.empty());
  Rule r;r.textures={{"resource\\gimi\\diffuse","ra","diffuse","$colour == 0",""},{"resource\\gimi\\diffuse","rb","diffuse","!($colour == 0) && $colour == 1",""},{"resource\\gimi\\lightmap","rl","lightmap","",""},
   {"ps-t3","rr","ramp","",""},{"ps-t5","ru","unknown","$time > 1",""}};
  std::vector<std::string> skipped;auto w=writes(r,v,skipped);
  assert(w.size()==2&&w[0].property=="_MainTex"&&w[0].resource=="rb"&&!w[0].linear&&w[1].property=="_LightMapTex"&&w[1].resource=="rl"&&w[1].linear);
  assert(skipped.size()==2&&has(skipped[0],"ps-t3 rr [ramp]: no confirmed game property (待诊断)")&&has(skipped[1],"condition '$time > 1' not evaluable (unknown variable $time)"));
  Rule off=r;off.textures.push_back({"resource\\gimi\\diffuse","","diffuse","",""});skipped.clear();assert(writes(off,v,skipped).size()==1);}   // null takes _MainTex off
 // texture.json: variants, textures (safe file names only), the rules.
 {auto mod=parseMod(R"J({"format":"gibox-texture-mod","version":1,"name":"X","generation":"t1","character":{"id":10000007},"variants":{"vars":[{"name":"$c","values":[0,1]}],"default":{"$c":1}},
   "textures":[{"resource":"r","file":"textures/0.rgba","width":4,"height":2}],"diag":{"format":"gibox-texture-diag","version":1,"rules":[{"section":"S","kind":"segment","hash":"h","first":0,"textures":[]}]}})J","/m/X");
  assert(mod.character==10000007&&mod.defaults.at("$c")==1&&mod.textures.size()==1&&mod.textures[0].width==4&&mod.doc.rules.size()==1&&mod.doc.character==10000007&&mod.doc.path=="/m/X/texture.json");
  bool bad=false;try{parseMod(R"J({"format":"gibox-texture-mod","version":1,"name":"X","generation":"t","character":{"id":10000007},"variants":{"vars":[],"default":{}},"textures":[{"resource":"r","file":"../x.rgba","width":1,"height":1}],"diag":{"format":"gibox-texture-diag","version":1,"rules":[]}})J","/m");}catch(const std::exception&){bad=true;}assert(bad);}
 if(std::getenv("TEXDIAG_PRINT")){for(auto& m:lm)std::cout<<matchLine(lumine,m,dev)<<"\n";for(auto& m:wm)std::cout<<matchLine(lumine,m,low)<<"\n";}
 if(std::getenv("TEXDIAG_PRINT"))for(auto& m:all)std::cout<<matchLine(d,m,rs)<<"\n";

 // The PC hash database (0.7.82): Linnea's entries as GI-Model-Importer-Assets has them (2039d16), plus another
 // character with a Face and the same light map, which must not be taken for hers.
 {const char* pc=R"J({"format":"gibox-pc-hashes","version":1,"commit":"2039d16d4b64696098ba53cd69888ce967397be9","date":"2026-08-03","characters":[
   {"name":"Linnea","avatar":10000130,"components":[{"name":"Bangs","ib":"773fbe7b","indexes":[0],"counts":[14517],"textures":[[["LightMap","0f6493aa"],["NormalMap","0287a193"],["Diffuse","ab64b4cc"]]]},
    {"name":"Eyes","ib":"39564ab6","indexes":[0],"counts":[684],"textures":[[["LightMap","0f6493aa"],["Diffuse","ab64b4cc"]]]},
    {"name":"Body","ib":"e282f691","indexes":[0,46956,101592],"counts":[46956,54636,870],"textures":[[["LightMap","0f6493aa"],["NormalMap","0287a193"],["Diffuse","ab64b4cc"]],[["LightMap","573e8513"],["Diffuse","832fe591"]],[["LightMap","573e8513"],["Diffuse","832fe591"]]]},
    {"name":"Face","ib":"","counts":[5196],"textures":[[["Diffuse","1d1bd3cf"]]]}]},
   {"name":"Other","avatar":10000107,"components":[{"name":"Face","ib":"","textures":[[["Diffuse","1d1bd3cf"],["LightMap","99999999"]]]}]}]})J";
  auto db=ms::pcdb::parse(pc);assert(db.comps.size()==5&&db.byIb.at("e282f691").size()==1&&db.byTexture.at("0f6493aa").size()==3&&db.byTexture.at("1d1bd3cf").size()==2&&db.characters.at("Linnea")==10000130);
  {std::string how;auto c=ms::pcdb::charactersFor(db,10000130,{"zzzzzzzz"},&how);assert(c.size()==1&&c.count("Linnea")&&has(how,"by AvatarId 10000130"));
   c=ms::pcdb::charactersFor(db,0,{"E282F691"},&how);assert(c.size()==1&&c.count("Linnea")&&has(how,"index-buffer hashes"));assert(ms::pcdb::charactersFor(db,10000002,{},&how).empty());}
  // A texture-only Linnea mod: one Body segment, Bangs by its hash at 0, Eyes without match_first_index, her face
  // diffuse and the light map she shares across Bangs, Eyes and the first Body draw (its resource name says nothing).
  const char* lm=R"J({"format":"gibox-texture-diag","version":1,"name":"Lin","ini":"Lin.ini","character":10000130,"rules":[
   {"section":"TextureOverrideLinneaBodyA","hint":"body","hash":"e282f691","first":0,"kind":"segment","textures":[{"slot":"resource\\gimi\\diffuse","resource":"rbody","role":"diffuse"}]},
   {"section":"TextureOverrideLinneaBangs","hint":"bangs","hash":"773fbe7b","first":0,"kind":"segment","textures":[{"slot":"resource\\gimi\\diffuse","resource":"rbangs","role":"diffuse"}]},
   {"section":"TextureOverrideLinneaEyes","hint":"eyes","hash":"39564ab6","first":-1,"kind":"hash-unknown","textures":[{"slot":"ps-t0","resource":"reyes","role":"diffuse"}]},
   {"section":"TextureOverrideLinneaFaceHeadDiffuse","hint":"faceheaddiffuse","hash":"1d1bd3cf","first":-1,"kind":"hash-unknown","textureName":true,"textures":[{"slot":"this","resource":"rface","role":"diffuse"}]},
   {"section":"TextureOverrideLinneaShared","hint":"shared","hash":"0F6493AA","first":-1,"kind":"hash-unknown","textures":[{"slot":"this","resource":"rl","role":"unknown"}]},
   {"section":"TextureOverrideLinneaOld","hint":"body","hash":"e282f691","first":46000,"kind":"segment","textures":[{"slot":"resource\\gimi\\diffuse","resource":"rbody","role":"diffuse"}]},
   {"section":"TextureOverrideLinneaSomething","hint":"something","hash":"12345678","first":-1,"kind":"hash-unknown","textures":[{"slot":"this","resource":"rx","role":"diffuse"}]}],"resources":[]})J";
  Doc ld=parse(lm,"/x/Lin.json");
  std::vector<Renderer> rs{renderer("Body",0x10,{{0,46956},{46956,54636},{101592,870}},3),renderer("Bang",0x20,{{0,14517}},1),renderer("Eye",0x30,{{0,684}},1),renderer("Face",0x40,{{0,5196}},1),renderer("Brow",0x50,{{0,600}},1)};
  // Without the database: Bangs is only named, Eyes and the textures cannot be told.
  {auto m=match(ld,rs);assert(of(m,ld,"TextureOverrideLinneaBangs").verdict==Verdict::Likely&&of(m,ld,"TextureOverrideLinneaFaceHeadDiffuse").verdict!=Verdict::Confirmed&&has(matchLine(ld,of(m,ld,"TextureOverrideLinneaShared"),rs),"PC hash database is not installed"));}
  annotate(ld,db);assert(has(ld.pc,"Linnea (by the index-buffer hashes its rules name)")&&ld.rules[4].textures[0].role=="lightmap"&&has(ld.rules[4].pcNote,"role unknown by its name, lightmap by the PC slot")&&ld.rules[3].pcTextures.size()==1&&ld.rules[3].pcTextures[0].component=="Face");
  assert(has(ld.rules[5].pcNote,"match_first_index 46000 is not a PC draw start of Linnea Body [0,46956,101592]"));
  auto m=match(ld,rs);auto line=[&](const Match& x){return matchLine(ld,x,rs);};
  if(std::getenv("TEXDIAG_PRINT"))for(auto& x:m)std::cout<<line(x)<<"\n";
  // Body A: one segment in the mod, the other PC draw starts make it the only one; Bangs: the PC index count.
  auto& body=of(m,ld,"TextureOverrideLinneaBodyA");assert(body.verdict==Verdict::Confirmed&&body.renderer==0&&body.slot==0&&has(line(body),"only Body#0x10 has all 3 of the hash's PC draw starts as submesh starts")&&has(line(body),"= the PC draw's"));
  auto& bangs=of(m,ld,"TextureOverrideLinneaBangs");assert(bangs.verdict==Verdict::Confirmed&&bangs.renderer==1&&has(line(bangs),"only Bang#0x20 has 1 submeshes and 14517 indices in it, as the PC draw"));
  auto& eyes=of(m,ld,"TextureOverrideLinneaEyes");assert(eyes.verdict==Verdict::Confirmed&&eyes.renderer==2&&eyes.slot==0&&has(line(eyes),"no match_first_index; PC: 1 draw(s) of this index buffer, this one at 0"));
  auto& face=of(m,ld,"TextureOverrideLinneaFaceHeadDiffuse");assert(face.verdict==Verdict::Confirmed&&face.renderer==3&&face.slot==0&&has(line(face),"PC: texture hash 1d1bd3cf is Linnea Face draw 0 Diffuse")&&has(line(face),"no index buffer of its own"));
  // The light map: one match per PC draw it is in, written next to the Body segment's diffuse (other textures).
  std::vector<const Match*> shared;for(auto& x:m)if(ld.rules[x.rule].section=="TextureOverrideLinneaShared")shared.push_back(&x);
  assert(shared.size()==3);std::set<int> at;for(auto* x:shared){assert(x->verdict==Verdict::Confirmed&&x->slot==0);at.insert(x->renderer);}assert((at==std::set<int>{0,1,2}));
  assert(has(line(*shared[0]),"also on this slot, setting other textures")||has(line(*shared[1]),"also on this slot, setting other textures")||has(line(*shared[2]),"also on this slot, setting other textures"));
  assert(of(m,ld,"TextureOverrideLinneaOld").verdict!=Verdict::Confirmed&&has(line(of(m,ld,"TextureOverrideLinneaOld")),"is not a PC draw start"));
  assert(of(m,ld,"TextureOverrideLinneaSomething").verdict==Verdict::None&&has(line(of(m,ld,"TextureOverrideLinneaSomething")),"not in the PC hash database for this character"));
  // A lower LOD Face is no target; the other character's Face texture is not Linnea's light map.
  {auto low=rs;low[3].lod=true;auto x=match(ld,low);assert(of(x,ld,"TextureOverrideLinneaFaceHeadDiffuse").verdict!=Verdict::Confirmed);}
  // This device's upload: Bangs without a PC count, and a second one-submesh mesh with the same index count.
  {auto db2=db;for(auto& c:db2.comps)c.counts.clear();Doc d2=parse(lm,"/x/Lin.json");annotate(d2,db2);auto two=rs;two.push_back(renderer("Bag",0x60,{{0,14517}},1));
   assert(of(match(d2,two),d2,"TextureOverrideLinneaBangs").verdict!=Verdict::Confirmed);
   ms::pcdb::DeviceMap dm;ms::pcdb::Mesh e;e.ib="773fbe7b";e.mesh="BangMesh";e.vertices=0;e.subs={{0,14517}};dm.byIb[e.ib]=e;two[1].vertices=0;
   auto dmText=ms::pcdb::writeMap(dm);bool bad=false;try{ms::pcdb::parseMap(dmText);}catch(const std::exception&){bad=true;}assert(bad);   // a mesh without vertices is refused
   e.vertices=4839;dm.byIb[e.ib]=e;two[1].vertices=4839;auto back=ms::pcdb::parseMap(ms::pcdb::writeMap(dm));assert(back.byIb.at("773fbe7b").subs.size()==1&&back.byIb.at("773fbe7b").vertices==4839);
   auto x=match(d2,two,&back);auto& b2=of(x,d2,"TextureOverrideLinneaBangs");assert(b2.verdict==Verdict::Confirmed&&b2.renderer==1&&has(matchLine(d2,b2,two),"was reproduced on this device by the upload of mesh BangMesh"));}
  // The database the App installs parses, and ties Linnea to her AvatarId.
  {std::ifstream f("app/src/main/assets/pc-hashes.json");std::string text((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());auto real=ms::pcdb::parse(text);
   assert(real.comps.size()>300&&real.characters.at("Linnea")==10000130&&real.byIb.at("e282f691").size()==1&&real.commit=="2039d16d4b64696098ba53cd69888ce967397be9");}
  // The device hash map the App ships (the maintainer's capture, build 6d85e314): parses, and holds Linnea's Bangs as
  // the device read it (4000 vertices, one draw of 14517 indices from 0).
  {std::ifstream f("app/src/main/assets/device-hashes.json");std::string text((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());auto map=ms::pcdb::parseMap(text);
   const auto& bang=map.byIb.at("773fbe7b");assert(map.byIb.size()>1000&&bang.mesh=="Bang"&&bang.vertices==4000&&bang.subs.size()==1&&bang.subs[0].first==0&&bang.subs[0].second==14517&&bang.character=="Linnea");}}
 std::cout<<"Texture-only diagnostics: PASS (conditions and write plan, texture.json, document, index-start / group / name / count / order evidence, merged PC segments, unreadable layouts never confirmed, texture-hash candidates by size, shared slots)\n";
}
