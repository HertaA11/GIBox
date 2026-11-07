#include "core.hpp"
#include "json.hpp"
#include "model.hpp"
#include "model_fixture.hpp"
#include "runtime.hpp"
#include "logic_game.hpp"
#include "logic_import.hpp"
#include <cassert>
#include <elf.h>
#include <iostream>
#include <limits>
#include <unistd.h>
using namespace ms;
using fixture::Spec;using fixture::replaceIn;
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
// The model folder is refused, and the message says why.
template<class F>void refused(const std::string& dir,F spec,const char* why){Spec s;spec(s);fixture::writeModel(dir,s);std::string message;
 try{loadModelFile(dir);}catch(const std::exception& ex){message=ex.what();}
 if(message.find(why)==std::string::npos){std::cerr<<"expected \""<<why<<"\", got \""<<message<<"\"\n";assert(false);}}
static void floatAt(Bytes& b,size_t p,float f){std::memcpy(b.data()+p,&f,4);}
// A version 2 model: an always-drawn draw plus one drawn only for $halo=3 with $legs=1; the skeleton carries parents.
Spec v2(){Spec s;s.version=2;s.variants="{\"vars\":[{\"name\":\"$halo\",\"values\":[0,1,3]},{\"name\":\"$legs\",\"values\":[0,1]}],\"default\":{\"$halo\":0,\"$legs\":1}}";
 s.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0},{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":1,\"when\":{\"vars\":[\"$halo\",\"$legs\"],\"allow\":[[3,1]]}}]"};
 s.edit=[](std::string& j){replaceIn(j,"\"bones\":[\"BoneA\"]","\"bones\":[\"BoneA\"],\"parents\":[-1]");};return s;}
int main(int argc,char**argv){
 // The converter's Present rehearsal (called by the Java tests in place of NativeBridge.rehearse): request file in, reply out.
 if(argc==3&&std::string(argv[1])=="--rehearse"){std::cout<<rehearseJson(text(argv[2]))<<"\n";return 0;}
 if(argc==3&&std::string(argv[1])=="--draws"){std::cout<<drawsJson(text(argv[2]))<<"\n";return 0;}
 // Converter output check (called by the Java tests): the folder must load exactly as the runtime will load it.
 if(argc==2){std::string p=argv[1];auto m=loadModelFile(p);auto s=readSummary(p);assert(m->parts.size()==s.parts.size()&&!m->parts.empty()&&m->variants.combos.empty());
  for(auto& part:m->parts){assert(!part.draws.empty());for(auto& d:part.draws)assert(m->materials.count(d.row.name));std::vector<Submesh> subs;std::vector<Material> rows;activeDraws(part.draws,m->variants.defaults,subs,rows);assert(!subs.empty());}
  if(m->animation.present())for(size_t i=0;i<m->animation.frames.size();i++){auto frame=loadModelFile(p,false,false,int(i),m->generation);assert(frame->parts.size()==m->parts.size());}
  if(m->logic.present()&&!m->animation.present()){LogicDriver d(m->logic);
   for(size_t i=0;i<m->logic.assignments.size();i++){for(auto& kv:m->logic.assignments[i])assert(d.set(kv.first,kv.second));d.reselect();
    if(d.frame()<0||m->logic.choices[size_t(d.frame())]!=m->logic.choices[i]){std::cerr<<"logic outfit "<<i<<": driver picked "<<d.frame()<<"; "<<d.miss<<"\n";return 1;}}
   std::cout<<"Native logic: "<<d.summary()<<"; every outfit picked when its values are set\n";}
  if(m->logic.present()&&m->animation.present()){LogicDriver d(m->logic);assert(d.frame()>=0);FrameLoop loop(m->animation);loop.tick(0);d.tick(0,0);
   for(uint32_t t=1;t<=60;t++){loop.tick(t);d.tick(t,t/60.0);if(m->logic.choices[size_t(d.frame())]!=m->logic.choices[loop.current]){std::cerr<<"logic frame "<<d.frame()<<" vs FrameLoop "<<loop.current<<" at "<<t<<"\n";return 1;}}
   std::cout<<"Native logic: "<<d.summary()<<"; 60 frames as FrameLoop\n";}
  if(exists(p+"/learned.json"))learnedOf(JsonParser(text(p+"/learned.json")).parse());
  return 0;}
 char temp[]="/tmp/gibox-core-XXXXXX";assert(mkdtemp(temp));std::string root=temp;
 // Rows back to PC order for a texture on one of the game's meshes (the face swap): 3 rows of 2 pixels.
 {Texture t{2,3,Bytes(24)};for(size_t i=0;i<24;i++)t.pixels[i]=uint8_t(i/8);flipRows(t);for(size_t i=0;i<24;i++)assert(t.pixels[i]==uint8_t(2-i/8));flipRows(t);for(size_t i=0;i<24;i++)assert(t.pixels[i]==uint8_t(i/8));
  Texture odd{1,1,Bytes(4,7)};flipRows(odd);assert(odd.pixels==Bytes(4,7));}
 // ---- the GIBox model format: a good folder loads with everything in place
 {auto dir=root+"/good";Spec s;s.textures=true;s.withTarget(fixture::target("Body"));s.variants="{\"vars\":[{\"name\":\"$swap\",\"values\":[0,1]}],\"combos\":[{\"$swap\":0},{\"$swap\":1}],\"default\":1}";
  s.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":0}]","[{\"start\":0,\"count\":0,\"material\":\"Material0\",\"slot\":2}]"};fixture::writeModel(dir,s);
  auto m=loadModelFile(dir);auto& p=m->parts.at(0);
  assert(m->id==10000107&&m->generation=="1"&&p.spec.name=="Part0"&&p.spec.group=="Body"&&p.spec.bones==1&&p.geo.n==3&&p.geo.ni==3&&u32(p.geo.idx()+8)==2);
  assert(p.used==std::set<unsigned>{0}&&p.sourceBones==std::vector<std::string>{"BoneA"}&&p.sourceBinds.size()==64&&f32(p.sourceBinds.data())==1);
  // Version 1 combinations read as draws that know their combinations; an unlisted assignment is the default one.
  {std::vector<Submesh> subs;std::vector<Material> rows;assert(m->variants.combos.size()==2&&m->variants.defaults==(Assignment{{"$swap",1}})&&p.draws.size()==2&&pick(m->variants,{})==(Assignment{{"$swap",1}}));
   activeDraws(p.draws,pick(m->variants,{}),subs,rows);assert(subs.size()==1&&subs[0].count==0&&rows[0].slot==2);
   activeDraws(p.draws,pick(m->variants,Assignment{{"$swap",0}}),subs,rows);assert(subs.size()==1&&subs[0].count==3&&rows[0].slot==0&&pick(m->variants,Assignment{{"$swap",7}})==(Assignment{{"$swap",1}}));}
  auto& mat=m->materials.at("Material0");assert(!mat.original&&mat.main&&mat.main->w==1&&mat.light&&!mat.normal);
  auto sum=readSummary(dir);assert(sum.parts.size()==1&&sum.parts[0].hasTarget&&sum.parts[0].target.mesh=="Body"&&sum.parts[0].renderer=="Body");
  // diffuseOnly drops the extra maps; a model by name needs names in the right shape
  {auto j=text(dir+"/model.json");replaceIn(j,"\"blob\"","\"flags\":{\"diffuseOnly\":true},\"blob\"");write(dir+"/model.json",j);assert(!loadModelFile(dir)->materials.at("Material0").light);}
  {Spec n;n.character(0,{"citlali"}).noSkeleton();fixture::writeModel(root+"/named",n);auto mn=loadModelFile(root+"/named");assert(mn->id==0&&mn->names==std::vector<std::string>{"citlali"}&&mn->parts[0].sourceBones.empty());}}
 // Full animation: frame-local geometry, strict sequence, fractional Present period, one advance per engine frame.
 {auto dir=root+"/animated";fixture::writeModel(dir,fixture::animated());auto m=loadModelFile(dir,false);assert(m->animation.frames.size()==3&&m->animation.bytes>0);
  auto next=loadModelFile(dir,false,false,1,m->generation);assert(next->parts.size()==1&&!next->animation.present());
  FrameLoop clock(m->animation);assert(!clock.tick(10)&&clock.current==0);assert(clock.tick(11)&&clock.current==1);assert(!clock.tick(11)&&clock.current==1);assert(clock.tick(12)&&clock.current==2);assert(clock.tick(13)&&clock.current==0);
  assert(!clock.tick(1000)&&clock.gaps==1);rejects([&]{loadModelFile(dir,false,false,3,m->generation);});rejects([&]{loadModelFile(dir,false,false,1,"old-generation");});}
 // ---- the checks: every one names what is wrong
 auto bad=root+"/bad";
 refused(bad,[](Spec& s){s.edit=[](std::string& j){replaceIn(j,"gibox-model","other-model");};},"不是 GIBox 模型");
 refused(bad,[](Spec& s){s.edit=[](std::string& j){replaceIn(j,"\"version\":1","\"version\":5");};},"不支持的版本");
 refused(bad,[](Spec& s){s.character(9999);},"角色 ID 超出范围");
 refused(bad,[](Spec& s){s.character(0);},"须指定角色 ID 或角色名称");
 refused(bad,[](Spec& s){s.character(0,{"Citlali"});},"角色名称无效");
 refused(bad,[](Spec& s){s.edit=[](std::string& j){auto p=j.find("\"crc32\":")+8;auto e=j.find('}',p);auto crc=std::stoull(j.substr(p,e-p));j.replace(p,e-p,std::to_string(crc^1));};},"校验和不符");
 refused(bad,[](Spec& s){s.edit=[](std::string& j){replaceIn(j,"\"size\":304","\"size\":320");};},"大小与 model.json 记录不符");
 refused(bad,[](Spec& s){s.edit=[](std::string& j){replaceIn(j,"\"uv\":[96,24]","\"uv\":[100,24]");};},"未按 16 字节对齐");
 refused(bad,[](Spec& s){s.edit=[](std::string& j){replaceIn(j,"\"uv\":[96,24]","\"uv\":[96,16]");};},"长度不符");
 refused(bad,[](Spec& s){s.edit=[](std::string& j){replaceIn(j,"\"bindposes\":[240,64]","\"bindposes\":[288,64]");};},"超出 model.bin 范围");
 refused(bad,[](Spec& s){s.indices={0,1,3};},"顶点编号超出范围");
 refused(bad,[](Spec& s){s.indices={0,1,2,0};},"3 的倍数");
 refused(bad,[](Spec& s){s.positions[4]=std::numeric_limits<float>::quiet_NaN();},"含非有限数值");
 refused(bad,[](Spec& s){floatAt(s.weights,0,.5f);},"权重之和不为 1");
 refused(bad,[](Spec& s){floatAt(s.weights,0,-1);floatAt(s.weights,4,2);},"权重无效");
 refused(bad,[](Spec& s){put32(s.weights.data()+16,1);},"骨骼编号超出范围");
 refused(bad,[](Spec& s){s.draws={"[{\"start\":0,\"count\":6,\"material\":\"Material0\",\"slot\":0}]"};},"超出范围");
 refused(bad,[](Spec& s){s.draws={"[{\"start\":0,\"count\":3,\"material\":\"Nope\",\"slot\":0}]"};},"未知材质 Nope");
 refused(bad,[](Spec& s){s.draws={"[{\"start\":0,\"count\":3,\"material\":\"Material0\",\"slot\":64}]"};},"超出范围：\"slot\"");
 refused(bad,[](Spec& s){s.draws.push_back(s.draws[0]);},"列表无效：\"draws\"");
 refused(bad,[](Spec& s){s.variants="{\"vars\":[{\"name\":\"$a\",\"values\":[0,1]}],\"combos\":[{}],\"default\":0}";},"须为每个变量赋值");
 refused(bad,[](Spec& s){s.variants="{\"vars\":[],\"combos\":[{\"$b\":1}],\"default\":0}";},"款式变量未声明：$b");
 refused(bad,[](Spec& s){s.variants="{\"vars\":[],\"combos\":[{}],\"default\":3}";},"超出范围：\"default\"");
 // ---- version 2: each draw says when it is drawn; no combination list, no limit of 32
 {Spec s=v2();fixture::writeModel(root+"/v2",s);auto m=loadModelFile(root+"/v2");auto& p=m->parts.at(0);std::vector<Submesh> subs;std::vector<Material> rows;
  assert(m->variants.combos.empty()&&m->variants.defaults==(Assignment{{"$halo",0},{"$legs",1}})&&p.draws.size()==2&&p.draws[0].when.vars.empty()&&p.sourceParents==std::vector<int>{-1});
  activeDraws(p.draws,pick(m->variants,{}),subs,rows);assert(subs.size()==1&&subs[0].count==3);
  activeDraws(p.draws,pick(m->variants,Assignment{{"$halo",3}}),subs,rows);assert(subs.size()==2&&rows[1].slot==1);
  activeDraws(p.draws,pick(m->variants,Assignment{{"$halo",3},{"$legs",0}}),subs,rows);assert(subs.size()==1);
  assert(pick(m->variants,Assignment{{"$halo",5},{"$nope",1}})==m->variants.defaults);}
 // Version 3 adds a model-level replacement switch; old readers must reject it rather than hide the original mesh.
 {Spec s=v2();s.version=3;s.edit=[](std::string& j){replaceIn(j,"\"blob\"","\"replaceWhen\":{\"vars\":[\"$halo\"],\"allow\":[[3]]},\"blob\"");};fixture::writeModel(root+"/v3",s);
  auto summary=readSummary(root+"/v3");assert(!drawn(summary.replaceWhen,pick(summary.variants,{}))&&drawn(summary.replaceWhen,pick(summary.variants,Assignment{{"$halo",3}}))&&!summary.parts[0].indexedBones);loadModelFile(root+"/v3");
  s.noSkeleton();fixture::writeModel(root+"/v3-indexed",s);assert(readSummary(root+"/v3-indexed").parts[0].indexedBones);}
 refused(bad,[](Spec& s){s=v2();s.version=3;s.edit=[](std::string& j){replaceIn(j,"\"blob\"","\"replaceWhen\":{\"vars\":[\"$unknown\"],\"allow\":[[0]]},\"blob\"");};},"引用了未声明的变量");
 refused(bad,[](Spec& s){s=v2();s.version=3;s.edit=[](std::string& j){replaceIn(j,"\"blob\"","\"replaceWhen\":{\"vars\":[\"$halo\"],\"allow\":[[9]]},\"blob\"");};},"when 中的取值未在变量中列出");
 refused(bad,[](Spec& s){s=v2();s.edit=[](std::string& j){replaceIn(j,"\"blob\"","\"replaceWhen\":{\"vars\":[\"$halo\"],\"allow\":[[3]]},\"blob\"");};},"需要模型格式版本 3");
 refused(bad,[](Spec& s){s=v2();replaceIn(s.draws[0],"[\"$halo\",\"$legs\"]","[\"$halo\",\"$wings\"]");},"引用了未声明的变量");
 refused(bad,[](Spec& s){s=v2();replaceIn(s.draws[0],"[[3,1]]","[[4,1]]");},"when 中的取值未在变量中列出：$halo");
 refused(bad,[](Spec& s){s=v2();replaceIn(s.draws[0],"[[3,1]]","[[3]]");},"when 的取值个数与变量数不符");
 refused(bad,[](Spec& s){s=v2();replaceIn(s.variants,",\"$legs\":1}","}");},"默认款式须为每个变量赋值");
 refused(bad,[](Spec& s){s=v2();s.draws[0]="[]";},"列表无效：\"draws\"");
 refused(bad,[](Spec& s){s=v2();s.edit=[](std::string& j){replaceIn(j,"\"bones\":[\"BoneA\"]","\"bones\":[\"BoneA\"],\"parents\":[0]");};},"父骨骼编号超出范围");
 refused(bad,[](Spec& s){s.edit=[](std::string& j){replaceIn(j,"\"name\":\"Part0\"","\"name\":\"../x\"");};},"部件名称无效");
 refused(bad,[](Spec& s){s.edit=[](std::string& j){replaceIn(j,"\"bones\":[\"BoneA\"]","\"bones\":[\"BoneA\",\"BoneB\"]");};},"列表无效：\"bones\"");
 refused(bad,[](Spec& s){s.withTarget("{\"renderer\":\"Body\",\"vertices\":0,\"submeshes\":1,\"bones\":1}");},"超出范围：\"vertices\"");
 {Spec s;fixture::writeModel(bad,s);write(bad+"/model.bin","short");rejects([&]{loadModelFile(bad);});std::filesystem::remove(bad+"/model.bin");rejects([&]{loadModelFile(bad);});}
 // ---- learned.json and the skeleton library round-trip through the native writers
 {Learned l;l.id=10000107;Fingerprint fp{"Body","Avatar_Body",23521,3,142};l.targets["Part0"]=fp;auto back=learnedOf(JsonParser(learnedJson(l)).parse());
  assert(back.id==10000107&&back.targets.at("Part0").vertices==23521&&back.targets.at("Part0").mesh=="Avatar_Body"&&matches(back.targets.at("Part0"),fp));
  rejects([]{learnedOf(JsonParser("{\"format\":\"gibox-learned\",\"targets\":{\"../x\":{}}}").parse());});rejects([]{learnedOf(JsonParser("{\"format\":\"x\"}").parse());});
  write(root+"/learned.json","{broken");assert(readLearned(root).targets.empty());}
 {SkeletonRenderer r;r.name="Body";r.target={"Body","Avatar_Body",3,1,2};r.bones={"Root","Spine\"1"};r.parents={-1,0};r.bindposes=Bytes(128);for(int b=0;b<2;b++)for(int i=0;i<4;i++)floatAt(r.bindposes,b*64+i*20,1);floatAt(r.bindposes,0,1.f/3);
  auto j=JsonParser(skeletonJson(10000107,"Scene/Avatar_Girl_Catalyst_Citlali",{r})).parse();auto& x=j.at("renderers").array.at(0);
  assert(j.at("format").string=="gibox-skeleton"&&j.at("id").number==10000107&&j.at("root").string.find("Citlali")!=std::string::npos&&x.at("bones").array[1].string=="Spine\"1"&&x.at("parents").array[0].number==-1);
  assert(float(x.at("bindposes").array[0].number)==1.f/3&&x.at("bindposes").array.size()==32&&fingerprintOf(x.at("target")).bones==2);
  auto good=r;r.parents={1,0};rejects([&]{skeletonJson(10000107,"",{r});});
  r=good;floatAt(r.bindposes,0,std::numeric_limits<float>::quiet_NaN());rejects([&]{skeletonJson(10000107,"",{r});});
  r=good;r.bindposes=Bytes(128);rejects([&]{skeletonJson(10000107,"",{r});});
  r=good;r.target.bones=3;rejects([&]{skeletonJson(10000107,"",{r});});
  r=good;r.parents.pop_back();rejects([&]{skeletonJson(10000107,"",{r});});}
 assert(crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926u);
 // ---- shared building blocks
 assert(config(" 10000107\n")==10000107);rejects([]{config("10000107 evil");});
 Bytes bind(64);for(int i=0;i<4;i++)floatAt(bind,i*20,1);auto mapping=mapBones({"Bone_A"},{"bonea"},bind,bind,{0}).target;assert(mapping[0]==0);rejects([&]{mapBones({"Bone_A"},{"bonea","Bone-A"},bind,Bytes(128),{0});});auto changed=bind;floatAt(changed,0,2);rejects([&]{mapBones({"Bone_A"},{"bonea"},bind,changed,{0});});
 // Bone matching in tiers: an exact name wins over a looser one; same names are told apart by their ancestors;
 // a helper bone the target lacks hands its weight to its nearest matching ancestor; a body bone never does.
 {auto binds=[&](size_t n){Bytes b;for(size_t i=0;i<n;i++)b.insert(b.end(),bind.begin(),bind.end());return b;};auto why=[&](auto f){std::string m;try{f();}catch(const std::exception& e){m=e.what();}return m;};
  assert(mapBones({"Bone A"},{"bone_a","Bone A"},bind,binds(2),{0}).target[0]==1);
  std::vector<std::string> src{"Root","L","Tip","R","Tip"},dst{"Root","R","Tip","L","Tip"};std::vector<int> sp{-1,0,1,0,3},dp{-1,0,1,0,3};
  auto m=mapBones(src,dst,binds(5),binds(5),{2,4},sp,dp);assert(m.target[2]==4&&m.target[4]==2&&m.moved.empty());
  assert(why([&]{mapBones(src,dst,binds(5),binds(5),{2});}).find("骨骼名称有歧义：Tip")==0);
  std::vector<std::string> hs{"Bip001 Head","+HairBack01","+HairBack02"};std::vector<int> hp{-1,0,1};
  auto h=mapBones(hs,{"Bip001 Head"},binds(3),bind,{0,2},hp,{-1});assert(h.target[2]==0&&h.moved.size()==1&&h.moved[0].first==2);
  assert(why([&]{mapBones({"Bip001 Head","Bip001 L Hand"},{"Bip001 Head"},binds(2),bind,{1},{-1,0},{-1});}).find("找不到骨骼：Bip001 L Hand")==0);
  assert(why([&]{mapBones(hs,{"Bip001 Head"},binds(3),bind,{2});}).find("找不到骨骼：+HairBack02")==0);}
 // Weights after mapping: two source bones that became one target bone are added together; largest first; sum 1.
 {Skin s{Bytes(32),{0,1,2}};floatAt(s.data,0,.2f);floatAt(s.data,4,.5f);floatAt(s.data,8,.3f);put32(s.data.data()+16,0);put32(s.data.data()+20,1);put32(s.data.data()+24,2);
  remap(s,{3,5,3});assert(f32(s.data.data())==.5f&&u32(s.data.data()+16)==3&&f32(s.data.data()+4)==.5f&&u32(s.data.data()+20)==5&&f32(s.data.data()+8)==0&&f32(s.data.data()+12)==0);}
 {Skin s{Bytes(32),{0}};floatAt(s.data,0,1);remap(s,mapping);assert(u32(s.data.data()+16)==0);put32(s.data.data()+16,5);rejects([&]{remap(s,mapping);});}
 auto j=JsonParser("{\"enabled\":true,\"selected\":[\"\\u6a21\\u578b\",\"\\ud83d\\ude00\"],\"cycle\":2}").parse();assert(j.at("selected").array[0].string=="模型");assert(j.at("cycle").number==2);rejects([]{JsonParser("{\"a\":1,\"a\":2}").parse();});rejects([]{JsonParser("[01]").parse();});rejects([]{JsonParser("\"\\ud800\"").parse();});
 Bytes note(32);put32(note.data(),4);put32(note.data()+4,16);put32(note.data()+8,3);put32(note.data()+12,0x00554e47);for(int i=0;i<16;i++)note[16+i]=i;Elf64_Phdr ph{};ph.p_type=PT_NOTE;ph.p_memsz=note.size();auto id=loadedBuildId(reinterpret_cast<Ptr>(note.data()),&ph,1,sizeof(ph));assert(id=="000102030405060708090a0b0c0d0e0f"&&id!=abi::buildId);ph.p_memsz=16;rejects([&]{loadedBuildId(reinterpret_cast<Ptr>(note.data()),&ph,1,sizeof(ph));});
 Bytes dds(136);put32(dds.data(),0x20534444);put32(dds.data()+4,124);put32(dds.data()+12,3);put32(dds.data()+16,3);put32(dds.data()+76,32);put32(dds.data()+80,4);put32(dds.data()+84,0x31545844);dds[128]=0;dds[129]=0xf8;auto tex=decodeDds(dds);assert(tex.w==3&&tex.h==3&&tex.pixels.size()==36&&tex.pixels[0]==255&&tex.pixels[3]==255);dds.pop_back();rejects([&]{decodeDds(dds);});
 // The App's DDS decode returns at most 1024 a side, halved as ModelConverter.texture / TextureMod halve in Java: 2x2
 // box average, an odd last row or column dropped, the halving repeated while either side is over.
 {Texture big{5,3,Bytes(5*3*4)};for(size_t i=0;i<big.pixels.size();i++)big.pixels[i]=uint8_t(i*7);auto s1=shrinkTexture(big,2);
  assert(s1.w==2&&s1.h==1&&s1.pixels.size()==8);for(unsigned i=0;i<2;i++)for(unsigned c=0;c<4;c++){unsigned sum=0;for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)sum+=big.pixels[((y*5)+i*2+x)*4+c];assert(s1.pixels[i*4+c]==uint8_t(sum/4));}
  auto same=shrinkTexture(big,8);assert(same.w==5&&same.h==3&&same.pixels==big.pixels);
  Texture tall{2,2048,Bytes(2*2048*4,9)};auto s2=shrinkTexture(tall,1024);assert(s2.w==1&&s2.h==1024&&s2.pixels==Bytes(1024*4,9));}
 Fingerprint t{"Body","Avatar_Body_LOD1",23521,3,142},live{"Body","avatar_body (Clone)",23521,3,142};assert(matches(t,live)&&fingerprintOf(JsonParser(fingerprintJson(t)).parse()).mesh==t.mesh);
 assert(lowerLod("Body_LOD3")&&lowerLod("body_lod1 (Clone)")&&lowerLod("Body_LOD12 Instance")&&!lowerLod("Body")&&!lowerLod("Body_LOD0")&&!lowerLod("Body_LOD")&&!lowerLod("Body_LODx")&&!lowerLod("Lod1_Body"));
 live.mesh="avatar_body Instance";assert(matches(t,live));live.vertices++;assert(!matches(t,live));live.vertices--;live.mesh="Avatar_Face";assert(!matches(t,live));
 // Texture positions without pixels; readTextures fetches only the named materials, and refuses another conversion.
 {Spec lz;lz.textures=true;lz.extraMaterials=2;fixture::writeModel(root+"/lazy",lz);auto m=loadModelFile(root+"/lazy",false);
  assert(m->materials.size()==3&&!m->materials.at("Material0").main&&m->materials.at("Material0").mainRef.present()&&m->materials.at("Material0").lightRef.present()&&!m->materials.at("Material1").lightRef.present());
  readTextures(root+"/lazy",m->generation,m->blobSize,m->materials,{"Material2"});
  assert(!m->materials.at("Material0").loaded()&&!m->materials.at("Material1").loaded()&&m->materials.at("Material2").main->pixels==Bytes(4,2));
  readTextures(root+"/lazy",m->generation,m->blobSize,m->materials,{"Material0"});assert(m->materials.at("Material0").main->pixels==Bytes(4,255)&&m->materials.at("Material0").light);
  auto full=loadModelFile(root+"/lazy");assert(full->materials.at("Material1").main->pixels==Bytes(4,1));
  auto again=loadModelFile(root+"/lazy",false);fixture::writeModel(root+"/lazy",lz.gen("2"));bool refused=false;
  try{readTextures(root+"/lazy",again->generation,again->blobSize,again->materials,{"Material1"});}catch(const std::exception& ex){refused=std::string(ex.what()).find("model.json 已变化")!=std::string::npos;}assert(refused);
  Assignment a;std::vector<PartFile> none;assert(materialsDrawn(m->parts,a)==std::set<std::string>{"Material0"}&&materialsDrawn(none,a).empty());}
 // Submeshes from the draws in effect (mergeDraws): neighbours with the same row share one, their ranges joined in draw
 // order; a draw the assignment hides between them is not drawn; ranges continuing each other become one range.
 {Material a{"A",0},a1{"A",1},b{"B",0};Bytes idx=fixture::words32({0,1,2,3,4,5,6,7,8,9,10,11});
  When mid;mid.vars={"$mid"};mid.allow={{1}};std::vector<DrawSpec> ds={{{0,3},a,{}},{{3,3},a,mid},{{6,6},a,{}}};std::vector<Submesh> subs;std::vector<Material> rows;
  activeDraws(ds,Assignment{{"$mid",0}},subs,rows);auto m=mergeDraws(subs,rows,true);
  assert(m.size()==1&&m[0].row.name=="A"&&m[0].ranges.size()==2&&m[0].count==9&&joinedIndices(idx,m[0])==fixture::words32({0,1,2,6,7,8,9,10,11}));
  activeDraws(ds,Assignment{{"$mid",1}},subs,rows);m=mergeDraws(subs,rows,true);
  assert(m.size()==1&&m[0].ranges.size()==1&&m[0].ranges[0].start==0&&m[0].ranges[0].count==12&&joinedIndices(idx,m[0])==idx);
  m=mergeDraws(subs,rows,false);assert(m.size()==3&&m[1].ranges[0].start==3&&m[1].count==3);           // not merged: every draw
  assert(mergeDraws({{0,3},{3,3},{6,3}},{a,b,a},true).size()==3);                                      // A B A: no draw moves
  assert(mergeDraws({{0,3},{3,3}},{a,a1},true).size()==2);                                             // another slot
  m=mergeDraws({{6,3},{0,3}},{a,a},true);assert(m.size()==1&&joinedIndices(idx,m[0])==fixture::words32({6,7,8,0,1,2}));   // draw order, not buffer order
  m=mergeDraws({{0,0},{3,3}},{a,a},true);assert(m.size()==1&&m[0].ranges.size()==1&&m[0].ranges[0].start==3&&m[0].count==3);
  activeDraws({{{0,3},a,mid}},Assignment{{"$mid",0}},subs,rows);m=mergeDraws(subs,rows,true);assert(m.size()==1&&m[0].count==0&&joinedIndices(idx,m[0]).empty());   // nothing drawn
  rejects([&]{joinedIndices(idx,MergedDraw{a,{{9,6}},6});});rejects([&]{mergeDraws({{0,3}},{},true);});}
 // Texture swaps ("textures"): a material of its own onto a game renderer; always among the materials an assignment needs.
 {auto swaps=[](const std::string& list){return [list](Spec& x){x.textures=true;x.extraMaterials=1;x.edit=[list](std::string& j){replaceIn(j,",\"parts\":[",",\"textures\":"+list+",\"parts\":[");};};};
  Spec ok;swaps("[{\"renderer\":\"Face\",\"material\":\"Material1\"}]")(ok);fixture::writeModel(root+"/swap",ok);auto m=loadModelFile(root+"/swap",false);Assignment a;
  assert(m->swaps.size()==1&&m->swaps[0].renderer=="Face"&&m->swaps[0].material=="Material1"&&!materialsDrawn(m->parts,a).count("Material1")&&materialsNeeded(*m,a).count("Material1")&&materialsNeeded(*m,a).count("Material0"));
  refused(root+"/swap-unknown",swaps("[{\"renderer\":\"Face\",\"material\":\"Material9\"}]"),"贴图替换的材质无效");
  refused(root+"/swap-twice",swaps("[{\"renderer\":\"Face\",\"material\":\"Material1\"},{\"renderer\":\"Face\",\"material\":\"Material0\"}]"),"贴图替换的渲染器重复");
  refused(root+"/swap-name",swaps("[{\"renderer\":\"Face/..\",\"material\":\"Material1\"}]"),"贴图替换的渲染器");
  // model.json "logic": animation models only, files inside logic/, reads of known draws, one record per frame.
  auto logicRefused=[&](const std::string& name,Spec spec,const char* why){std::filesystem::create_directories(root+"/"+name+"/logic");write(root+"/"+name+"/logic/0.ini",std::string(fixture::logicIni));
   std::string message;fixture::writeModel(root+"/"+name,spec);try{loadModelFile(root+"/"+name);}catch(const std::exception& ex){message=ex.what();}
   if(message.find(why)==std::string::npos){std::cerr<<"expected \""<<why<<"\", got \""<<message<<"\"\n";assert(false);}};
  auto ibs=std::vector<std::string>{"file:a.ib","file:b.ib","file:c.ib"};
  logicRefused("logic-static",fixture::withLogic(Spec(),fixture::logicJson(ibs)),"logic：仅用于动画或款式模型");
  logicRefused("logic-path",fixture::withLogic(fixture::animated(),fixture::logicJson(ibs,"logic/../x.ini")),"logic：文件名无效");
  logicRefused("logic-missing",fixture::withLogic(fixture::animated(),fixture::logicJson(ibs,"logic/7.ini")),"无法读取");
  logicRefused("logic-pass",fixture::withLogic(fixture::animated(),fixture::logicJson(ibs,"logic/0.ini","Skin")),"logic：字段无效：Part ib");
  logicRefused("logic-frames",fixture::withLogic(fixture::animated(),fixture::logicJson({"file:a.ib","file:b.ib"})),"列表无效：\"frames\"");
  // present-sequence (the rehearsal's order): frames by index, the sequence played one step per Present, then from loop.
  {auto sequenced=[](const std::string& tail){Spec s=fixture::animated();auto inner=s.edit;s.edit=[inner,tail](std::string& t){inner(t);
     auto m=JsonParser(t).parse();auto& an=m.object["animation"].object;for(auto k:{"variable","first","last","step","initial","counter","period","counterStep"})an.erase(k);
     an["kind"].string="present-sequence";for(auto& kv:JsonParser("{"+tail+"}").parse().object)an[kv.first]=kv.second;t=fixture::jsonText(m);};return s;};
   fixture::writeModel(root+"/sequence",sequenced("\"sequence\":[0,1,2,1],\"loop\":1"));auto m=loadModelFile(root+"/sequence");assert(m->animation.sequenced&&m->animation.sequence.size()==4&&m->animation.loop==1);
   FrameLoop loop(m->animation);loop.tick(0);std::vector<size_t> shown{loop.current};for(uint32_t t=1;t<=7;t++){loop.tick(t);shown.push_back(loop.current);}
   assert((shown==std::vector<size_t>{0,1,2,1,1,2,1,1}));loop.showFrom(2);assert(loop.current==2&&loop.step==2);
   refused(root+"/sequence-range",[&](Spec& s){s=sequenced("\"sequence\":[0,3],\"loop\":0");},"超出范围：动画序列");
   refused(root+"/sequence-loop",[&](Spec& s){s=sequenced("\"sequence\":[0,1],\"loop\":2");},"超出范围：\"loop\"");}
  logicRefused("logic-key",fixture::withLogic(fixture::animated(),[&]{auto j=fixture::logicJson(ibs);j.insert(j.rfind('}'),",\"keys\":[{\"file\":0,\"section\":\"KeyA\",\"type\":\"press\"}]");return j;}()),"logic：按键类型无效：press");
  // A drawindexed of 0 indices before the part's real draw (BarbaraCelestialRose), or draws after ib = null
  // (NahidaPoS): they draw nothing, the converter does not record them, and the driver's selection must not either.
  {LogicSpec z;z.mainNs="Mods\\Z\\a.ini";z.files.push_back({z.mainNs,"logic/0.ini","[Constants]\nglobal $look = 0\n[TextureOverridePart]\nhash = aaaa0001\nhandling = skip\nib = ResourceL0\nif $look == 2\n ib = null\nendif\ndrawindexed = 0, 0, 0\ndrawindexed = 3, 0, 0\nif $look == 1\n ib = ResourceL1\n drawindexed = 0, 0, 0\nendif\n[ResourceL0]\nfilename = l0.ib\n[ResourceL1]\nfilename = l1.ib\n"});
   z.resources["resource\\mods\\z\\a.ini\\l0"]="file:l0.ib";z.resources["resource\\mods\\z\\a.ini\\l1"]="file:l1.ib";
   z.choices.push_back({{"Part",fixture::logicDraw("file:l0.ib","count=3;start=0;base=0;")}});z.assignments.push_back({{"$look",0}});
   z.choices.push_back({{"Part",fixture::logicDraw("file:l0.ib","count=3;start=0;base=0;")}});z.assignments.push_back({{"$look",1}});   // the second 0 draw adds nothing
   z.choices.push_back({{"Part","hidden"}});z.assignments.push_back({{"$look",2}});   // ib = null: the draws issued after it draw nothing (NahidaPoS outfit 1)
   LogicSpec::Pass pass;pass.name="Part";pass.bindings["ib"]=0xaaaa0001u;pass.checks={"ib"};z.passes.push_back(pass);
   for(auto& f:logicFields())z.parts["Part"].push_back({f,"Part",f=="ib"||f.rfind("ps-",0)==0?f:std::string("vb0")});
   LogicDriver d(z);assert(d.frame()==0&&d.signature("Part")==fixture::logicDraw("file:l0.ib","count=3;start=0;base=0;"));
   assert(d.set("$look",1));d.reselect();assert(d.frame()==0&&d.stats.misses==0);
   assert(d.set("$look",2));d.reselect();assert(d.frame()==2&&d.signature("Part")=="hidden"&&d.stats.misses==0);}
  // Keys through the driver: a namespaced [Key], its conditions (one per cycle preset, as upstream), its run list's pre (on) and post (off), two presses
  // before one Present both dispatched in order; persist values by full name, restored into a new driver.
  {LogicSpec k;k.mainNs="Mods\\K\\a.ini";k.files.push_back({k.mainNs,"logic/0.ini","[Constants]\nglobal persist $look = 0\nglobal $armed = 1\n[KeyLook]\nkey = VK_UP\ntype = cycle\ncondition = $armed == 1, $armed == 1, $armed == 1\n$look = 0,1,2\n"
    "[KeyStep]\nkey = VK_DOWN\ntype = toggle\nrun = CommandListStep\n[CommandListStep]\npre $look = 2\npost $look = 0\n"
    "[TextureOverridePart]\nhash = aaaa0001\nhandling = skip\nif $look == 0\n ib = ResourceL0\nelif $look == 1\n ib = ResourceL1\nelse\n ib = ResourceL2\nendif\ndrawindexed = auto\n[ResourceL0]\nfilename = l0.ib\n[ResourceL1]\nfilename = l1.ib\n[ResourceL2]\nfilename = l2.ib\n"});
   for(int i=0;i<3;i++){k.resources["resource\\mods\\k\\a.ini\\l"+std::to_string(i)]="file:l"+std::to_string(i)+".ib";k.choices.push_back({{"Part",fixture::logicDraw("file:l"+std::to_string(i)+".ib")}});k.assignments.push_back({{"$look",i}});}
   LogicSpec::Pass pass;pass.name="Part";pass.bindings["ib"]=0xaaaa0001u;pass.checks={"ib"};k.passes.push_back(pass);
   for(auto& f:logicFields())k.parts["Part"].push_back({f,"Part",f=="ib"||f.rfind("ps-",0)==0?f:std::string("vb0")});
   k.keys={{0,"KeyLook","cycle"},{0,"KeyStep","toggle"}};LogicSpec::Key wrong{0,"KeyLook","toggle"},none{0,"KeyNone","cycle"},other{1,"KeyLook","cycle"};
   LogicDriver d(k);d.tick(0,0);assert(d.frame()==0&&!d.press(wrong)&&!d.press(none)&&!d.press(other));
   assert(d.press(k.keys[0])&&d.press(k.keys[0])&&d.frame()==0);d.tick(1,1/60.0);assert(d.frame()==2&&d.stats.keys==2);   // 0 -> 1 -> 2 in one Present
   assert(d.press(k.keys[1]));d.tick(2,2/60.0);assert(d.frame()==2);assert(d.press(k.keys[1]));d.tick(3,3/60.0);assert(d.frame()==0);   // pre, then post
   assert(d.set("$armed",0));assert(d.press(k.keys[0]));d.tick(4,4/60.0);assert(d.frame()==0);   // condition not met
   assert(d.set("$armed",1));assert(d.press(k.keys[0]));d.tick(5,5/60.0);assert(d.frame()==1&&d.persistChanged()&&!d.persistChanged());
   auto saved=d.persistent();assert(saved.size()==1&&saved.at("$\\mods\\k\\a.ini\\look")==1);
   saved["$\\mods\\k\\a.ini\\armed"]=0;   // not persist: ignored, as upstream ignores it in d3dx_user.ini
   LogicDriver back(k,saved);assert(back.frame()==1&&back.restoredCount()==1&&back.restoredVar("$look")&&!back.restoredVar("$armed")&&!back.persistChanged());
   assert(back.press(k.keys[0]));back.tick(0,0);assert(back.frame()==1);back.tick(1,1/60.0);assert(back.frame()==2);   // no Present on the first tick; smart cycle goes on from the restored 1
   // The App's panel: a cycle's preset chosen directly (its condition still applies), the state read back, other types refused.
   using In=LogicDriver::KeyInput;
   assert(back.input(k.keys[0],In::Select,0));back.tick(2,2/60.0);assert(back.frame()==0&&back.keyStates()[0].current==0&&back.keyStates()[0].name=="KeyLook");
   assert(!back.input(k.keys[0],In::Select,3)&&!back.input(k.keys[1],In::Select,0)&&!back.input(k.keys[0],In::Back)&&!back.input(k.keys[0],In::HoldDown));
   assert(back.set("$armed",0));assert(back.input(k.keys[0],In::Select,2));back.tick(3,3/60.0);assert(back.frame()==0);   // condition not met: as stepping
   assert(back.set("$armed",1));assert(back.input(k.keys[0],In::Select,2));back.tick(4,4/60.0);assert(back.frame()==2&&back.keyStates()[0].current==2&&!back.keyStates()[1].active);}
  // A hold key and an activate key (a MOD's own "1 2 3" keys): held and released, pressed; their states.
  {LogicSpec k;k.mainNs="Mods\\H\\a.ini";k.files.push_back({k.mainNs,"logic/0.ini","[Constants]\nglobal $look = 0\n[KeyHold]\nkey = 1\ntype = hold\n$look = 1\n[KeyTwo]\nkey = 2\n$look = 2\n"
    "[TextureOverridePart]\nhash = aaaa0001\nhandling = skip\nif $look == 0\n ib = ResourceL0\nelif $look == 1\n ib = ResourceL1\nelse\n ib = ResourceL2\nendif\ndrawindexed = auto\n[ResourceL0]\nfilename = l0.ib\n[ResourceL1]\nfilename = l1.ib\n[ResourceL2]\nfilename = l2.ib\n"});
   for(int i=0;i<3;i++){k.resources["resource\\mods\\h\\a.ini\\l"+std::to_string(i)]="file:l"+std::to_string(i)+".ib";k.choices.push_back({{"Part",fixture::logicDraw("file:l"+std::to_string(i)+".ib")}});k.assignments.push_back({{"$look",i}});}
   LogicSpec::Pass pass;pass.name="Part";pass.bindings["ib"]=0xaaaa0001u;pass.checks={"ib"};k.passes.push_back(pass);
   for(auto& f:logicFields())k.parts["Part"].push_back({f,"Part",f=="ib"||f.rfind("ps-",0)==0?f:std::string("vb0")});
   k.keys={{0,"KeyHold","hold"},{0,"KeyTwo","activate"}};using In=LogicDriver::KeyInput;
   LogicDriver d(k);d.tick(0,0);assert(!d.press(k.keys[0])&&!d.input(k.keys[1],In::HoldDown));
   assert(d.input(k.keys[0],In::HoldDown));d.tick(1,1/60.0);assert(d.frame()==1&&d.keyStates()[0].active&&d.keyStates()[0].matches);
   assert(d.input(k.keys[0],In::HoldUp));d.tick(2,2/60.0);assert(d.frame()==0&&!d.keyStates()[0].active);
   assert(!d.keyStates()[1].matches&&d.press(k.keys[1]));d.tick(3,3/60.0);assert(d.frame()==2&&d.keyStates()[1].matches);}
  {fixture::writeLogicModel(root+"/logic-ok",fixture::withLogic(fixture::animated(),fixture::logicJson(ibs)));auto m=loadModelFile(root+"/logic-ok");
   assert(m->logic.present()&&m->logic.files[0].text==fixture::logicIni&&m->logic.passes[0].bindings.at("ib")==0xaaaa0001u&&m->logic.choices.size()==3);
   LogicDriver d(m->logic);assert(d.frame()==0&&d.signature("Part")==fixture::logicSignature("file:a.ib"));
   FrameLoop loop(m->animation);loop.tick(0);d.tick(0,0);for(uint32_t t=1;t<=12;t++){loop.tick(t);d.tick(t,t/60.0);assert(d.frame()==int(loop.current));}}
  // Signature 2: what a part's draws select, matched to prebuilt frames by resource identity, range, order and state.
  {const std::string cycle="[Constants]\nglobal $frame = 0\nglobal $counter = 0\nglobal $n = 6\n[Present]\nif $counter % 0.5 == 0\n if $frame < 2\n  $frame = $frame + 1\n else\n  $frame = 0\n endif\nendif\npost $counter = $counter + 1\n";
   // inis: (namespace, text); resources / passes / fields: JSON; frames: per frame, part -> signature.
   auto model=[&](const std::string& name,const std::vector<std::pair<std::string,std::string>>& inis,const std::string& resources,const std::string& passes,const std::string& fields,const std::vector<std::map<std::string,std::string>>& frames){
    std::string dir=root+"/"+name,files,fr;std::filesystem::create_directories(dir+"/logic");
    for(size_t i=0;i<inis.size();i++){write(dir+"/logic/"+std::to_string(i)+".ini",inis[i].second);files+=(i?",":"")+std::string("{\"namespace\":")+quote(inis[i].first)+",\"file\":\"logic/"+std::to_string(i)+".ini\"}";}
    for(size_t i=0;i<frames.size();i++){std::string d;for(auto& kv:frames[i])d+=(d.empty()?"":",")+quote(kv.first)+":"+quote(kv.second);fr+=(i?",":"")+std::string("{\"value\":")+std::to_string(i)+",\"draws\":{"+d+"}}";}
    fixture::writeModel(dir,fixture::withLogic(fixture::animated(),"{\"signature\":2,\"main\":"+quote(inis[0].first)+",\"files\":["+files+"],\"resources\":"+resources+",\"passes\":"+passes+",\"fields\":"+fields+",\"frames\":["+fr+"]}"));
    return loadModelFile(dir);};
   auto own=[](const std::string& part){std::string f;for(std::string x:{"ib","position","blend","texcoord","ps-t0","ps-t1","ps-t2"})f+=(f.empty()?"":",")+("\""+x+"\":[\""+part+"\",\""+(x=="ib"||x.rfind("ps-",0)==0?x:std::string("vb0"))+"\"]");return "{"+f+"}";};
   auto pass=[](const std::string& part,const std::string& hash){return "{\"name\":\""+part+"\",\"bindings\":{\"ib\":\""+hash+"\"},\"checks\":[\"ib\"]}";};
   auto follows=[&](LogicDriver& d,const ms::AnimationSpec& a){FrameLoop loop(a);loop.tick(0);d.tick(0,0);if(d.frame()!=0)return false;for(uint32_t t=1;t<=9;t++){loop.tick(t);d.tick(t,t/60.0);if(d.frame()!=int(loop.current))return false;}return true;};
   // The same [ResourceA] name in two INIs: each binding is its own file's, by namespace.
   auto two=model("logic-namespaces",{{"Mods\\Two\\a.ini",cycle+"[TextureOverridePart]\nhash = aaaa0001\nhandling = skip\nib = ResourceA\ndrawindexed = auto\n[ResourceA]\nfilename = a.ib\n"},
     {"Mods\\Two\\b.ini","[TextureOverrideOther]\nhash = bbbb0001\nhandling = skip\nib = ResourceA\ndrawindexed = auto\n[ResourceA]\nfilename = other.ib\n"}},
    "{\"resource\\\\mods\\\\two\\\\a.ini\\\\a\":\"file:a.ib\",\"resource\\\\mods\\\\two\\\\b.ini\\\\a\":\"file:other.ib\"}","["+pass("Part","aaaa0001")+","+pass("Other","bbbb0001")+"]",
    "{\"Part\":"+own("Part")+",\"Other\":"+own("Other")+"}",std::vector<std::map<std::string,std::string>>(3,{{"Part",fixture::logicDraw("file:a.ib")},{"Other",fixture::logicDraw("file:other.ib")}}));
   {LogicDriver d(two->logic);assert(d.frame()==0&&d.signature("Other")==fixture::logicDraw("file:other.ib")&&d.unknown.empty());}
   // One part, two drawindexed: their order and ranges count; a draw count read straight from a variable.
   std::string ranges=cycle+"[TextureOverridePart]\nhash = aaaa0001\nhandling = skip\nib = ResourceA\nif $frame == 0\n drawindexed = 3,0,0\n drawindexed = 3,3,0\nelif $frame == 1\n drawindexed = 3,3,0\n drawindexed = 3,0,0\nelse\n drawindexed = $n,0,0\nendif\n[ResourceA]\nfilename = a.ib\n";
   auto r0=fixture::logicDraw("file:a.ib","count=3;start=0;base=0;"),r3=fixture::logicDraw("file:a.ib","count=3;start=3;base=0;"),r6=fixture::logicDraw("file:a.ib","count=6;start=0;base=0;");
   auto table="{\"resource\\\\mods\\\\logic\\\\a.ini\\\\a\":\"file:a.ib\"}";
   auto ordered=model("logic-ranges",{{"Mods\\Logic\\a.ini",ranges}},table,"["+pass("Part","aaaa0001")+"]","{\"Part\":"+own("Part")+"}",{{{"Part",r0+r3}},{{"Part",r3+r0}},{{"Part",r6}}});
   {LogicDriver d(ordered->logic);assert(follows(d,ordered->animation)&&d.stats.misses==0);}
   // Recorded as one order only: the other order is a different selection, not the same draws.
   auto unordered=model("logic-order",{{"Mods\\Logic\\a.ini",ranges}},table,"["+pass("Part","aaaa0001")+"]","{\"Part\":"+own("Part")+"}",{{{"Part",r0+r3}},{{"Part",r0+r3}},{{"Part",r6}}});
   {LogicDriver d(unordered->logic);d.tick(0,0);d.tick(1,1/60.0);assert(d.frame()==0&&d.stats.misses==1&&d.miss.find("当前选中 "+r3+r0)!=std::string::npos);}
   // Hidden (skipped, nothing drawn), the game's own draw, and a replacement are three selections.
   std::string states=cycle+"[TextureOverridePart]\nhash = aaaa0001\nif $frame == 0\n handling = skip\nelif $frame == 2\n handling = skip\n ib = ResourceA\n drawindexed = auto\nendif\n[ResourceA]\nfilename = a.ib\n";
   auto shown=model("logic-states",{{"Mods\\Logic\\a.ini",states}},table,"["+pass("Part","aaaa0001")+"]","{\"Part\":"+own("Part")+"}",{{{"Part","hidden"}},{{"Part","original"}},{{"Part",fixture::logicDraw("file:a.ib")}}});
   {LogicDriver d(shown->logic);assert(follows(d,shown->animation)&&d.stats.misses==0);}
   // An animation whose logic also has an outfit variable: frames were converted with $dress = 0 only, so $dress = 1
   // selects what no frame has; the frame shown stays and the miss says what differs.
   std::string dressed=cycle+"[Constants]\nglobal $dress = 0\n[TextureOverridePart]\nhash = aaaa0001\nhandling = skip\nif $dress == 1\n ib = ResourceB\nelse\n ib = ResourceA\nendif\ndrawindexed = auto\n[ResourceA]\nfilename = a.ib\n[ResourceB]\nfilename = b.ib\n";
   auto both=model("logic-dress",{{"Mods\\Logic\\a.ini",dressed}},"{\"resource\\\\mods\\\\logic\\\\a.ini\\\\a\":\"file:a.ib\",\"resource\\\\mods\\\\logic\\\\a.ini\\\\b\":\"file:b.ib\"}","["+pass("Part","aaaa0001")+"]","{\"Part\":"+own("Part")+"}",
    std::vector<std::map<std::string,std::string>>(3,{{"Part",fixture::logicDraw("file:a.ib")}}));
   {LogicDriver d(both->logic);assert(d.frame()==0&&d.set("$dress",1));d.reselect();assert(d.frame()==0&&d.stats.misses==1&&d.miss.find("当前选中 "+fixture::logicDraw("file:b.ib"))!=std::string::npos);
    // One engine frame is one Present however many renderers ask: the same frame number again does nothing.
    d.tick(10,0.1);d.tick(11,0.2);d.tick(11,0.2);d.tick(11,0.25);assert(d.stats.presents==1);}
   // Logic data of 0.7.34 loads (the model is fine) and is refused by the driver, with the reason.
   fixture::writeModel(root+"/logic-old",fixture::withLogic(fixture::animated(),"{\"files\":[{\"namespace\":\"Mods\\\\Logic\\\\a.ini\",\"file\":\"logic/0.ini\"}],\"passes\":[],\"reads\":{},\"frames\":[]}"));
   auto old=loadModelFile(root+"/logic-old");std::string why;try{LogicDriver d(old->logic);}catch(const std::exception& ex){why=ex.what();}
   assert(old->logic.present()&&why.find("0.7.34 版的 logic 数据")!=std::string::npos);}
  refused(root+"/swap-original",[](Spec& x){x.edit=[](std::string& j){replaceIn(j,",\"parts\":[",",\"textures\":[{\"renderer\":\"Face\",\"material\":\"Material0\"}],\"parts\":[");};},"贴图替换的材质无效");}
 std::filesystem::remove_all(root);
 // The global client's build picks its own table: Unity methods by MethodIndex, the game's fields, the native finds;
 // nothing pending. Another build ID picks nothing.
 {assert(!abi::select("0000")&&abi::at(abi::getMesh)==0x8f4217c&&abi::managerSlot==0x9070);
  assert(abi::select("32f920af1fd831959a440d294fa996a09657afb8")&&std::string(abi::buildId).substr(0,8)=="32f920af");
  assert(abi::at(abi::getMesh)==0x903ef50&&abi::at(abi::manager)==0x157d53a0&&abi::at(abi::nativeAlloc)==0x517af94&&abi::at(abi::receive)==0x9798c6c);
  // The allocator instance, not the storage pointer most calls read: CN's 0x15d92120 and the manager both 0x34200 lower.
  assert(abi::at(abi::nativeAllocator)==0x15d5df20&&0x15d92120-abi::at(abi::nativeAllocator)==0x158095a0-abi::at(abi::manager));
  assert(abi::managerSlot==0x90e0&&abi::entityVisual==0xc8&&abi::visualRenderers==0x68&&abi::managerLists.size()==12&&abi::entityIds.front()==0x368);
  assert(*abi::pendingRequired==0&&abi::at(abi::typeObject)==0x43b1bbc&&abi::at(abi::lodBind)-abi::at(abi::meshBones)==0xb3530&&std::string(abi::handoffChecks[2].hex)=="fe4fbfa968ff0590e50304aae403032a");}
 std::cout<<"Native core: PASS (global client build table selected by Build ID; GIBox model format: manifest, model.bin views, CRC-32, value/range/reference checks, variants, on-demand textures, learned.json, skeleton.json; bones, JSON, ELF notes, DDS, targets)\n";
}
