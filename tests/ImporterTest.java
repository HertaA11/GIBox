package com.gibox.tool;
import java.io.*;import java.nio.*;import java.nio.file.*;import java.nio.charset.StandardCharsets;import java.util.*;
/** Manual import: the 7.1 character table, search, the recommended character, staging folders and zips, conversion with
 *  and without the bone library, character changes, the Mods list, delete, and the XXMI log line. Host-only. */
public class ImporterTest {
 static void ok(boolean b,String s){if(!b)throw new AssertionError(s);}
 static String read(File f)throws Exception{return new String(Files.readAllBytes(f.toPath()),StandardCharsets.UTF_8);}
 static List<Integer> ids(List<Characters.Avatar> l){List<Integer> o=new ArrayList<>();for(Characters.Avatar x:l)o.add(x.id);return o;}
 static void character(File boneRoot,String dir,int id,String avatar)throws Exception{
  Fixtures.skeleton(new File(boneRoot,dir),id,"Scene/"+avatar+"/Bip001",new String[]{"body","face"},new String[]{"Root","Spine"},Collections.singletonMap("body",Fixtures.target("Body",23521,3,2)));}
 /** The Citlali fixture renamed: files, sections and resources carry `who` instead. */
 static File renamed(File base,String folder,String who)throws Exception{File src=GimiPatternsTest.mod(new File(base,".fixture-"+folder),"x",GimiPatternsTest.ini("global $creditinfo = 0\n","","",""));File d=new File(base,folder);d.mkdirs();
  for(File f:src.listFiles())if(f.getName().endsWith(".ini"))Files.write(new File(d,who+".ini").toPath(),read(f).replace("Citlali",who).getBytes(StandardCharsets.UTF_8));else Files.copy(f.toPath(),new File(d,f.getName().replace("Citlali",who)).toPath());
  ModelConverter.remove(src.getParentFile());return d;}
 static void zip(File zip,File dir,String prefix,boolean evil)throws Exception{try(java.util.zip.ZipOutputStream z=new java.util.zip.ZipOutputStream(new FileOutputStream(zip))){for(File f:dir.listFiles()){if(f.isDirectory())continue;z.putNextEntry(new java.util.zip.ZipEntry(prefix+f.getName()));z.write(Files.readAllBytes(f.toPath()));z.closeEntry();}if(evil){z.putNextEntry(new java.util.zip.ZipEntry("../evil.txt"));z.write(1);z.closeEntry();}}}
 public static void main(String[] a)throws Exception{
  File base=new File(a[0]),list=new File(a[1]),bones=new File(base,"骨骼"),mods=new File(base,"mods"),models=new File(base,"模型"),download=new File(base,"Download");models.mkdirs();download.mkdirs();
  // 1. The table is the list: same 130 IDs and names, same order; model names fit model.json "character.names".
  List<String> want=new ArrayList<>(),have=new ArrayList<>();for(String l:read(list).split("\\r?\\n"))if(l.matches("[0-9]{8}\t.+"))want.add(l);for(Characters.Avatar x:Characters.all())have.add(x.id+"\t"+x.name);
  ok(want.size()==130&&want.equals(have),"character table differs from tests/avatar_ids.txt");
  for(Characters.Avatar x:Characters.all()){for(String n:x.aliases)ok(n.matches("[a-z0-9]{3,40}"),"alias "+n);ok(Characters.byId(x.id)==x,"byId "+x.id);}
  // 2. Search by ID, Chinese name, English or internal name.
  ok(ids(Characters.search("10000107")).equals(Collections.singletonList(10000107))&&ids(Characters.search("茜特")).equals(Collections.singletonList(10000107)),"search id/name");
  ok(ids(Characters.search("旅行者")).equals(Arrays.asList(10000005,10000007))&&ids(Characters.search("Raiden")).equals(Collections.singletonList(10000052))&&ids(Characters.search("shougun")).equals(Collections.singletonList(10000052)),"search aliases");
  ok(Characters.search("1000010").size()==10&&Characters.search(" ").size()==131&&Characters.search("没有这个人").isEmpty(),"search edge cases");
  ok(Characters.words("TextureOverrideHuTaoBody2").equals(Arrays.asList("texture","override","hu","tao","body","2")),"words: "+Characters.words("TextureOverrideHuTaoBody2"));
  // 3. The recommended character: from the import name, folder, INI sections and resource names.
  character(bones,"10000107",10000107,"Avatar_Girl_Catalyst_Citlali");character(bones,"10000150",10000150,"Avatar_Girl_Sword_Odette(Clone)");character(bones,"10000107-123",10000107,"Old_Export");
  ok(ModLibrary.library(bones).size()==2&&ModLibrary.recorded(bones,10000107).bones.getName().equals("10000107"),"library prefers the per-character folder");
  File citlali=GimiPatternsTest.mod(download,"Citlali Summer",GimiPatternsTest.ini("global $creditinfo = 0\n","","",""));
  Characters.Guess g=Characters.guess(citlali,"Citlali Summer",ModLibrary.library(bones));
  ok(ids(g.recommended).equals(Collections.singletonList(10000107))&&Characters.ordered(g,"").get(0).id==10000107&&Characters.ordered(g,"").size()==131,"citlali recommended first: "+g.evidence);
  ok(Characters.ordered(g,"琴").get(0).id==10000003,"a search keeps only matches");
  File raiden=renamed(download,"RaidenShogun Kimono","RaidenShogun"),hutao=renamed(download,"hu tao","HuTao"),odette=renamed(download,"odette_v2","Odette"),hero=renamed(download,"Mystery","Hero");
  ok(ids(Characters.guess(raiden,"RaidenShogun Kimono",null).recommended).equals(Collections.singletonList(10000052)),"raiden");
  ok(ids(Characters.guess(hutao,"hu tao",null).recommended).equals(Collections.singletonList(10000046)),"hu tao joined");
  ok(ids(Characters.guess(odette,"odette_v2",ModLibrary.library(bones)).recommended).equals(Collections.singletonList(10000150)),"a character known only to the bone library");
  ok(Characters.guess(hero,"Mystery",ModLibrary.library(bones)).recommended.isEmpty()&&Characters.ordered(new Characters.Guess(),"").get(0).id==10000002,"no guess: plain list");
  // NPC / monster (1.0.3): last in the plain list, found by its words, never a character's alias or recommendation.
  {List<Characters.Avatar> plain=Characters.ordered(new Characters.Guess(),"");ok(plain.get(plain.size()-1)==Characters.SCENE_AVATAR&&Characters.byId(Characters.SCENE)==Characters.SCENE_AVATAR
    &&ids(Characters.search("怪物")).equals(Collections.singletonList(Characters.SCENE))&&ids(Characters.search("npc")).equals(Collections.singletonList(Characters.SCENE))
    &&!Characters.all().contains(Characters.SCENE_AVATAR)&&"NPC / 怪物".equals(Characters.nameOf(Characters.SCENE)),"NPC / monster choice");}
  File lumine=renamed(download,"Traveler","TravelerGirl"),both=renamed(download,"traveler x","Traveler");
  ok(ids(Characters.guess(lumine,"Traveler",null).recommended).equals(Collections.singletonList(10000007))&&ids(Characters.guess(both,"traveler x",null).recommended).equals(Arrays.asList(10000005,10000007)),"traveler: girl wins, plain is a tie");
  // 4. Staging: a folder is copied (one extra level is fine), a zip unpacked; unsafe archives leave nothing behind.
  File nested=new File(download,"Pack");nested.mkdirs();Files.move(citlali.toPath(),new File(nested,"Citlali Summer").toPath());
  StringBuilder log=new StringBuilder();File staged=Importer.stage(nested,mods,Importer.suggestName(nested),log);
  ok(staged.equals(new File(mods,"Pack"))&&ModLibrary.modRoot(staged).getName().equals("Citlali Summer")&&new File(nested,"Citlali Summer/Citlali.ini").isFile(),"folder copied, source kept");
  ok(Importer.stage(staged,mods,"ignored",log).equals(staged),"a folder already in Mods is used in place");
  File evil=new File(download,"evil.zip");zip(evil,hutao,"HuTao/",true);
  try{Importer.stage(evil,mods,"evil",log);ok(false,"zip slip accepted");}catch(IllegalArgumentException e){ok(e.getMessage().contains("压缩包路径越界")&&!new File(mods,"evil.txt").exists()&&!new File(mods,".pending/evil.txt").exists()&&!new File(mods,"evil").exists()&&!new File(mods,".pending/evil").exists(),"zip slip: "+e.getMessage());}
  File good=new File(download,"hutao_v1.zip");zip(good,hutao,"HuTao Mod/",false);File hz=Importer.stage(good,mods,Importer.suggestName(good),log);
  ok(hz.getName().equals("hutao_v1")&&ModLibrary.modRoot(hz).getName().equals("HuTao Mod"),"zip unpacked");
  File notMod=new File(download,"empty");notMod.mkdirs();try{Importer.stage(notMod,mods,"empty",log);ok(false,"folder without INI accepted");}catch(IllegalArgumentException e){ok(!new File(mods,"empty").exists(),"nothing staged");}
  // System file picker flow: prepare (Mods/.pending, invisible to the list) → character confirmed → commit; or discard.
  final File src=renamed(download,"Picked Klee","Klee");Importer.Source picked=new Importer.Source(){public String name(){return "Klee Summer.zip";}public void copyTo(File d,StringBuilder l)throws Exception{for(File f:src.listFiles())Files.copy(f.toPath(),new File(d,f.getName()).toPath());}};
  File pend=Importer.prepare(picked,mods,log);ok(pend.getParentFile().getName().equals(".pending")&&pend.getName().equals("Klee Summer")&&Importer.list(mods,models).stream().noneMatch(m->m.name.startsWith("Klee")),"pending is not listed: "+pend);
  ok(ids(Importer.guess(pend,picked.name(),bones).recommended).equals(Collections.singletonList(10000029)),"guess from the pending copy");
  Importer.discard(pend);ok(!pend.exists(),"discarded");
  pend=Importer.prepare(picked,mods,log);File klee=Importer.commit(pend,mods,Importer.suggestName(picked.name()));ok(klee.equals(new File(mods,"Klee Summer"))&&!pend.exists()&&ModLibrary.modRoot(klee).equals(klee),"committed");
  Importer.Source broken=new Importer.Source(){public String name(){return "broken";}public void copyTo(File d,StringBuilder l)throws Exception{GimiPatternsTest.put(d,"readme.txt","no ini");}};
  try{Importer.prepare(broken,mods,log);ok(false,"mod without INI prepared");}catch(IllegalArgumentException e){ok(!new File(mods,".pending/broken").exists(),"failed prepare leaves nothing");}
  Importer.prepare(picked,mods,log);Importer.discardAll(mods);ok(!new File(mods,".pending").exists(),"leftovers dropped at start");
  ModelConverter.remove(klee);
  ok(Importer.freeName(mods,models,"Pack").equals("Pack (2)")&&Importer.freeName(mods,models,"New").equals("New"),"free names");
  // 5. Bone library has the character: bones by name, targets known.
  ModelConverter.TextureDecoder dec=dds->{ByteBuffer o=ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN);o.putInt(1).putInt(1).putInt(-1);return o.array();};
  Importer.Result r=Importer.convert(staged,10000107,models,bones,dec,new StringBuilder());File pack=new File(models,"Pack");
  ModelManifest pm=ModelManifest.read(pack);Map<String,Object> pp=Fixtures.part(pack,0);
  ok(r.library&&r.model.equals(pack.getCanonicalFile())&&pm.id==10000107&&pm.names.isEmpty()&&pp.get("target")!=null&&pp.get("skeleton")!=null&&pm.sourcePath.equals(staged.getAbsolutePath())&&pm.sourceSignature.length()==40,"library conversion");
  ok(ModLibrary.overrides(staged).get("id").equals("10000107")&&r.summary.startsWith("茜特菈莉（10000107）"),"choice remembered: "+r.summary);
  r=Importer.convert(staged,10000107,models,new File(base,"missing-library"),dec,new StringBuilder());
  ok(!r.library&&r.summary.contains("复用本 MOD 已有骨骼记录")&&Fixtures.part(pack,0).get("skeleton")!=null&&Fixtures.part(pack,0).get("target")!=null,"a missing library must not erase the previous conversion's complete skeleton");
  // 6. Not in the bone library: the game's bone order, the ID, and the model name for the party / detail pages.
  r=Importer.convert(hz,10000046,models,bones,dec,new StringBuilder());File hm=new File(models,"hutao_v1");
  ModelManifest hmm=ModelManifest.read(hm);
  ok(!r.library&&hmm.id==10000046&&hmm.names.equals(Collections.singletonList("hutao"))&&Fixtures.part(hm,0).get("skeleton")==null&&Fixtures.part(hm,0).get("target")==null,"ID conversion without library");
  // What the game learned survives a re-conversion for the same character, not one for another character.
  String learned="{\"format\":\"gibox-learned\",\"version\":1,\"id\":0,\"targets\":{\"Part0\":{\"renderer\":\"Body\",\"mesh\":\"Body\",\"vertices\":3,\"submeshes\":1,\"bones\":1}}}";
  Files.write(new File(hm,"learned.json").toPath(),learned.getBytes(StandardCharsets.UTF_8));
  Importer.convert(hz,10000046,models,bones,dec,new StringBuilder());ok(read(new File(hm,"learned.json")).equals(learned),"learned.json kept for the same character");
  r=Importer.convert(hz,10000119,models,bones,dec,new StringBuilder());hmm=ModelManifest.read(hm);
  ok(!new File(hm,"learned.json").exists()&&hmm.names.isEmpty()&&hmm.id==10000119&&r.summary.contains("大世界出场一次"),"another character: learned.json dropped; no model name known");
  try{Importer.convert(hz,123,models,bones,dec,new StringBuilder());ok(false,"bad id");}catch(IllegalArgumentException e){ok(true,"");}
  // 7. Re-importing the same name keeps the chosen character.
  ModLibrary.setOverride(hz,"map","Part9=Body");File again=Importer.stage(good,mods,"hutao_v1",log);ok(ModLibrary.overrides(again).get("id").equals("10000119")&&ModLibrary.overrides(again).get("map").equals("Part9=Body"),"modelswap.txt kept: "+ModLibrary.overrides(again));
  // 8. The Mods list: imported mods, an old model without source, an unconverted mod, a changed choice.
  File legacy=new File(models,"Legacy");ModelConverter.convert(ModLibrary.modRoot(staged),null,legacy,0,new ModelConverter.Options().names(Collections.singletonList("citlali")),dec,new StringBuilder());
  File damaged=new File(models,"Damaged");damaged.mkdirs();Files.write(new File(damaged,"model.json").toPath(),"{".getBytes());new File(models,"NotAModel").mkdirs();
  Importer.stage(raiden,mods,"RaidenShogun Kimono",log);ModLibrary.setOverride(again,"id","10000046");
  Map<String,Importer.Mod> byName=new HashMap<>();for(Importer.Mod m:Importer.list(mods,models))byName.put(m.name,m);
  ok(byName.keySet().equals(new HashSet<>(Arrays.asList("Pack","hutao_v1","Legacy","RaidenShogun Kimono","Damaged"))),"list: "+byName.keySet());
  ok(byName.get("Damaged").converted&&byName.get("Damaged").outdated&&byName.get("Damaged").id==0,"an unreadable model.json asks for a re-conversion");
  ok(byName.get("Pack").converted&&byName.get("Pack").id==10000107&&!byName.get("Pack").outdated&&byName.get("Legacy").source==null&&byName.get("Legacy").id==0,"list entries");
  ok(!byName.get("RaidenShogun Kimono").converted&&byName.get("RaidenShogun Kimono").id==0&&byName.get("hutao_v1").outdated,"unconverted / changed character");
  Importer.delete(byName.get("hutao_v1"));ok(!new File(mods,"hutao_v1").exists()&&!hm.exists()&&good.isFile(),"delete keeps the original file");
  // 10. Names given to mods: kept apart from folders and models; blank, or the entry name itself, removes one.
  File lf=new File(base,"labels/com.miHoYo.Yuanshen.json");ok(Labels.read(lf).isEmpty(),"no labels yet");
  Labels.set(lf,"Pack","夏日\n茜特菈莉");Labels.set(lf,"hutao_v1","胡桃");Map<String,String> lab=Labels.read(lf);
  ok(lab.get("Pack").equals("夏日 茜特菈莉")&&lab.get("hutao_v1").equals("胡桃")&&Labels.of(lab,"Other").equals("Other"),"labels: "+lab);
  Labels.set(lf,"hutao_v1","  ");Labels.set(lf,"Pack","Pack");ok(Labels.read(lf).isEmpty(),"labels removed");
  Files.write(lf.toPath(),"{broken".getBytes(StandardCharsets.UTF_8));ok(Labels.read(lf).isEmpty(),"broken labels file ignored");
  // 11. XXMI Launcher log line: '%(asctime)s {instance_id:06} %(name)s %(levelname)s %(message)s'.
  String line=AppLog.line("importer",AppLog.INFO,"导入 a\nb",0);
  ok(line.matches("\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d,000 \\d{6} importer INFO 导入 a\n    b\n"),"log line: "+line);
  File logs=new File(base,"logs");AppLog.write(logs,AppLog.threshold("warning"),"importer",AppLog.INFO,"hidden");AppLog.write(logs,AppLog.threshold("warning"),"importer",AppLog.ERROR,"shown");AppLog.write(logs,AppLog.threshold("disabled"),"importer",AppLog.ERROR,"never");
  String text=read(new File(logs,AppLog.FILE));ok(!text.contains("hidden")&&text.contains(" importer ERROR shown")&&!text.contains("never")&&AppLog.threshold("debug")==AppLog.DEBUG&&AppLog.threshold("?")==AppLog.INFO,"log levels");
  ok(AppLog.tail(new File(logs,AppLog.FILE),1000).equals(text),"tail");
  // 12. Lower levels of detail: a library record made right after loading (Body_LOD3) is not used for an import, a
  //     conversion against it is refused, and a model converted against one is listed as needing a re-conversion.
  File lodBones=new File(base,"骨骼LOD");Fixtures.skeleton(new File(lodBones,"10000107"),10000107,"Scene/Avatar_Girl_Catalyst_Citlali/Bip001",new String[]{"body","face"},new String[]{"Root","Spine"},Collections.singletonMap("body",Fixtures.target("Body_LOD3",5246,2,2)));
  ok(ModLibrary.recorded(lodBones,10000107).lodMesh.equals("Body_LOD3")&&ModLibrary.recorded(bones,10000107).lodMesh.isEmpty(),"LOD record found");
  StringBuilder lodLog=new StringBuilder();r=Importer.convert(staged,10000107,models,lodBones,dec,lodLog);
  ok(!r.library&&Fixtures.part(pack,0).get("skeleton")!=null&&Json.obj(Fixtures.part(pack,0).get("target"),"target").get("mesh").equals("Body")&&lodLog.toString().contains("低精度模型 Body_LOD3")&&r.summary.contains("复用本 MOD 已有骨骼记录"),"bad LOD library rejected while the existing complete palette survives: "+lodLog);
  try{ModelConverter.bones(new File(lodBones,"10000107"),10000107);ok(false,"LOD record accepted");}catch(IllegalArgumentException e){ok(e.getMessage().contains("低精度模型 Body_LOD3"),e.getMessage());}
  Importer.convert(staged,10000107,models,bones,dec,new StringBuilder());
  File packJson=new File(pack,"model.json");String full=read(packJson);ok(full.contains("\"mesh\":\"Body\""),"target mesh written");
  Files.write(packJson.toPath(),full.replace("\"mesh\":\"Body\"","\"mesh\":\"Body_LOD3\"").getBytes(StandardCharsets.UTF_8));
  Importer.Mod pk=null;for(Importer.Mod m:Importer.list(mods,models))if(m.name.equals("Pack"))pk=m;
  ok(pk!=null&&pk.outdated&&pk.reason.contains("Body_LOD3")&&ModelManifest.read(pack).lodMesh.equals("Body_LOD3")&&!ModelManifest.lowerLod("Body_LOD0")&&ModelManifest.lowerLod("body_lod1 (Clone)"),"LOD model listed for re-conversion");
  // 13. Outfits (0.7.94): GIBox's own table (assets/outfits.json), which outfit an INI's hashes name (this device's map
  //     before the PC data, a hash the PC data gives to two outfits decides nothing), a bone library per outfit (a skin
  //     recorded in the character's folder before 0.7.94 counts for that skin only), and a mod made for a skin refused
  //     as the original look and converted against the skin's library with the outfit in model.json.
  Outfits table=Outfits.load(new File("app/src/main/assets/outfits.json"));Outfits.Outfit ff=table.byPc("ShenheFrostFlower");
  ok(table.of(10000063).size()==2&&ff.prefab.equals("avatar_lady_pole_shenhecostumedai")&&ff.skin==206301&&ff.name.equals("冷花幽露")&&ff.en.equals("Frostflower Dew")&&table.original(10000063).prefab.equals("avatar_lady_pole_shenhe"),"table: Shenhe");
  ok(table.of(10000003).size()==3&&table.byPc("JeanCN").skin==200302&&table.of(10000003).get(1).name.equals("海风之梦")&&table.of(10000107).get(1).name.equals("星与烟帷的夜语")&&table.of(10000052).isEmpty()&&table.byPc("XingqiuBamboo").skin==202501,"table: Jean, Citlali, Xingqiu, no skins");
  ok(Outfits.chainAvatar("Scene/Avatar_Lady_Pole_ShenheCostumeDai_Manekin(Clone)/Bip001").equals("avatar_lady_pole_shenhecostumedai")&&Outfits.chainAvatar("Scene/Bip001").isEmpty(),"chain avatar");
  File oshared=new File(base,"GIBox-outfit"),ob=new File(oshared,"skeletons");ob.mkdirs();
  for(String f:new String[]{"pc-hashes.json","device-hashes.json"})Files.copy(new File("app/src/main/assets/"+f).toPath(),new File(oshared,f).toPath(),StandardCopyOption.REPLACE_EXISTING);
  ok(table.guess(10000063,Collections.singleton("83a9116d"),oshared).outfit==ff&&table.guess(10000063,Collections.singleton("33a92492"),oshared).outfit.original(),"guess: skin and original by hash");
  ok(table.guess(10000003,Collections.singleton("115737ff"),oshared).outfit.original(),"guess: the device names 115737ff the original although the PC data gives it to JeanCN too");
  // 76df4025: the PC data gives it to Xingqiu and XingqiuBamboo; the game's own files (1.0.3 offline table) have it only
  // in the bamboo outfit's Body (the original Body is 82c97b1c), so the bundled table decides; without it, nothing does.
  ok(table.guess(10000025,Collections.singleton("76df4025"),oshared).outfit==table.byPc("XingqiuBamboo"),"guess: the bundled table names the outfit the PC data cannot");
  File onone=new File(base,"GIBox-outfit-pc-only");onone.mkdirs();Files.copy(new File("app/src/main/assets/pc-hashes.json").toPath(),new File(onone,"pc-hashes.json").toPath(),StandardCopyOption.REPLACE_EXISTING);
  Files.write(new File(onone,"device-hashes.json").toPath(),"{\"format\":\"gibox-hash-map\",\"version\":1,\"meshes\":[\n]}\n".getBytes(java.nio.charset.StandardCharsets.UTF_8));
  ok(table.guess(10000025,Collections.singleton("76df4025"),onone).outfit==null,"guess: a hash two outfits share in the PC data decides nothing");
  Outfits.Guess twoOutfits=table.guess(10000063,Arrays.asList("33a92492","83a9116d"),oshared);ok(twoOutfits.outfit==null&&twoOutfits.conflict.size()==2,"guess: hashes of two outfits");
  ok(table.guess(10000052,Collections.singleton("83a9116d"),oshared).outfit==null,"guess: a character without skins");
  File old=new File(base,"骨骼-旧皮肤");character(old,"10000063",10000063,"Avatar_Lady_Pole_ShenheCostumeDai_Manekin");
  ok(ModLibrary.recorded(old,10000063,table.original(10000063).prefab)==null&&ModLibrary.recorded(old,10000063,ff.prefab)!=null,"a skin recorded in the character's folder counts for the skin only");
  character(ob,"10000063",10000063,"Avatar_Lady_Pole_Shenhe");
  ok(ModLibrary.recorded(ob,10000063,ff.prefab)==null&&ModLibrary.recordedOutfits(ob,10000063,table).equals(Collections.singleton("avatar_lady_pole_shenhe")),"only the original recorded");
  character(ob,"10000063/"+ff.prefab,10000063,"Avatar_Lady_Pole_ShenheCostumeDai");
  ok(ModLibrary.recorded(ob,10000063,ff.prefab).bones.getName().equals(ff.prefab)&&ModLibrary.recorded(ob,10000063,"avatar_lady_pole_shenhe").bones.getName().equals("10000063")&&ModLibrary.recordedOutfits(ob,10000063,table).size()==2,"a library per outfit");
  File shm=renamed(base,"ShenheFF","Shenhe");File shIni=new File(shm,"Shenhe.ini");Files.write(shIni.toPath(),read(shIni).replace("hash = 44444444","hash = 83a9116d").getBytes(StandardCharsets.UTF_8));
  Outfits.use(table);File omodels=new File(base,"模型-外观");omodels.mkdirs();
  ok(Importer.guessOutfit(shm,10000063,ob).outfit==ff,"the picker's suggestion");
  Importer.setOutfit(shm,"avatar_lady_pole_shenhe");boolean refused=false;
  try{Importer.convert(shm,10000063,omodels,ob,dec,new StringBuilder());}catch(IllegalArgumentException e){refused=e.getMessage().contains("此 MOD 适用于「冷花幽露」")&&e.getMessage().contains("83a9116d")&&e.getMessage().contains("所选外观为「原皮」");}
  ok(refused&&!new File(omodels,"ShenheFF").exists(),"a Frostflower Dew mod is refused as the original look");
  Importer.setOutfit(shm,null);StringBuilder olog=new StringBuilder();r=Importer.convert(shm,10000063,omodels,ob,dec,olog);
  ok(r.library&&olog.toString().contains("外观：冷花幽露（avatar_lady_pole_shenhecostumedai）；依据 INI 哈希识别：83a9116d")&&olog.toString().contains("使用骨骼库："+new File(new File(ob,"10000063"),ff.prefab))&&r.summary.startsWith("申鹤·冷花幽露（10000063）"),"converted for the skin with its library: "+olog);
  ok(ModelManifest.read(new File(omodels,"ShenheFF")).outfit.equals(ff.prefab)&&Importer.outfit(shm)==null,"model.json names the outfit");
  Outfits.use(null);
  // Shared Mods (0.7.101): per-game Mods folders merged; same content once, a different mod of the same name renamed with its model.
  File shared=new File(base,"library"),legA=new File(base,"legacy/a"),legB=new File(base,"legacy/b"),modelsB=new File(base,"modelsB");
  File sa=renamed(legA,"Same","HuTao"),sb=renamed(legB,"Same","HuTao"),da=renamed(legA,"Clash","HuTao"),db=renamed(legB,"Clash","Odette");renamed(legB,"OnlyB","Odette");
  for(File f:sb.listFiles())f.setLastModified(f.lastModified()-86400000L);
  ok(ModLibrary.contentKey(sa).equals(ModLibrary.contentKey(sb))&&!ModLibrary.signature(sa).equals(ModLibrary.signature(sb))&&!ModLibrary.contentKey(da).equals(ModLibrary.contentKey(db)),"content key ignores mtimes");
  new File(modelsB,"Clash").mkdirs();Files.write(new File(modelsB,"Clash/model.json").toPath(),"{}".getBytes(StandardCharsets.UTF_8));new File(legB,".pending/x").mkdirs();
  StringBuilder ml=new StringBuilder();
  ok(Importer.mergeMods(shared,legA,new File(base,"modelsA"),ml).isEmpty()&&!legA.exists()&&new File(shared,"Same").isDirectory()&&new File(shared,"Clash").isDirectory(),"first game's Mods moved");
  List<Importer.Renamed> rn=Importer.mergeMods(shared,legB,modelsB,ml);
  ok(rn.size()==1&&rn.get(0).from.equals("Clash")&&rn.get(0).to.equals("Clash (2)")&&!legB.exists(),"clash renamed: "+ml);
  ok(new File(shared,"Clash (2)/Odette.ini").isFile()&&new File(shared,"Clash/HuTao.ini").isFile()&&new File(shared,"OnlyB/Odette.ini").isFile()&&!new File(shared,"Same (2)").exists(),"same content kept once");
  ok(new File(modelsB,"Clash (2)/model.json").isFile()&&!new File(modelsB,"Clash").exists(),"the renamed mod's model follows it");
  File[] lib=shared.listFiles(f->!f.getName().startsWith("."));ok(lib!=null&&lib.length==4,"four shared entries: "+Arrays.toString(lib));
  Importer.deleteModel("Clash (2)",modelsB);ok(!new File(modelsB,"Clash (2)").exists(),"another game's model deleted");
  System.out.println("Importer: PASS (shared Mods merge: duplicates once, clashes renamed with their model; outfit table, outfit by INI hash, library per outfit, a skin mod refused as the original and converted for its skin, lower-LOD library records and models, mod names, unreadable models listed, 7.1 character table, search by ID/name, recommended character first, folder/zip staging, picker prepare/commit/discard, zip-slip guard, bone-library and ID conversion, character change, Mods list, delete, XXMI log line and levels)");
 }
}
