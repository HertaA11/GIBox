package com.gibox.tool;
import java.io.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public final class ConverterTest {
 static void ok(boolean b,String s){if(!b)throw new AssertionError(s);}
 static ByteBuffer b(int n){return ByteBuffer.allocate(n).order(ByteOrder.LITTLE_ENDIAN);}
 static void put(File f,String n,byte[] a)throws Exception{Files.write(new File(f,n).toPath(),a);}
 static void put(File f,String n,String a)throws Exception{put(f,n,a.getBytes(StandardCharsets.UTF_8));}
 static String ini="[Constants]\nglobal persist $variant = 0\n[TextureOverridePosition]\nvb0 = ResourcePosition\n[TextureOverrideBlend]\nvb1 = ResourceBlend\n[TextureOverrideTexcoord]\nvb2 = ResourceTexcoord\n[TextureOverrideBodyA]\nib = ResourceIndex\nps-t0 = ResourceDiffuse\nif $variant == 0\ndrawindexed = 3, 0, 0\nelse if $variant == 1\ndrawindexed = 3, 3, 0\nendif\n[ResourcePosition]\nfilename = Position.buf\nstride = 40\n[ResourceBlend]\nfilename = Blend.buf\nstride = 32\n[ResourceTexcoord]\nfilename = Texcoord.buf\nstride = 12\n[ResourceIndex]\nfilename = Index.buf\nformat = R32_UINT\n[ResourceDiffuse]\nfilename = Diffuse.dds\n";
 /** Material of the slot-0 (Head) draw: draws come in table order, not slot order. */
 static Object headMaterial(File out,Map<String,Integer> a)throws Exception{for(Object d:Fixtures.draws(out,0,a))if(Fixtures.number(Json.obj(d,"draw"),"slot")==0)return Json.obj(d,"draw").get("material");throw new AssertionError("no slot-0 draw");}
 public static void main(String[] args)throws Exception{
  File root=Files.createTempDirectory("model-converter-test").toFile(),src=new File(root,"src"),bones=new File(root,"bones"),dest=new File(root,"model");src.mkdir();bones.mkdir();put(src,"mod.ini",ini);
  boolean bad;ByteBuffer pos=b(120),blend=b(96),uv=b(36),bind=b(64);for(int i=0;i<3;i++){pos.position(i*40);pos.putFloat(i).putFloat(0).putFloat(0).putFloat(0).putFloat(1).putFloat(0).putFloat(1).putFloat(0).putFloat(0).putFloat(1);blend.putFloat(i*32,2);uv.position(i*12);uv.putInt(-1).putFloat(.25f).putFloat(.75f);}for(int i=0;i<4;i++)bind.putFloat((i*4+i)*4,1);
  put(src,"Position.buf",pos.array());put(src,"Blend.buf",blend.array());put(src,"Texcoord.buf",uv.array());put(src,"Index.buf",b(24).putInt(2).putInt(0).putInt(1).putInt(0).putInt(1).putInt(2).array());put(src,"Diffuse.dds",new byte[]{1});
  byte[] rgba=b(24).putInt(2).putInt(2).putInt(0x01020304).putInt(0x05060708).putInt(0x090a0b0c).putInt(0x0d0e0f10).array();
  Fixtures.skeleton(bones,10000107,"Scene/Avatar_Girl_Catalyst_Citlali",new String[]{"Body"},new String[]{"BoneA"},Collections.singletonMap("Body",Fixtures.target("Body",23521,3,1)));
  List<File> outputs=new ArrayList<>(ModelConverter.convert(src,bones,dest,10000107,"",true,dds->rgba,new StringBuilder()));ok(outputs.size()==1,"variants stay in one folder");
  ModelManifest mf=ModelManifest.read(dest);ok(mf.version==2&&mf.vars.get("$variant").equals(Arrays.asList(0,1))&&mf.combos.isEmpty()&&mf.defaults.get("$variant")==0&&Variants.read(dest).defaults.get("$variant")==0,"variants in model.json: "+mf.vars+" "+mf.combos);
  Map<String,Object> d1=Json.obj(Fixtures.draws(dest,0,Fixtures.set("$variant",1)).get(0),"draw");ok(Fixtures.number(d1,"start")==3&&Fixtures.number(d1,"count")==3&&"Material0".equals(d1.get("material")),"combo 1 uses the second index range");
  Map<String,Object> p0=Fixtures.part(dest,0);ok(Fixtures.number(Json.obj(p0.get("target"),"target"),"vertices")==23521&&mf.id==10000107&&mf.names.isEmpty()&&!mf.generation.isEmpty(),"target copied, generation written");
  ok("Body".equals(p0.get("renderer"))&&Fixtures.number(p0,"bones")==1&&Json.obj(p0.get("skeleton"),"skeleton").get("bones").equals(Collections.singletonList("BoneA"))&&Json.integer(Json.arr(Json.obj(p0.get("skeleton"),"skeleton").get("parents"),"parents",1,1).get(0),"parent",-1,0)==-1,"renderer and skeleton (with parents)");
  for(File out:outputs){Map<String,Object> p=Fixtures.part(out,0);ok(Fixtures.number(p,"vertices")==3&&Fixtures.number(p,"indices")==6,"vertices shared by both variants, two index ranges");ok(Fixtures.data(out,0,"uv").getFloat(4)==.25f,"V flip");
   ok(Fixtures.data(out,0,"weights").getFloat(0)==1,"normalized weights");ok(Fixtures.view(out,Json.obj(Fixtures.material(out,"Material0").get("main"),"main").get("data")).getInt(0)==0x090a0b0c,"row flip");}
  // 64 assignments (more than the old limit of 32): each draw keeps only the variables it depends on; a variable an if
  // reads without changing any draw ($g), and one only key sections read ($active), are not offered.
  {File many=new File(root,"many");many.mkdirs();for(String f:new String[]{"Position.buf","Blend.buf","Texcoord.buf","Index.buf","Diffuse.dds"})Files.copy(new File(src,f).toPath(),new File(many,f).toPath());
   StringBuilder m=new StringBuilder("[Constants]\nglobal $active = 0\n");for(String v:new String[]{"b","c","d","e","f","g"})m.append("global persist $").append(v).append(" = 0\n");
   for(String v:new String[]{"b","c","d","e","f","g"})m.append("[Key").append(v).append("]\nkey = ").append(v).append("\ncondition = $active == 1\ntype = cycle\n$").append(v).append(" = 0,1\n");
   m.append("[TextureOverridePosition]\nvb0 = ResourcePosition\nif $g == 1\n$active = 1\nendif\n[TextureOverrideBlend]\nvb1 = ResourceBlend\n[TextureOverrideTexcoord]\nvb2 = ResourceTexcoord\n");
   m.append("[TextureOverrideBodyA]\nib = ResourceIndex\nps-t0 = ResourceDiffuse\ndrawindexed = 3, 0, 0\n");
   for(String v:new String[]{"b","c","d","e"})m.append("[TextureOverrideBody").append(v.toUpperCase()).append("]\nib = ResourceIndex\nps-t0 = ResourceDiffuse\nif $").append(v).append(" == 1\ndrawindexed = 3, 3, 0\nendif\n");
   m.append("[TextureOverrideBodyF]\nib = ResourceIndex\nps-t0 = ResourceDiffuse\nif $e == 1\nif $f == 1\ndrawindexed = 3, 0, 0\nendif\nendif\n");
   m.append(ini.substring(ini.indexOf("[ResourcePosition]")));put(many,"mod.ini",m.toString());
   File manyOut=new File(root,"many_out");StringBuilder ml=new StringBuilder();ModelConverter.convert(many,bones,manyOut,10000107,"",true,dds->rgba,ml);ModelManifest mm=ModelManifest.read(manyOut);
   ok(mm.vars.keySet().equals(new LinkedHashSet<>(Arrays.asList("$b","$c","$d","$e","$f")))&&mm.defaults.values().stream().allMatch(x->x==0),"variables offered: "+mm.vars.keySet()+"\n"+ml);
   Map<String,Integer> ef=new HashMap<>();ef.put("$e",1);ef.put("$f",1);
   ok(Fixtures.draws(manyOut,0,new HashMap<>()).size()==1&&Fixtures.draws(manyOut,0,Fixtures.set("$b",1)).size()==2&&Fixtures.draws(manyOut,0,ef).size()==3&&Fixtures.draws(manyOut,0,Fixtures.set("$f",1)).size()==1,"draws by condition");
   Map<String,Object> f=Json.obj(Fixtures.draws(manyOut,0,ef).get(2),"draw");ok(Json.obj(f.get("when"),"when").get("vars").equals(Arrays.asList("$e","$f"))&&Json.write(f.get("when")).replaceAll("\\s","").contains("[[1,1]]"),"nested condition kept: "+Json.write(f));
   ok(!ml.toString().contains("只转换默认款式"),"no fallback to the default variant");}
  // 3DMigoto operators: === and !== (as == and !=), // (floor division), ** (power).
  // A Resource's semantic name still identifies a texture when the filename has no role or misspells "Diffuse".
  {File renamed=new File(root,"renamed_texture");renamed.mkdirs();for(String f:new String[]{"Position.buf","Blend.buf","Texcoord.buf","Index.buf"})Files.copy(new File(src,f).toPath(),new File(renamed,f).toPath());
   put(renamed,"HairDifuse_Type_2_black.dds",new byte[]{1});put(renamed,"mod.ini",ini.replace("filename = Diffuse.dds","filename = HairDifuse_Type_2_black.dds"));
   File renamedOut=new File(root,"renamed_texture_out");ModelConverter.convert(renamed,bones,renamedOut,10000107,"",true,dds->rgba,new StringBuilder());
   Map<String,Object> material=Fixtures.material(renamedOut,"Material0");ok(material.containsKey("main")&&!Boolean.TRUE.equals(material.get("original")),"ResourceDiffuse must not silently use the game's original texture: "+material);}
  // NNFix defines roles by slots, even when both filenames lie (the real Shenhe package has exactly this layout).
  // Exercise a nested call, texture variants, another draw without NNFix, and hash-level inherited auto draws.
  {File nn=new File(root,"nnfix");nn.mkdirs();for(String f:new String[]{"Position.buf","Blend.buf","Texcoord.buf","Index.buf"})Files.copy(new File(src,f).toPath(),new File(nn,f).toPath());
   put(nn,"BodyNormalMap.dds",new byte[]{11});put(nn,"BodyNormalMapAlt.dds",new byte[]{22});put(nn,"BodyDiffuse.dds",new byte[]{33});
   String prefix=ini.substring(0,ini.indexOf("[TextureOverrideBodyA]"))+"[KeyVariant]\ntype = cycle\n$variant = 0,1\n";
   String defs=ini.substring(ini.indexOf("[ResourcePosition]"),ini.indexOf("[ResourceDiffuse]"))
    +"[ResourceColor]\nfilename = BodyNormalMap.dds\n[ResourceColorAlt]\nfilename = BodyNormalMapAlt.dds\n[ResourceMask]\nfilename = BodyDiffuse.dds\n";
   String fixed="if $variant == 0\nps-t0 = ResourceColor\nelse\nps-t0 = ResourceColorAlt\nendif\nps-t1 = ResourceMask\nrun = CommandListNested\n";
   String local="[CommandListNested]\nrun = CommandList\\global\\ORFix\\NNFix\n";
   String auto="[TextureOverrideHeroIB]\nhash = aabbccdd\nhandling = skip\ndrawindexed = auto\n";
   for(boolean inheritedAuto:new boolean[]{false,true}){
    String draw=inheritedAuto?"":"drawindexed = 3, 0, 0\n";
    String bodyA="[TextureOverrideBodyA]\nhash = aabbccdd\nmatch_first_index = 0\nib = ResourceIndex\n"+fixed+draw;
    String bodyB="[TextureOverrideBodyB]\nhash = aabbccdd\nmatch_first_index = 3\nib = ResourceIndex\nps-t0 = ResourceMask\nps-t1 = ResourceColor\n"+draw;
    put(nn,"mod.ini",prefix+(inheritedAuto?auto:"")+bodyA+bodyB+local+defs);
    File nnOut=new File(root,"nnfix_out_"+inheritedAuto);StringBuilder nnLog=new StringBuilder();
    ModelConverter.convert(nn,bones,nnOut,10000107,"",true,dds->b(12).putInt(1).putInt(1).putInt(dds[0]&255).array(),nnLog);
    for(int variant:new int[]{0,1}){
     List<Object> draws=Fixtures.draws(nnOut,0,Fixtures.set("$variant",variant));ok(draws.size()==2,"both draws retained");
     for(Object entry:draws){Map<String,Object> d=Json.obj(entry,"draw"),m=Fixtures.material(nnOut,(String)d.get("material"));
      int color=Fixtures.view(nnOut,Json.obj(m.get("main"),"main").get("data")).getInt(0);
      if(Fixtures.number(d,"slot")==0){ok(color==(variant==0?11:22),"NNFix ps-t0 is albedo, including variants");
       ok(Fixtures.view(nnOut,Json.obj(m.get("lightmap"),"lightmap").get("data")).getInt(0)==33&&!m.containsKey("normalmap"),"NNFix ps-t1 is the mask; no normalmap");}
      else ok(color==33&&m.containsKey("normalmap")&&!m.containsKey("lightmap"),"NNFix must not leak into another draw");
     }
    }
    ok(nnLog.toString().contains("已按 NNFix 识别贴图")&&!nnLog.toString().contains("已忽略外部 CommandList"),"known texture convention is reported accurately");
   }
  }
  // A literal key cycle defines selectable states; inactive legacy branches are not additional variants. Keep the
  // initial value, union several cycles, and keep condition inference for variables that have no literal cycle.
  {File cycle=new File(root,"cycle");cycle.mkdirs();for(String f:new String[]{"Position.buf","Blend.buf","Texcoord.buf","Index.buf","Diffuse.dds"})Files.copy(new File(src,f).toPath(),new File(cycle,f).toPath());
   String text=ini.replace("[TextureOverridePosition]","[KeyDress]\ntype = cycle\n$variant = 0,1\n[TextureOverridePosition]")
    .replace("ps-t0 = ResourceDiffuse\nif $variant", "if $variant < 2\nps-t0 = ResourceDiffuse\nendif\nif $variant")
    .replace("else if $variant == 1", "else if $variant == 1 || $variant == 3 || $variant == 4");
   put(cycle,"mod.ini",text);ModelConverter.Ini ci=new ModelConverter.Ini(cycle);
   ok(ci.choices.get("$variant").equals(new TreeSet<>(Arrays.asList(0,1))),"comparison constants must not extend a cycle: "+ci.choices);
   File cycleOut=new File(root,"cycle_out");StringBuilder cl=new StringBuilder();ModelConverter.convert(cycle,bones,cycleOut,10000107,"",true,dds->rgba,cl);
   ok(ModelManifest.read(cycleOut).vars.get("$variant").equals(Arrays.asList(0,1))&&cl.toString().contains("按按键列出的值"),"only reachable states are exported: "+cl);
   for(int v:new int[]{0,1})for(Object d:Fixtures.draws(cycleOut,0,Fixtures.set("$variant",v)))ok(!Boolean.TRUE.equals(Fixtures.material(cycleOut,(String)Json.obj(d,"draw").get("material")).get("original")),"legacy untextured state leaked");
   put(cycle,"mod.ini",text.replace("global persist $variant = 0","global persist $variant = 9")+"\n[KeyMore]\ntype = cycle\n$variant = 5,6\n");ci=new ModelConverter.Ini(cycle);
   ok(ci.choices.get("$variant").equals(new TreeSet<>(Arrays.asList(0,1,5,6,9))),"cycles union and initial value: "+ci.choices);
   put(cycle,"mod.ini",text.replace("type = cycle","type = hold").replace("[TextureOverridePosition]","[TextureOverridePosition]\n$variant = 7"));ci=new ModelConverter.Ini(cycle);
   ok(ci.choices.get("$variant").equals(new TreeSet<>(Arrays.asList(0,1,7))),"any key type, plus values sections assign: "+ci.choices);
   put(cycle,"mod.ini",text.replace("[KeyDress]\ntype = cycle\n$variant = 0,1\n",""));ci=new ModelConverter.Ini(cycle);
   ok(ci.choices.get("$variant").containsAll(Arrays.asList(2,3,4)),"a variable no key sets keeps its comparison values");}
  {Map<String,Integer> v=new HashMap<>();v.put("$dress",4);v.put("$a",2);
   ok(new ModelConverter.Expr("$dress!==4",v).eval()==0&&new ModelConverter.Expr("$dress !== 3",v).eval()==1&&new ModelConverter.Expr("$a===2",v).eval()==1&&new ModelConverter.Expr("-7//2",v).eval()==-4&&new ModelConverter.Expr("2**3+$a",v).eval()==10,"3DMigoto operators");}
  // 13 toggles, each in its own section: 8192 assignments in all, but 13 independent groups of 2, so every toggle is
  // offered. A Resource whose file depends on $tex ties $tex to the section that binds it ($b).
  {File wide=new File(root,"wide");wide.mkdirs();for(String f:new String[]{"Position.buf","Blend.buf","Texcoord.buf","Index.buf","Diffuse.dds"})Files.copy(new File(src,f).toPath(),new File(wide,f).toPath());
   Files.copy(new File(src,"Diffuse.dds").toPath(),new File(wide,"Diffuse2.dds").toPath());
   StringBuilder m=new StringBuilder("[Constants]\n");for(int i=0;i<13;i++)m.append("global persist $t").append(i).append(" = 0\n");m.append("global persist $tex = 0\nglobal persist $dress = 0\n");
   m.append("[TextureOverridePosition]\nvb0 = ResourcePosition\n[TextureOverrideBlend]\nvb1 = ResourceBlend\n[TextureOverrideTexcoord]\nvb2 = ResourceTexcoord\n");
   m.append("[TextureOverrideBodyA]\nib = ResourceIndex\nps-t0 = ResourceDiffuse\nif $dress !== 4\ndrawindexed = 3, 0, 0\nendif\n");
   for(int i=0;i<13;i++)m.append("[TextureOverrideBody").append((char)('B'+i)).append("]\nib = ResourceIndex\nps-t0 = ").append(i==0?"ResourceSkin":"ResourceDiffuse").append("\nif $t").append(i).append(" == 1\ndrawindexed = 3, 3, 0\nendif\n");
   m.append(ini.substring(ini.indexOf("[ResourcePosition]"))).append("[ResourceSkin]\nif $tex == 1\nfilename = Diffuse2.dds\nelse\nfilename = Diffuse.dds\nendif\n");
   String text=m.toString().replace("global persist $dress = 0","global persist $dress = 0\n[KeyDress]\nkey = d\ntype = cycle\n$dress = 0,4");put(wide,"mod.ini",text);
   File wideOut=new File(root,"wide_out");StringBuilder wl=new StringBuilder();ModelConverter.convert(wide,bones,wideOut,10000107,"",true,dds->rgba,wl);ModelManifest wm=ModelManifest.read(wideOut);
   ok(wm.vars.size()==15&&wm.vars.containsKey("$tex")&&wm.vars.containsKey("$t12")&&wm.vars.get("$dress").equals(Arrays.asList(0,4))&&!wl.toString().contains("只")&&!wl.toString().contains("过多"),"grouped variables: "+wm.vars.keySet()+"\n"+wl);
   ok(Fixtures.draws(wideOut,0,new HashMap<>()).size()==1&&Fixtures.draws(wideOut,0,Fixtures.set("$dress",4)).isEmpty()&&Fixtures.draws(wideOut,0,Fixtures.set("$t12",1)).size()==2,"draws per group");
   Map<String,Integer> bt=new HashMap<>();bt.put("$t0",1);bt.put("$tex",1);Map<String,Integer> b0=Fixtures.set("$t0",1);
   String m1=(String)Json.obj(Fixtures.draws(wideOut,0,bt).get(1),"draw").get("material"),m0=(String)Json.obj(Fixtures.draws(wideOut,0,b0).get(1),"draw").get("material");
   ok(!m1.equals(m0),"the Resource's variable picks the texture: "+m0+" "+m1);
   // Every draw of the part's sections keeps its slot by match order, whatever else is drawn.
   for(Object d:Fixtures.draws(wideOut,0,bt))ok(Fixtures.number(Json.obj(d,"draw"),"slot")<14,"slot");}
  // Groups the INI structure does not tie together but that do affect each other (a CommandList binds a Resource whose
  // file depends on $tex) are found by trying them together and merged; drawindexed = 0 draws nothing.
  {File cross=new File(root,"cross");cross.mkdirs();for(String f:new String[]{"Position.buf","Blend.buf","Texcoord.buf","Index.buf","Diffuse.dds"})Files.copy(new File(src,f).toPath(),new File(cross,f).toPath());
   Files.copy(new File(src,"Diffuse.dds").toPath(),new File(cross,"Diffuse2.dds").toPath());
   String text="[Constants]\nglobal persist $b = 0\nglobal persist $tex = 0\n[TextureOverridePosition]\nvb0 = ResourcePosition\n[TextureOverrideBlend]\nvb1 = ResourceBlend\n[TextureOverrideTexcoord]\nvb2 = ResourceTexcoord\n"
    +"[TextureOverrideBodyA]\nib = ResourceIndex\nps-t0 = ResourceDiffuse\ndrawindexed = 3, 0, 0\ndrawindexed = 0, 0, 0\n[TextureOverrideBodyB]\nib = ResourceIndex\nif $b == 1\nrun = CommandListSkin\ndrawindexed = 3, 3, 0\nendif\n[CommandListSkin]\nps-t0 = ResourceSkin\n"
    +ini.substring(ini.indexOf("[ResourcePosition]"))+"[ResourceSkin]\nif $tex == 1\nfilename = Diffuse2.dds\nelse\nfilename = Diffuse.dds\nendif\n";
   put(cross,"mod.ini",text);File crossOut=new File(root,"cross_out");StringBuilder cl=new StringBuilder();ModelConverter.convert(cross,bones,crossOut,10000107,"",true,dds->rgba,cl);ModelManifest cm=ModelManifest.read(crossOut);
   Map<String,Integer> both=new HashMap<>();both.put("$b",1);both.put("$tex",1);
   ok(cm.vars.keySet().equals(new HashSet<>(Arrays.asList("$b","$tex")))&&Fixtures.draws(crossOut,0,new HashMap<>()).size()==1,"merged groups: "+cm.vars.keySet()+"\n"+cl);
   String m1=(String)Json.obj(Fixtures.draws(crossOut,0,both).get(1),"draw").get("material"),m0=(String)Json.obj(Fixtures.draws(crossOut,0,Fixtures.set("$b",1)).get(1),"draw").get("material");ok(!m1.equals(m0),"texture follows $tex");}
  // One vertex's weights: a bone listed twice is added up, the largest comes first, the sum is 1.
  {float[] w={.1f,.4f,.2f,0};int[] ids={7,3,7,9};ByteBuffer o=b(32);ModelConverter.tidy(w,ids,o);
   ok(Math.abs(o.getFloat(0)-4/7f)<1e-6&&o.getInt(16)==3&&Math.abs(o.getFloat(4)-3/7f)<1e-6&&o.getInt(20)==7&&o.getFloat(8)==0&&o.getInt(24)==0,"weights tidied");}
  // The opt-in animation fallback keeps the supported association ratios and still rejects malformed data.
  {ModelConverter.ExtraBoneFallback fallback=new ModelConverter.ExtraBoneFallback();float[] w={1,.25f,.75f,0};int[] ids={119,2,4,0};fallback.apply(w,ids,114,"Blend.buf",1641);ByteBuffer out=b(32);ModelConverter.cleanWeights(w,ids,114);ModelConverter.tidy(w,ids,out);
   ok(out.getFloat(0)==.75f&&out.getInt(16)==4&&out.getFloat(4)==.25f&&out.getInt(20)==2&&fallback.vertices==1&&fallback.associations==1,"experimental fallback preserves the relative supported associations");
   for(float invalid:new float[]{-1,Float.NaN,Float.POSITIVE_INFINITY}){boolean refused=false;try{fallback.apply(new float[]{1,invalid,0,0},new int[]{0,119,0,0},114,"Blend.buf",1);}catch(IllegalArgumentException ex){refused=true;}ok(refused,"fallback still rejects malformed weights");}
   boolean refused=false;try{fallback.apply(new float[]{1,1,0,0},new int[]{-1,119,0,0},114,"Blend.buf",1);}catch(IllegalArgumentException ex){refused=true;}ok(refused,"fallback must not hide negative indices");}
  // Lumine's Body contains 20 vertices with a fourth, tiny weight on index 114. Test the actual vertex-7891 values,
  // then both conversion paths: named skeleton index validation and synthetic minimum-bone count calculation.
  {float[] w={.5608007311820984f,.3355036675930023f,.1036955714225769f,5.960464477539063e-8f};int[] ids={1,0,2,114};ByteBuffer out=b(32);
   ok(ModelConverter.cleanWeights(w,ids,114)==1,"actual Lumine residue removed before validating index 114");ModelConverter.tidy(w,ids,out);
   ok(out.getFloat(12)==0&&out.getInt(28)==0&&Math.abs(out.getFloat(0)+out.getFloat(4)+out.getFloat(8)-1)<1e-6,"remaining associations are preserved and normalized");
   byte[] noisy=blend.array().clone();ByteBuffer nb=ByteBuffer.wrap(noisy).order(ByteOrder.LITTLE_ENDIAN);
   for(int i=0;i<3;i++){nb.putFloat(i*32+12,1.1920929e-7f);nb.putInt(i*32+28,114);}put(src,"Blend.buf",noisy);
   File namedNoise=new File(root,"named_noise"),indexedNoise=new File(root,"indexed_noise");
   outputs.addAll(ModelConverter.convert(src,bones,namedNoise,10000107,"",false,dds->rgba,new StringBuilder()));
   ModelConverter.convert(src,null,indexedNoise,10000107,new ModelConverter.Options(),dds->rgba,new StringBuilder());outputs.add(indexedNoise);
   ok(Fixtures.number(Fixtures.part(indexedNoise,0),"bones")==1,"epsilon residue must not inflate the required bone count to 115");
   for(File d:Arrays.asList(namedNoise,indexedNoise)){ByteBuffer skin=Fixtures.data(d,0,"weights");for(int i=0;i<3;i++)ok(skin.getFloat(i*32)==1&&skin.getFloat(i*32+12)==0&&skin.getInt(i*32+28)==0,"no dangling fourth association");}
   for(int i=0;i<3;i++)nb.putFloat(i*32+12,.25f);put(src,"Blend.buf",noisy);boolean refused=false;
   try{ModelConverter.convert(src,bones,new File(root,"real_bad_bone"),10000107,"",false,dds->rgba,new StringBuilder());}catch(IllegalArgumentException ex){refused=ex.getMessage().contains("骨骼索引越界");}ok(refused,"a significant out-of-range weight is never silently removed");
   File realIndexed=new File(root,"real_indexed");ModelConverter.convert(src,null,realIndexed,10000107,new ModelConverter.Options(),dds->rgba,new StringBuilder());outputs.add(realIndexed);
   ok(Fixtures.number(Fixtures.part(realIndexed,0),"bones")==115,"a real association still requires 115 bones");
   float[] scaled={1e-9f,0,0,0};int[] scaledIds={0,0,0,0};ok(ModelConverter.cleanWeights(scaled,scaledIds,1)==0&&scaled[0]>0,"threshold is relative to the vertex total, not raw buffer magnitude");
   for(float invalid:new float[]{-1e-8f,Float.NaN,Float.POSITIVE_INFINITY}){refused=false;try{ModelConverter.cleanWeights(new float[]{1,invalid,0,0},new int[4],1);}catch(IllegalArgumentException ex){refused=true;}ok(refused,"invalid weight must not be treated as residue");}
   put(src,"Blend.buf",blend.array());}
  // Vertices the author left without weights take the nearest weighted vertex's (old vertex 2 is first in the index
  // buffer, so compact vertex 0; its nearest, old vertex 1, lists bone 0 twice: 1 + 3, merged to 1); a part with no
  // weights at all is refused.
  {byte[] holes=blend.array().clone();ByteBuffer h=ByteBuffer.wrap(holes).order(ByteOrder.LITTLE_ENDIAN);h.putFloat(64,0);h.putFloat(32,1).putFloat(36,3);put(src,"Blend.buf",holes);
   StringBuilder hl=new StringBuilder();File holed=new File(root,"holes");ModelConverter.convert(src,bones,holed,10000107,"",false,dds->rgba,hl);ByteBuffer w=Fixtures.data(holed,0,"weights");
   ok(w.getFloat(0)==1&&w.getFloat(4)==0&&w.getFloat(32)==1&&hl.toString().contains("Body：1 个顶点没有权重"),"unweighted vertex repaired: "+w.getFloat(0)+" "+hl);
   for(int i=0;i<3;i++)h.putFloat(i*32,0);h.putFloat(36,0);put(src,"Blend.buf",holes);
   try{ModelConverter.convert(src,bones,new File(root,"none"),10000107,"",false,dds->rgba,new StringBuilder());ok(false,"no weights accepted");}catch(IllegalArgumentException e){ok(e.getMessage().contains("所有顶点的权重都是 0"),e.getMessage());}
   put(src,"Blend.buf",blend.array());}
  // GIMI binds both Blend and Texcoord to vb1 in separate override stages.
  put(src,"mod.ini",ini.replace("vb2 = ResourceTexcoord","vb1 = ResourceTexcoord"));
  outputs.addAll(ModelConverter.convert(src,bones,new File(root,"shared_slot"),10000107,"",false,dds->rgba,new StringBuilder()));
  // The same physical slot may be reused inside a nested CommandList too.
  String nested=ini.replace("vb2 = ResourceTexcoord","run = CommandListUV")+"\n[CommandListUV]\nvb1 = ResourceTexcoord\n";
  put(src,"mod.ini",nested);outputs.addAll(ModelConverter.convert(src,bones,new File(root,"nested"),10000107,"",false,dds->rgba,new StringBuilder()));
  // Official GIMI shape: a hash-level auto draw and per-part IB resources.
  String resourceDefs=ini.substring(ini.indexOf("[ResourcePosition]"));
  String inherited="[TextureOverrideHeroPosition]\nvb0 = ResourcePosition\n[TextureOverrideHeroBlend]\nvb1 = ResourceBlend\n[TextureOverrideHeroTexcoord]\nvb1 = ResourceTexcoord\n[TextureOverrideHeroIB]\nhash = aabbccdd\nhandling = skip\ndrawindexed = auto\n[TextureOverrideHeroBodyA]\nhash = aabbccdd\nmatch_first_index = 9000\nib = ResourceIndex\nps-t0 = ResourceDiffuse\n"+resourceDefs;
  put(src,"mod.ini",inherited);File inheritedOut=new File(root,"inherited");outputs.addAll(ModelConverter.convert(src,bones,inheritedOut,10000107,"",false,dds->rgba,new StringBuilder()));
  ok(Fixtures.number(Fixtures.part(inheritedOut,0),"indices")==6,"auto uses all split IB indices, not match_first_index offset");
  // Shenhe Frost Flower: the default $vision=1 has no active geometry override. Enumerate the enabled state before
  // rejecting the mod, preserve its initial switch, and distinguish original-model mode from an intentional hide.
  {File guarded=new File(root,"guarded");guarded.mkdirs();for(String f:new String[]{"Position.buf","Blend.buf","Texcoord.buf","Index.buf","Diffuse.dds"})Files.copy(new File(src,f).toPath(),new File(guarded,f).toPath());
   Files.copy(new File(src,"Diffuse.dds").toPath(),new File(guarded,"Diffuse2.dds").toPath());
   StringBuilder g=new StringBuilder("[Constants]\nglobal persist $vision = 1\nglobal persist $stockings = 0\nglobal persist $chest = 0\n");
   for(String key:new String[]{"vision","stockings","chest"})g.append("[Key").append(key).append("]\ntype = cycle\n$").append(key).append(" = 0,1\n");
   for(String stream:new String[]{"Position","Blend","Texcoord"})g.append("[TextureOverrideHero").append(stream).append("]\nif $vision == 0\n").append(stream.equals("Position")?"vb0":"vb1").append(" = Resource").append(stream).append("\nendif\n");
   g.append("[TextureOverrideHeroIB]\nhash = aabbccdd\nif $vision == 0\nhandling = skip\ndrawindexed = auto\nendif\n");
   String[] components={"Head","Body","Dress","Extra"};
   for(int i=0;i<4;i++)g.append("[TextureOverrideHero").append(components[i]).append("]\nhash = aabbccdd\nmatch_first_index = ").append(i*6).append("\nif $vision == 0\nib = ResourceIndex\nendif\nif $").append(i%2==0?"stockings":"chest").append(" == 0\nps-t0 = ResourceDiffuse\nelse\nps-t0 = ResourceDiffuse2\nendif\n");
   String guardedIni=g+resourceDefs+"[ResourceDiffuse2]\nfilename = Diffuse2.dds\n";put(guarded,"mod.ini",guardedIni);
   File guardedOut=new File(root,"guarded_out");outputs.addAll(ModelConverter.convert(guarded,bones,guardedOut,10000107,"",true,dds->rgba,new StringBuilder()));
   ModelManifest gm=ModelManifest.read(guardedOut);ok(gm.version==3&&gm.defaults.get("$vision")==1&&gm.vars.size()==3,"default-off mod retains all three toggles");
   Map<String,Object> gate=Json.obj(Fixtures.manifest(guardedOut).get("replaceWhen"),"replaceWhen");
   ok(gate.get("vars").equals(Collections.singletonList("$vision"))&&Json.write(gate.get("allow")).replaceAll("\\s","").equals("[[0]]"),"only $vision enables geometry: "+Json.write(gate));
   for(int vision=0;vision<2;vision++)for(int stockings=0;stockings<2;stockings++)for(int chest=0;chest<2;chest++){
    Map<String,Integer> a=new HashMap<>();a.put("$vision",vision);a.put("$stockings",stockings);a.put("$chest",chest);List<Object> ds=Fixtures.draws(guardedOut,0,a);
    ok(ds.size()==(vision==0?4:0),"all eight combinations retain their draw count: "+a);
    Set<Integer> slots=new TreeSet<>();for(Object d:ds)slots.add((int)Fixtures.number(Json.obj(d,"draw"),"slot"));
    ok(slots.size()==ds.size()&&(ds.isEmpty()||slots.equals(new TreeSet<>(Arrays.asList(0,1,2,3)))),"component slots stay at match_first_index order: "+ds);}
   // 显示替换: $vision is only the switch, so it is shown as 开/关 and not as its own row.
   Variants vs=Variants.read(guardedOut);ok(vs.hasSwitch()&&vs.hidden("$vision")&&!vs.hidden("$stockings")&&!vs.hidden("$chest"),"显示替换 replaces the $vision row");
   Map<String,Integer> now=new HashMap<>();now.put("$vision",1);now.put("$stockings",1);now.put("$chest",0);
   ok(!vs.replacing(now)&&vs.switchTo(true,now).equals(Fixtures.set("$vision",0))&&vs.switchTo(false,now).isEmpty(),"开 sets only the switch variable");
   now.put("$vision",0);ok(vs.replacing(now)&&vs.switchTo(false,now).equals(Fixtures.set("$vision",1)),"关 goes back to the mod's own off value");
   Variants two=new Variants();two.values.put("$a",Arrays.asList(0,1,2));two.values.put("$b",Arrays.asList(0,1));two.defaults.put("$a",0);two.defaults.put("$b",0);
   two.gateVars.addAll(Arrays.asList("$a","$b"));two.gateAllow.add(Arrays.asList(0,0));two.gateAllow.add(Arrays.asList(2,1));
   Map<String,Integer> off=two.switchTo(false,new HashMap<>(two.defaults));ok(!two.hidden("$a")&&off.size()==2&&!two.replacing(Variants.merged(two.defaults,off)),"关 with a switch over two variables picks an off row: "+off);
   Map<String,Integer> stockingOn=new HashMap<>();stockingOn.put("$vision",0);stockingOn.put("$stockings",1);
   ok(!headMaterial(guardedOut,Fixtures.set("$vision",0)).equals(headMaterial(guardedOut,stockingOn)),"stocking texture still switches");
   boolean refused=false;try{ModelConverter.convert(guarded,bones,new File(root,"guarded_default_only"),10000107,"",false,dds->rgba,new StringBuilder());}catch(IllegalArgumentException ex){refused=ex.getMessage().contains("当前只转换默认款式");}ok(refused,"default-only conversion explains why enumeration is needed");
   put(guarded,"mod.ini",guardedIni.replace("if $vision == 0\nhandling = skip\n","handling = skip\nif $vision == 0\n"));
   File hideOut=new File(root,"guarded_hide");outputs.addAll(ModelConverter.convert(guarded,bones,hideOut,10000107,"",true,dds->rgba,new StringBuilder()));
   ok(!Fixtures.manifest(hideOut).containsKey("replaceWhen")&&Fixtures.draws(hideOut,0,new HashMap<>()).isEmpty(),"handling=skip remains an intentional hide");
   put(guarded,"mod.ini",guardedIni.replace("drawindexed = auto","drawindexed = 0"));refused=false;
   try{ModelConverter.convert(guarded,bones,new File(root,"no_draw"),10000107,"",true,dds->rgba,new StringBuilder());}catch(IllegalArgumentException ex){refused=ex.getMessage().contains("已开启款式枚举");}ok(refused,"a mod with no draw in any state remains invalid");}
  // A more specific component wins over shared character defaults.
  byte[] hairPos=pos.array().clone(),hairBlend=blend.array().clone(),hairUV=uv.array().clone();for(int i=0;i<3;i++){ByteBuffer.wrap(hairPos).order(ByteOrder.LITTLE_ENDIAN).putFloat(i*40,100+i);ByteBuffer.wrap(hairBlend).order(ByteOrder.LITTLE_ENDIAN).putFloat(i*32,.5f).putFloat(i*32+4,.5f).putInt(i*32+20,1);ByteBuffer.wrap(hairUV).order(ByteOrder.LITTLE_ENDIAN).putFloat(i*12+4,.5f).putFloat(i*12+8,.5f);}
  put(src,"HairPosition.buf",hairPos);put(src,"HairBlend.buf",hairBlend);put(src,"HairTexcoord.buf",hairUV);
  String hairResources="\n[ResourceHairPosition]\nfilename = HairPosition.buf\nstride = 40\n[ResourceHairBlend]\nfilename = HairBlend.buf\nstride = 32\n[ResourceHairTexcoord]\nfilename = HairTexcoord.buf\nstride = 12\n";
  String scoped="[TextureOverrideHeroPosition]\nvb0 = ResourcePosition\n[TextureOverrideHeroBlend]\nvb1 = ResourceBlend\n[TextureOverrideHeroTexcoord]\nvb1 = ResourceTexcoord\n[TextureOverrideHeroHairPosition]\nvb0 = ResourceHairPosition\n[TextureOverrideHeroHairBlend]\nvb1 = ResourceHairBlend\n[TextureOverrideHeroHairTexcoord]\nvb1 = ResourceHairTexcoord\n[TextureOverrideHeroBodyA]\nib = ResourceIndex\ndrawindexed = 3,0,0\n[TextureOverrideHeroHairA]\nib = ResourceIndex\ndrawindexed = 3,0,0\n"+resourceDefs+hairResources;
  // (two bones here, so the hair's Blend stream, half on each, stays told apart from the body's after the weights are tidied)
  File bones2=new File(root,"bones2");Fixtures.skeleton(bones2,10000107,"Scene/Avatar_Girl_Catalyst_Citlali",new String[]{"Body"},new String[]{"BoneA","BoneB"},Collections.singletonMap("Body",Fixtures.target("Body",23521,3,2)));
  put(src,"mod.ini",scoped);File scopedOut=new File(root,"scoped");outputs.addAll(ModelConverter.convert(src,bones2,scopedOut,10000107,"",false,dds->rgba,new StringBuilder()));
  ByteBuffer sg=Fixtures.data(scopedOut,0,"positions");ok(Fixtures.number(Fixtures.part(scopedOut,0),"vertices")==6&&sg.getFloat(0)==2&&sg.getFloat(3*12)==102,"component Position streams stay separate");
  ok(Fixtures.data(scopedOut,0,"weights").getFloat(32*3)==.5f,"component Blend selection");ok(Fixtures.data(scopedOut,0,"uv").getFloat(3*8)==.5f,"component Texcoord selection");
  put(src,"mod.ini",scoped+"\n[TextureOverrideUnrelated]\nib=ResourceIndex\ndrawindexed=3,0,0\n");outputs.addAll(ModelConverter.convert(src,bones2,new File(root,"skip"),10000107,"Unrelated=skip",false,dds->rgba,new StringBuilder()));
  // Unused ambiguous defaults must not override explicit per-draw bindings.
  String conflicting="[TextureOverrideBlend1]\nvb1=ResourceBlend\n[TextureOverrideBlend2]\nvb1=ResourceHairBlend\n";
  String explicit="[TextureOverrideBodyA]\nvb0=ResourcePosition\nvb1=ResourceBlend\nvb2=ResourceTexcoord\nib=ResourceIndex\ndrawindexed=3,0,0\n";
  put(src,"mod.ini",conflicting+explicit+resourceDefs+hairResources);outputs.addAll(ModelConverter.convert(src,bones2,new File(root,"explicit"),10000107,"",false,dds->rgba,new StringBuilder()));
  // A genuine same-role ambiguity still fails with useful source sections.
  put(src,"mod.ini",conflicting+"[TextureOverridePosition]\nvb0=ResourcePosition\n[TextureOverrideTexcoord]\nvb1=ResourceTexcoord\n[TextureOverrideBodyA]\nib=ResourceIndex\ndrawindexed=3,0,0\n"+resourceDefs+hairResources);
  boolean ambiguous=false;try{ModelConverter.convert(src,bones2,new File(root,"ambiguous"),10000107,"",false,dds->rgba,new StringBuilder());}catch(IllegalArgumentException ex){ambiguous=ex.getMessage().contains("Blend")&&ex.getMessage().contains("TextureOverrideBlend1")&&ex.getMessage().contains("TextureOverrideBlend2");}ok(ambiguous&&!new File(root,"ambiguous").exists(),"reject real ambiguity without selecting a random stream");
  // Resource aliases for the same file/stride are one logical candidate.
  String alias="[TextureOverrideBlend1]\nvb1=ResourceBlend\n[TextureOverrideBlend2]\nvb1=ResourceBlendAlias\n[TextureOverridePosition]\nvb0=ResourcePosition\n[TextureOverrideTexcoord]\nvb1=ResourceTexcoord\n[TextureOverrideBodyA]\nib=ResourceIndex\ndrawindexed=3,0,0\n"+resourceDefs+"\n[ResourceBlendAlias]\nfilename=Blend.buf\nstride=32\n";
  put(src,"mod.ini",alias);outputs.addAll(ModelConverter.convert(src,bones2,new File(root,"alias"),10000107,"",false,dds->rgba,new StringBuilder()));
  // Conditional providers select only the active variant's logical stream.
  String conditional=ini.replace("vb1 = ResourceBlend","if $variant == 0\nvb1=ResourceBlend\nelse if $variant == 1\nvb1=ResourceHairBlend\nendif").replace("vb2 = ResourceTexcoord","vb1 = ResourceTexcoord")+hairResources;
  put(src,"mod.ini",conditional);File conditionalOut=new File(root,"conditional");outputs.addAll(ModelConverter.convert(src,bones2,conditionalOut,10000107,"",true,dds->rgba,new StringBuilder()));ByteBuffer unionSkin=Fixtures.data(conditionalOut,0,"weights");ok(Fixtures.number(Fixtures.part(conditionalOut,0),"vertices")==6&&unionSkin.getFloat(0)==1f&&unionSkin.getFloat(32*3)==.5f,"each variant keeps its own Blend stream");
  // Re-import replaces the folder atomically and keeps what the runtime wrote, while the character stays the same.
  String firstGeneration=ModelManifest.read(dest).generation;put(dest,"bonemap-Part0.tsv","kept");put(dest,"learned.json","{\"format\":\"gibox-learned\",\"version\":1,\"id\":0,\"targets\":{}}");put(src,"mod.ini",ini);
  ModelConverter.Options replace=new ModelConverter.Options().variants(true).replace(true).source("/mods/x","abc");ModelConverter.convert(src,bones2,dest,10000107,replace,dds->rgba,new StringBuilder());
  ModelManifest again=ModelManifest.read(dest);ok(!again.generation.equals(firstGeneration)&&new File(dest,"bonemap-Part0.tsv").isFile()&&new File(dest,"learned.json").isFile()&&again.sourceSignature.equals("abc")&&again.sourcePath.equals("/mods/x"),"replace: new generation, kept runtime files, source");
  // The standalone library can disappear. Do not replace the same mod's checked named skeleton with index-only data,
  // or discard its learned files just because the importer now supplies the character's model-name alias.
  Object previousTarget=Fixtures.part(dest,0).get("target");ModelConverter.Options recovered=new ModelConverter.Options().variants(true).replace(true).names(Collections.singletonList("citlali"));StringBuilder recoveredLog=new StringBuilder();
  ModelConverter.convert(src,null,dest,10000107,recovered,dds->rgba,recoveredLog);
  ok(recovered.reusedSkeleton&&Fixtures.part(dest,0).get("skeleton")!=null&&Objects.equals(Fixtures.part(dest,0).get("target"),previousTarget)&&new File(dest,"learned.json").isFile()&&new File(dest,"bonemap-Part0.tsv").isFile()&&recoveredLog.toString().contains("骨骼复用"),"missing library: preserve the existing complete palette and identity across alias changes: "+recoveredLog);
  byte[] savedBlob=Files.readAllBytes(new File(dest,"model.bin").toPath());byte[] damaged=savedBlob.clone();damaged[0]^=1;put(dest,"model.bin",damaged);bad=false;try{ModelConverter.previousBones(dest,10000107);}catch(IllegalArgumentException ex){bad=ex.getMessage().contains("校验失败");}ok(bad,"corrupt source data cannot prove a reusable skeleton");put(dest,"model.bin",savedBlob);
  // Converted again for another character (here: without the skeleton library), what was learned for the old one goes.
  File swapped=new File(root,"swapped");ModelConverter.convert(src,bones,swapped,10000107,new ModelConverter.Options(),dds->rgba,new StringBuilder());put(swapped,"learned.json","{}");put(swapped,"bonemap-Part0.tsv","x");
  ModelConverter.convert(src,null,swapped,10000046,new ModelConverter.Options().replace(true).names(Collections.singletonList("hutao")),dds->rgba,new StringBuilder());ok(!new File(swapped,"learned.json").exists()&&!new File(swapped,"bonemap-Part0.tsv").exists()&&ModelManifest.read(swapped).id==10000046,"another character: runtime files dropped");
  // A skeleton record of another character is refused.
  bad=false;try{ModelConverter.convert(src,bones,new File(root,"wrongid"),10000046,new ModelConverter.Options(),dds->rgba,new StringBuilder());}catch(IllegalArgumentException e){bad=e.getMessage().contains("另一个角色");}ok(bad&&!new File(root,"wrongid").exists(),"skeleton of another character");
  File[] old=root.listFiles(f->f.getName().startsWith(".old-"));ok(old!=null&&old.length==0,"old folder removed");
  put(src,"mod.ini",ini);
  Map<String,Integer> v=new HashMap<>();v.put("$x",2);ok(new ModelConverter.Expr("($x + 1) * 2 == 6 && !0",v).eval()==1,"expression");bad=false;try{new ModelConverter.Expr("system(1)",v).eval();}catch(IllegalArgumentException e){bad=true;}ok(bad,"reject unknown expression");
  put(src,"Index.buf",b(4).putInt(999).array());File failed=new File(root,"failed");bad=false;try{ModelConverter.convert(src,bones,failed,10000107,"",false,dds->rgba,new StringBuilder());}catch(IllegalArgumentException e){bad=true;}ok(bad&&!failed.exists(),"rollback on invalid input");File[] temps=root.listFiles(f->f.getName().startsWith(".model-convert-"));ok(temps!=null&&temps.length==0,"temporary cleanup");
  for(File out:outputs){Process process=new ProcessBuilder(args[0],out.getAbsolutePath()).inheritIO().start();ok(process.waitFor()==0,"native parser validates converter output");}
  // No skeleton library: renderers by name, no bone names, bone count from the mod, character name in model.json.
  put(src,"mod.ini",ini);put(src,"Index.buf",b(24).putInt(2).putInt(0).putInt(1).putInt(0).putInt(1).putInt(2).array());File named=new File(root,"named");ModelConverter.convert(src,null,named,0,new ModelConverter.Options().names(Collections.singletonList("citlali")),dds->rgba,new StringBuilder());
  Map<String,Object> np=Fixtures.part(named,0);ModelManifest nm=ModelManifest.read(named);
  ok(np.get("skeleton")==null&&np.get("target")==null&&"Body".equals(np.get("renderer"))&&Fixtures.number(np,"bones")==1&&nm.id==0&&nm.names.equals(Collections.singletonList("citlali"))&&nm.character()==0,"conversion without skeleton library");
  put(named,"learned.json","{\"format\":\"gibox-learned\",\"version\":1,\"id\":10000107,\"targets\":{}}");ok(ModelManifest.read(named).character()==10000107,"learned character ID");
  Process np2=new ProcessBuilder(args[0],named.getAbsolutePath()).inheritIO().start();ok(np2.waitFor()==0,"native loader reads the by-name model");
  // model.json is checked when the app reads it, too.
  put(named,"model.json",new String(Files.readAllBytes(new File(named,"model.json").toPath()),StandardCharsets.UTF_8).replace("\"version\": 2","\"version\": 5"));bad=false;try{ModelManifest.read(named);}catch(IllegalArgumentException e){bad=e.getMessage().contains("版本");}ok(bad,"newer format refused");
  // Our JSON: strict like the native reader, exact float round trip.
  for(String j:new String[]{"{\"a\":1,\"a\":2}","[01]","[1,]","{\"a\":1} x","[\"\u0001\"]","[1e999]"}){bad=false;try{Json.parse(j);}catch(IllegalArgumentException e){bad=true;}ok(bad,"JSON accepted: "+j);}
  Object third=((List<?>)Json.parse(Json.write(Arrays.<Object>asList(1f/3,-2.5e-7f)))).get(0);ok(Json.finite(third,"f")==1f/3,"float round trip");
  ok(Json.parse("{\"s\":\"\\u6a21\\\"\"}").equals(Collections.singletonMap("s","模\"")),"escapes");
  // A part named for something no renderer is called ("Hood": Alyosha's Hat_Off) goes where its IB hash is in the game
  // (this device's hash map), not to Body with Body's bones; GIMI's Resource\GIMI\... textures with SetTextures are
  // taken by the role they name, whatever the files are called.
  {File g=new File(root,"gimi");g.mkdirs();for(String f:new String[]{"Position.buf","Blend.buf","Texcoord.buf","Index.buf"})Files.copy(new File(src,f).toPath(),new File(g,f).toPath());
   put(g,"a.dds",new byte[]{11});put(g,"b.dds",new byte[]{22});put(g,"c.dds",new byte[]{33});
   String gi="[TextureOverridePosition]\nvb0 = ResourcePosition\n[TextureOverrideBlend]\nvb1 = ResourceBlend\n[TextureOverrideTexcoord]\nvb2 = ResourceTexcoord\n"
    +"[TextureOverrideBodyA]\nhash = aaaa0001\nmatch_first_index = 0\nib = ResourceIndex\nps-t0 = ResourceA\ndrawindexed = 3, 0, 0\n"
    +"[TextureOverrideHoodA]\nhash = 8c11d133\nmatch_first_index = 0\nib = ResourceIndex\nResource\\GIMI\\Diffuse = ref ResourceA\nResource\\GIMI\\LightMap = ref ResourceB\nResource\\GIMI\\NormalMap = ref ResourceC\nrun = CommandList\\GIMI\\SetTextures\ndrawindexed = 3, 3, 0\n"
    +"[ResourcePosition]\nfilename = Position.buf\nstride = 40\n[ResourceBlend]\nfilename = Blend.buf\nstride = 32\n[ResourceTexcoord]\nfilename = Texcoord.buf\nstride = 12\n[ResourceIndex]\nfilename = Index.buf\nformat = R32_UINT\n"
    +"[ResourceA]\nfilename = a.dds\n[ResourceB]\nfilename = b.dds\n[ResourceC]\nfilename = c.dds\n";
   put(g,"mod.ini",gi);ModelConverter.TextureDecoder first=img->b(12).putInt(1).putInt(1).putInt(img[0]&255).array();
   Map<String,String> device=new HashMap<>();device.put("aaaa0001","Body");device.put("8C11D133","Hat_Off");
   File gOut=new File(root,"gimi_out");StringBuilder gl=new StringBuilder();
   ModelConverter.convert(g,null,gOut,10000148,new ModelConverter.Options().names(Collections.singletonList("alyosha")).fallback("Body").deviceRenderers(device),first,gl);
   ok("Hat_Off".equals(Fixtures.part(gOut,1).get("renderer"))&&"Body".equals(Fixtures.part(gOut,0).get("renderer"))&&gl.toString().contains("HoodA -> Hat_Off（IB hash 8c11d133 在设备 hash 表中是游戏的 Hat_Off）")&&!gl.toString().contains("按默认渲染器"),"Hood goes to Hat_Off by its IB hash: "+gl);
   Object hoodMat=Json.obj(Fixtures.draws(gOut,1,new HashMap<>()).get(0),"draw").get("material");Map<String,Object> hm=Fixtures.material(gOut,(String)hoodMat);
   int[] px=new int[3];String[] roles={"main","lightmap","normalmap"};for(int k=0;k<3;k++)px[k]=Fixtures.view(gOut,Json.obj(hm.get(roles[k]),roles[k]).get("data")).getInt(0);
   ok(px[0]==11&&px[1]==22&&px[2]==33&&gl.toString().contains("已按 GIMI 命名空间识别贴图")&&!gl.toString().contains("已忽略外部 CommandList：CommandList\\GIMI\\SetTextures"),"GIMI roles as named: "+Arrays.toString(px)+"\n"+gl);
   // Without SetTextures nothing is bound on PC: the GIMI lines alone give no textures.
   put(g,"mod.ini",gi.replace("run = CommandList\\GIMI\\SetTextures\n",""));File gOut2=new File(root,"gimi_out2");
   ModelConverter.convert(g,null,gOut2,10000148,new ModelConverter.Options().names(Collections.singletonList("alyosha")).fallback("Body").deviceRenderers(device),first,new StringBuilder());
   ok(Fixtures.material(gOut2,(String)Json.obj(Fixtures.draws(gOut2,1,new HashMap<>()).get(0),"draw").get("material")).get("main")==null,"no SetTextures, no GIMI textures");
   put(g,"mod.ini",gi);
   // With a bone library that has no Hat_Off: the part is left out (said why), not skinned with Body's bones.
   File gBones=new File(root,"gimi_bones");gBones.mkdirs();Fixtures.skeleton(gBones,10000148,"Scene/Avatar_Boy_Pole_Alyosha",new String[]{"Body"},new String[]{"BoneA"},Collections.singletonMap("Body",Fixtures.target("Body",23521,3,1)));
   File gOut3=new File(root,"gimi_out3");StringBuilder gl3=new StringBuilder();
   ModelConverter.convert(g,gBones,gOut3,10000148,new ModelConverter.Options().fallback("Body").deviceRenderers(device),first,gl3);
   ok(Json.arr(Fixtures.manifest(gOut3).get("parts"),"parts",1,64).size()==1&&gl3.toString().contains("HoodA：IB hash 8c11d133 在设备 hash 表中是游戏的 Hat_Off，骨骼库中没有它的记录，此部件不转换"),"no Hat_Off in the library: left out: "+gl3);
   // The hash map names the character too (Alyosha's mods call him Aloysha), and the name is an alias as well.
   File shared=new File(root,"gimi_shared");shared.mkdirs();
   put(shared,"device-hashes.json","{\"format\":\"gibox-hash-map\",\"version\":1,\"meshes\":[{\"ib\":\"8c11d133\",\"avatar\":\"avatar_boy_pole_alyosha\",\"renderer\":\"hat_off\",\"mesh\":\"Hat_Off\",\"vertices\":5443,\"bones\":15,\"submeshes\":[[0,22932]]},"
    +"{\"ib\":\"fdbdb407\",\"avatar\":\"avatar_boy_pole_alyosha\",\"renderer\":\"body\",\"mesh\":\"Body\",\"vertices\":25862,\"bones\":148,\"submeshes\":[[0,17973]]},{\"ib\":\"9e1e8513\",\"avatar\":\"avatar_male_claymore_varka\",\"renderer\":\"bang\",\"mesh\":\"Bang\",\"vertices\":1920,\"bones\":12,\"submeshes\":[[0,7554]]}]}");
   DeviceHashes dh=DeviceHashes.load(shared);
   ok(dh.renderers(10000148,null).toString().equals("{8c11d133=Hat_Off, fdbdb407=Body}")&&dh.characters(Arrays.asList("8c11d133","fdbdb407","12345678")).equals(Collections.singletonMap(10000148,Arrays.asList("8c11d133","fdbdb407"))),"device map: renderers and character: "+dh.renderers(10000148,null));
   Characters.Guess cg=Characters.guess(g,new File(g,"mod.ini"),"Something",null);ok(cg.recommended.isEmpty(),"no name says who");
   Characters.hashEvidence(cg,dh.characters(ModelConverter.overrideHashes(g,new File(g,"mod.ini"))));ok(cg.recommends(10000148)&&cg.recommended.size()==1&&cg.evidence.contains("hash 8c11d133=10"),"hash says Alyosha: "+cg.evidence);
   File spelled=new File(root,"AloyshaStripdown");spelled.mkdirs();put(spelled,"Aloysha.ini","[TextureOverrideAloyshaBodyA]\nhash = 1\n");ok(Characters.guess(spelled,new File(spelled,"Aloysha.ini"),null,null).recommends(10000148),"the Aloysha spelling");}
  System.out.println("Java converter: PASS (Hood by IB hash on Hat_Off, GIMI namespace textures by role, Alyosha by hash and name, groups that affect each other merged after a check, drawindexed = 0 ignored, variables enumerated per independent group, 3DMigoto === / !== / // / **, weights merged and sorted, variants by condition beyond 32 assignments, unweighted vertices repaired, GIBox model.json + model.bin, shared vb1, nested commands, inherited auto draw, component scopes, explicit overrides, real ambiguity, merged variants, conditional streams, targets, atomic replace keeping runtime files per character, skeleton.json, no-library conversion, strict JSON, binary interoperability)");
 }
}
