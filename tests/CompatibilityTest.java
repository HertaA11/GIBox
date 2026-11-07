package com.gibox.tool;

import java.io.File;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.util.*;

/** Regression inputs matching the uploaded legacy Nahida and HoHE package structures. Host-only. */
public final class CompatibilityTest {
    private static void ok(boolean value,String detail){if(!value)throw new AssertionError(detail);}
    private static final ModelConverter.TextureDecoder DECODE = image -> ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN)
        .putInt(1).putInt(1).putInt(image[0]&255).array();
    private static int pixel(File out,Map<String,Object> material,String role)throws Exception{
        return Fixtures.view(out,Json.obj(material.get(role),role).get("data")).getInt(0);
    }
    private static void legacy(File base)throws Exception{
        for(int mode=0;mode<3;mode++){
            String bindings="ps-t0 = ResourceNormalData\nif $swapvar == 4\nps-t1 = ResourceOtherColor\nelse\nps-t1 = ResourceColorData\nendif\nps-t2 = ResourceMaskData\n"
                +(mode==1?"run = CommandList\\global\\ORFix\\NNFix\n":"");
            String text=GimiPatternsTest.ini("global persist $swapvar = 0\n","",bindings,"")
                .replace("44444444",mode==2?"aabbccdd":"8b268d05")
                +"\n[KeySwap]\nkey = r\ntype = cycle\n$swapvar = 0,1,2,3,4\n"
                +"[ResourceNormalData]\nfilename = FalseDiffuse.dds\n[ResourceColorData]\nfilename = FalseLightMap.dds\n"
                +"[ResourceOtherColor]\nfilename = OtherLightMap.dds\n[ResourceMaskData]\nfilename = FalseShadow.dds\n";
            File src=GimiPatternsTest.mod(base,"legacy"+mode,text),out=new File(base,"legacy-out"+mode);
            GimiPatternsTest.put(src,"FalseDiffuse.dds",new byte[]{11});GimiPatternsTest.put(src,"FalseLightMap.dds",new byte[]{22});
            GimiPatternsTest.put(src,"OtherLightMap.dds",new byte[]{44});GimiPatternsTest.put(src,"FalseShadow.dds",new byte[]{33});
            // Same numeric index data, encoded as the original legacy package's R16 stream.
            GimiPatternsTest.put(src,"CitlaliHead.ib",ByteBuffer.allocate(6).order(ByteOrder.LITTLE_ENDIAN).putShort((short)0).putShort((short)1).putShort((short)2).array());
            GimiPatternsTest.put(src,"CitlaliBody.ib",ByteBuffer.allocate(6).order(ByteOrder.LITTLE_ENDIAN).putShort((short)3).putShort((short)4).putShort((short)5).array());
            GimiPatternsTest.put(src,"Citlali.ini",text.replace("R32_UINT","R16_UINT"));
            StringBuilder log=new StringBuilder();ModelConverter.convert(src,null,out,10000073,new ModelConverter.Options().variants(true).fallback("Body"),DECODE,log);
            // Outfit logic data: one record per distinct drawn result, with its value of the variant variable; the
            // native check sets each outfit's values in the logic and needs the driver to pick that outfit.
            Object logicData=Json.obj(Json.read(new File(out,"model.json"),4L<<20),"model").get("logic");
            if(mode==0){List<Object> variants=Json.arr(Json.obj(logicData,"logic").get("variants"),"variants",2,2);
                ok(Json.obj(Json.obj(variants.get(0),"v").get("assignment"),"a").equals(Json.of("$swapvar",0L))||Json.obj(Json.obj(variants.get(0),"v").get("assignment"),"a").get("$swapvar").equals(0.0),"first outfit: "+variants.get(0));
                ok(((Number)Json.obj(Json.obj(variants.get(1),"v").get("assignment"),"a").get("$swapvar")).intValue()==4&&Json.obj(Json.obj(variants.get(1),"v").get("draws"),"d").toString().contains("ps-t1=file:otherlightmap.dds"),"the outfit that differs: "+variants.get(1));
                if(nativeChecker!=null){Process check=new ProcessBuilder(nativeChecker,out.getAbsolutePath()).inheritIO().start();ok(check.waitFor()==0,"the native driver picks every converted outfit");}
                // The mod's cycle key is offered to the App, and the App reads the same list back.
                List<Object> keys=Json.arr(Json.obj(logicData,"logic").get("keys"),"keys",1,8);boolean swap=false;for(Object k:keys)swap|=Json.obj(k,"key").get("section").equals("KeySwap")&&Json.obj(k,"key").get("type").equals("cycle");
                ok(swap&&ModelManifest.read(out).keys.size()==keys.size(),"logic keys: "+keys);}
            if(mode==1)ok(logicData==null&&log.toString().contains("用了 TexFx / ORFix 兼容转换"),"ORFix's NNFix convention is not something the logic core can state: "+log);
            for(int state=0;state<5;state++)for(Object raw:Fixtures.draws(out,0,Fixtures.set("$swapvar",state))){
                Map<String,Object> draw=Json.obj(raw,"draw"),m=Fixtures.material(out,(String)draw.get("material"));
                if(mode==0){ok(pixel(out,m,"main")== (state==4?44:22),"legacy IB must select real color across variants");
                    ok(pixel(out,m,"lightmap")==33&&pixel(out,m,"normalmap")==11,"legacy mask/normal roles");}
                else {ok(pixel(out,m,"main")==11&&pixel(out,m,"lightmap")== (state==4?44:22),"explicit NNFix and unknown IB preserve their conventions");
                    ok(!m.containsKey("normalmap"),"no inferred normal on NNFix or unknown layout");}
            }
            ok((mode!=0)||log.toString().contains("优先于文件名"),"layout decision must be explained");
        }
    }
    private static void hohe(File base)throws Exception{
        String key="[KeyWings]\nkey = 6\ntype = cycle\n$wings = 0,1\n";
        String text=GimiPatternsTest.ini("global persist $wings = 1\n","","drawindexed = 3,0,0\n","")
            .replace("drawindexed = auto\n","")
            .replace("ps-t0 = ResourceCitlaliBodyDiffuse\ndrawindexed = 3,0,0\n",
                "ps-t0 = ResourceCitlaliBodyDiffuse\nif $wings == 1\n$\\TexFx\\_1 = 3\n$\\TexFx\\_2 = 3\nrun = CommandList\\TexFx\\Component.1\nendif\n")
            +"\n[Present]\npost $wings = 1\n[Present]\nrun = CommandListMenu\n"+key+key
            +"[CommandListMenu]\nif $unknown_menu_only\nrun = CommandListMouseOnly\nendif\n";
        File archive=new File(base,"wrapper"),src=GimiPatternsTest.mod(archive,"HoHE",text),face=new File(src,"face");face.mkdirs();
        GimiPatternsTest.put(src,"help.ini","[Constants]\nglobal $active = 0\n[Present]\npost $active = 0\n");
        GimiPatternsTest.put(face,"face.ini","[TextureOverrideFace]\nhash = 895cd4d0\nrun = CommandList\\GIMI\\SetTextures\n");
        GimiPatternsTest.put(src,"DISABLED-old.ini",text);
        Files.move(new File(src,"CitlaliTexcoord.buf").toPath(),new File(src,"CitlaliTexCoord.buf").toPath());
        File textures=new File(src,"resources");textures.mkdir();GimiPatternsTest.put(textures,"pigment.png",new byte[]{7});
        GimiPatternsTest.put(src,"Citlali.ini",text.replace("filename = CitlaliHeadDiffuse.dds","filename = Resources/Pigment.png"));
        GimiPatternsTest.put(src,"CitlaliBody.ib",ByteBuffer.allocate(24).order(ByteOrder.LITTLE_ENDIAN).putInt(3).putInt(4).putInt(5).putInt(5).putInt(4).putInt(3).array());
        ok(ModLibrary.modRoot(archive).equals(src)&&ModLibrary.ini(src).getName().equals("Citlali.ini"),"main geometry must win over help and face INIs");
        File out=new File(base,"hohe-out");StringBuilder log=new StringBuilder();
        ModelConverter.convert(src,null,out,10000073,new ModelConverter.Options().variants(true).fallback("Body"),DECODE,log);
        ok(Fixtures.draws(out,0,Fixtures.set("$wings",0)).size()==1&&Fixtures.draws(out,0,Fixtures.set("$wings",1)).size()==2,"TexFx-only component is conditional and retained");
        ok(ModelManifest.read(out).vars.keySet().equals(Collections.singleton("$wings")),"mouse/menu conditions must not create model variants");
        List<Object> on=Fixtures.draws(out,0,Fixtures.set("$wings",1));boolean body=false,color=false;
        for(Object raw:on){Map<String,Object>d=Json.obj(raw,"draw"),m=Fixtures.material(out,(String)d.get("material"));
            if(Fixtures.number(d,"slot")==1){body=true;ok(Fixtures.number(d,"count")==3,"TexFx count");}
            else color|=pixel(out,m,"main")==7;
        }
        ByteBuffer ix=Fixtures.data(out,0,"indices"),positions=Fixtures.data(out,0,"positions");
        ok(body&&color&&positions.getFloat(ix.getInt(12)*12)==5&&positions.getFloat(ix.getInt(16)*12)==4&&positions.getFloat(ix.getInt(20)*12)==3,"TexFx start offset survives vertex compaction; PNG and PC filename casing");
        ok(log.toString().contains("重复 Present")&&log.toString().contains("重复按键")&&log.toString().contains("未复刻 PC 着色器")&&log.toString().contains("face.ini"),"compatibility decisions and limits must be visible");
        // Two actual geometry INIs must never silently pick one, even when both have a face sidecar.
        GimiPatternsTest.put(src,"second.ini",text);boolean refused=false;
        try{ModLibrary.modRoot(archive);}catch(IllegalArgumentException e){refused=e.getMessage().contains("套 INI 方案");}ok(refused,"ambiguous geometry must be refused until a scheme is chosen");
        // Several model INIs are schemes: listed, the choice stored in modelswap.txt, and only that one converted.
        ModLibrary.check(archive);List<String> schemes=Importer.schemes(archive);
        ok(schemes.equals(Arrays.asList("HoHE/Citlali.ini","HoHE/second.ini")),"schemes are the model INIs, not help/face sidecars: "+schemes);
        boolean help=false,escape=false;try{Importer.setScheme(archive,"HoHE/help.ini");}catch(IllegalArgumentException e){help=true;}
        try{Importer.setScheme(archive,"../escape.ini");}catch(IllegalArgumentException e){escape=true;}ok(help&&escape,"only model INIs inside the mod can be chosen");
        Importer.setScheme(archive,"HoHE/second.ini");ok(Importer.scheme(archive).equals("HoHE/second.ini")&&ModLibrary.mainIni(archive).getName().equals("second.ini")&&ModLibrary.modRoot(archive).equals(src),"the stored scheme is used");
        File chosenOut=new File(base,"scheme-out");StringBuilder chosenLog=new StringBuilder();
        ModelConverter.convert(src,null,chosenOut,10000073,new ModelConverter.Options().variants(true).fallback("Body").ini(ModLibrary.mainIni(archive)),DECODE,chosenLog);
        ok(chosenLog.toString().contains("模型主 INI：second.ini")&&chosenLog.toString().contains("未导入辅助 INI：Citlali.ini"),"conversion uses the chosen scheme: "+chosenLog);
        Files.delete(new File(src,"second.ini").toPath());refused=false;try{ModLibrary.mainIni(archive);}catch(IllegalArgumentException e){refused=e.getMessage().contains("已不存在");}ok(refused,"a removed scheme asks again");
        ModLibrary.setOverride(archive,"ini",null);GimiPatternsTest.put(src,"second.ini",text);
        // Conflicting duplicate model sections remain errors; only the known harmless control duplicates are merged.
        Files.delete(new File(src,"second.ini").toPath());GimiPatternsTest.put(src,"Citlali.ini",text+"\n[TextureOverrideCitlaliHead]\nib = ResourceCitlaliHeadIB\n");
        refused=false;try{new ModelConverter.Ini(src);}catch(IllegalArgumentException e){refused=e.getMessage().contains("重复 INI 段");}ok(refused,"do not merge conflicting geometry sections");
    }
    // A Present program the frame-loop pattern does not cover (back and forth), through the logic core's rehearsal
    // (core_test --rehearse in place of NativeBridge.rehearse): frames by the values the draws read, the order
    // without input, and the native check playing it by the logic driver and by the sequence FrameLoop side by side.
    // A counter that later opens a branch is refused with the rehearsal's reason.
    private static void rehearsedAnimation(File base)throws Exception{
        if(nativeChecker==null)return;
        ModelConverter.Rehearsal native_=request->{File req=File.createTempFile("rehearse-",".json",base);Files.write(req.toPath(),request.getBytes("UTF-8"));
            Process p=new ProcessBuilder(nativeChecker,"--rehearse",req.getAbsolutePath()).redirectErrorStream(true).start();java.io.ByteArrayOutputStream bytes=new java.io.ByteArrayOutputStream();byte[] chunk=new byte[65536];for(int n;(n=p.getInputStream().read(chunk))>0;)bytes.write(chunk,0,n);String reply=new String(bytes.toByteArray(),"UTF-8").trim();ok(p.waitFor()==0,"rehearsal tool: "+reply);return reply;};
        String program="[Present]\n$frame = $frame + $dir\nif $frame >= 2\n$dir = -1\nendif\nif $frame <= 0\n$dir = 1\nendif\n";
        String text=GimiPatternsTest.ini("global $frame = 0\nglobal $dir = 1\n","if $frame == 1\nvb0 = ResourceNextPosition\nendif\n","","")+"[ResourceNextPosition]\nstride = 40\nfilename = Next.buf\n"+program;
        File src=GimiPatternsTest.mod(base,"rehearsed",text),out=new File(base,"rehearsed-out");byte[] next=Files.readAllBytes(new File(src,"CitlaliPosition.buf").toPath());ByteBuffer.wrap(next).order(ByteOrder.LITTLE_ENDIAN).putFloat(0,99);GimiPatternsTest.put(src,"Next.buf",next);
        boolean refused=false;try{ModelConverter.convert(src,null,new File(base,"rehearsed-none"),10000107,new ModelConverter.Options().variants(true),DECODE,new StringBuilder());}catch(IllegalArgumentException e){refused=e.getMessage().contains("暂不支持此 Present 动画程序");}
        ok(refused,"without the rehearsal the program is refused as before");
        StringBuilder log=new StringBuilder();ModelConverter.convert(src,null,out,10000107,new ModelConverter.Options().variants(true).rehearsal(native_),DECODE,log);
        Map<String,Object> anim=Json.obj(Json.obj(Json.read(new File(out,"model.json"),4L<<20),"model").get("animation"),"animation");
        List<Integer> sequence=new ArrayList<>();for(Object x:Json.arr(anim.get("sequence"),"sequence",1,16))sequence.add(((Number)x).intValue());
        ok("present-sequence".equals(anim.get("kind"))&&sequence.equals(Arrays.asList(0,1,2,1))&&((Number)anim.get("loop")).intValue()==0&&Json.arr(anim.get("frames"),"frames",3,3).size()==3,"rehearsed frames and order: "+anim.get("kind")+" "+sequence+" loop "+anim.get("loop"));
        ok(log.toString().contains("由逻辑核心预演 Present 程序得出")&&log.toString().contains("未采用取余循环的原因"),"the conversion log says where the frames come from: "+log);
        ok(Json.arr(Json.obj(Json.obj(Json.read(new File(out,"model.json"),4L<<20),"model").get("logic"),"logic").get("frames"),"logic frames",3,3).size()==3,"logic data for every rehearsed frame");
        Process check=new ProcessBuilder(nativeChecker,out.getAbsolutePath()).inheritIO().start();ok(check.waitFor()==0,"the native loader reads the sequence and the logic driver follows it");
        GimiPatternsTest.put(src,"Citlali.ini",text.replace(program,"[Present]\n$t = $t + 1\nif $t > 100\n$frame = 1\nendif\n").replace("global $dir = 1\n","global $dir = 1\nglobal $t = 0\n"));refused=false;
        try{ModelConverter.convert(src,null,new File(base,"rehearsed-counter"),10000107,new ModelConverter.Options().variants(true).rehearsal(native_),DECODE,new StringBuilder());}catch(IllegalArgumentException e){refused=e.getMessage().contains("逻辑核心预演也无法确定")&&e.getMessage().contains("没有重复");}
        ok(refused,"a counter that opens a branch later is refused, with the rehearsal's reason");
    }
    static String runNative(String flag,String request,File base)throws Exception{File req=File.createTempFile("native-",".json",base);Files.write(req.toPath(),request.getBytes("UTF-8"));
        Process p=new ProcessBuilder(nativeChecker,flag,req.getAbsolutePath()).redirectErrorStream(true).start();java.io.ByteArrayOutputStream bytes=new java.io.ByteArrayOutputStream();byte[] chunk=new byte[65536];
        for(int n;(n=p.getInputStream().read(chunk))>0;)bytes.write(chunk,0,n);String reply=new String(bytes.toByteArray(),"UTF-8").trim();ok(p.waitFor()==0,"native tool "+flag+": "+reply);return reply;}
    // The 3DMigoto backend: the logic core runs the INI and its draws are converted (a static model, an outfit model, a
    // frame animation). A standard GIMI
    // mod gives the same model as the converter's own evaluation; with an IB-level drawindexed = auto and a part's own
    // draw, 3DMigoto draws both for that part (the old evaluation only the part's own), and the log names it; a core
    // that cannot run leaves the converter's own evaluation, with the reason.
    private static void staticBackend(File base)throws Exception{
        if(nativeChecker==null)return;ModelConverter.CoreDraws core=r->runNative("--draws",r,base);
        String text=GimiPatternsTest.ini("global $x = 0\n","","","");File src=GimiPatternsTest.mod(base,"backend",text);
        StringBuilder plain=new StringBuilder(),log=new StringBuilder();File a=new File(base,"backend-plain"),b=new File(base,"backend-core");
        ModelConverter.convert(src,null,a,10000107,new ModelConverter.Options(),DECODE,plain);ModelConverter.convert(src,null,b,10000107,new ModelConverter.Options().core(core),DECODE,log);
        Object pa=Json.obj(Json.read(new File(a,"model.json"),4L<<20),"m").get("parts"),pb=Json.obj(Json.read(new File(b,"model.json"),4L<<20),"m").get("parts");
        ok(log.toString().contains("3dm 后端：由逻辑核心按 XXMI 的方式运行 INI 决定绘制（2 次），与转换器自己的求值一致")&&!plain.toString().contains("3dm 后端"),"the backend's draws agree on a standard mod: "+log);
        ok(String.valueOf(pa).equals(String.valueOf(pb))&&Arrays.equals(Files.readAllBytes(new File(a,"model.bin").toPath()),Files.readAllBytes(new File(b,"model.bin").toPath())),"and the converted model is the same, byte for byte");
        GimiPatternsTest.put(src,"Citlali.ini",text.replace("ps-t0 = ResourceCitlaliBodyDiffuse\n","ps-t0 = ResourceCitlaliBodyDiffuse\ndrawindexed = 3, 0, 0\n"));
        GimiPatternsTest.put(src,"CitlaliBody.ib",ByteBuffer.allocate(24).order(ByteOrder.LITTLE_ENDIAN).putInt(3).putInt(4).putInt(5).putInt(0).putInt(1).putInt(2).array());   // its own draw: the first 3; auto: all 6
        StringBuilder both=new StringBuilder(),old=new StringBuilder();File c=new File(base,"backend-both"),d=new File(base,"backend-old");
        ModelConverter.convert(src,null,c,10000107,new ModelConverter.Options().core(core),DECODE,both);ModelConverter.convert(src,null,d,10000107,new ModelConverter.Options(),DECODE,old);
        long ic=0,id=0;for(Object x:Json.arr(Json.obj(Json.read(new File(c,"model.json"),4L<<20),"m").get("parts"),"parts",1,4))ic+=Fixtures.number(Json.obj(x,"p"),"indices");
        for(Object x:Json.arr(Json.obj(Json.read(new File(d,"model.json"),4L<<20),"m").get("parts"),"parts",1,4))id+=Fixtures.number(Json.obj(x,"p"),"indices");
        ok(both.toString().contains("与转换器自己的求值不同，按 3dm 后端转换")&&both.toString().contains("3dm 后端有、旧求值没有：CitlaliBody count=auto start=0 ib=CitlaliBody.ib")&&ic==id+6,"the IB-level auto draw is drawn for the part too, as on PC: "+ic+" vs "+id+"\n"+both);
        // An outfit model (every outfit) and a frame animation (every frame): the same model either way, and the
        // logic data the game side matches against.
        String outfit=GimiPatternsTest.ini("global $look = 0\n","if $look == 1\nvb0 = ResourceNextPosition\nendif\n","","")+"[ResourceNextPosition]\nstride = 40\nfilename = Next.buf\n[KeyLook]\nkey = 1\ntype = cycle\n$look = 0,1\n";
        String program="[Present]\nif $counter % $speed == 0\nif $frame < $last\n$frame = $frame + 1\nelse\n$frame = $first\nendif\nendif\npost $counter = $counter + 1\n";
        String animated=GimiPatternsTest.ini("global $speed = 0.5\nglobal $counter = 0\nglobal $frame = 0\nglobal $first = 0\nglobal $last = 2\n","if $frame == 1\nvb0 = ResourceNextPosition\nendif\n","","")+"[ResourceNextPosition]\nstride = 40\nfilename = Next.buf\n"+program;
        for(String[] k:new String[][]{{"backend-outfit",outfit,"2 次"},{"backend-animation",animated,"6 次"}}){File m=GimiPatternsTest.mod(base,k[0],k[1]);byte[] next=Files.readAllBytes(new File(m,"CitlaliPosition.buf").toPath());ByteBuffer.wrap(next).order(ByteOrder.LITTLE_ENDIAN).putFloat(0,99);GimiPatternsTest.put(m,"Next.buf",next);
            File x=new File(base,k[0]+"-plain"),y=new File(base,k[0]+"-core");StringBuilder lx=new StringBuilder(),ly=new StringBuilder();
            ModelConverter.convert(m,null,x,10000107,new ModelConverter.Options().variants(true),DECODE,lx);ModelConverter.convert(m,null,y,10000107,new ModelConverter.Options().variants(true).core(core),DECODE,ly);
            ok(ly.toString().contains("与转换器自己的求值一致")&&Arrays.equals(Files.readAllBytes(new File(x,"model.bin").toPath()),Files.readAllBytes(new File(y,"model.bin").toPath())),k[0]+": the same model through the backend: "+ly);
            Map<String,Object> jx=Json.obj(Json.read(new File(x,"model.json"),4L<<20),"m"),jy=Json.obj(Json.read(new File(y,"model.json"),4L<<20),"m");
            ok(String.valueOf(jx.get("parts")).equals(String.valueOf(jy.get("parts")))&&String.valueOf(jx.get("logic")).equals(String.valueOf(jy.get("logic")))&&jy.get("logic")!=null,k[0]+": parts and logic data the same");
            Process check=new ProcessBuilder(nativeChecker,y.getAbsolutePath()).inheritIO().start();ok(check.waitFor()==0,k[0]+": the native driver follows it");}
        // A texture-only mod (XXMI's GIMI SetTextures on the game's own draws, no IB or vertex buffer of its own): said
        // so, the main INI not listed as an auxiliary one, and the backend not asked to run draws there are none of.
        File tex=GimiPatternsTest.mod(base,"texture-only","[Constants]\nglobal $active\n[TextureOverrideBodyA]\nhash = d5ae71a3\nmatch_first_index = 0\nResource\\GIMI\\Diffuse = ref ResourceBodyDiffuse\nrun = CommandList\\GIMI\\SetTextures\n[TextureOverrideEyes]\nhash = 848c5980\nps-t1 = ResourceBodyDiffuse\n[ResourceBodyDiffuse]\nfilename = CitlaliHeadDiffuse.dds\n");
        StringBuilder tl=new StringBuilder();String why="";try{ModelConverter.convert(tex,null,new File(base,"texture-only-out"),10000107,new ModelConverter.Options().variants(true).core(core),DECODE,tl);}catch(IllegalArgumentException e){why=e.getMessage();}
        ok(why.contains("是纯贴图 MOD")&&!tl.toString().contains("未导入辅助 INI：Citlali.ini")&&tl.toString().contains("3dm 后端未用于本次转换：INI 没有模型绘制"),"a texture-only mod is named as one: "+why+"\n"+tl);
        // With a diagnostic folder (the App's GIBox/texture-diag): the INI's rules written for the game side, read-only;
        // a later conversion under that name that is not texture-only takes the stale diagnostic away.
        File diagDir=new File(base,"texture-diag");StringBuilder dl=new StringBuilder();String dwhy="";
        try{ModelConverter.convert(tex,null,new File(base,"texture-only-diag"),10000107,new ModelConverter.Options().variants(true).textureDiag(diagDir),DECODE,dl);}catch(IllegalArgumentException e){dwhy=e.getMessage();}
        File diag=new File(diagDir,"texture-only-diag.json");Map<String,Object> dj=diag.isFile()?Json.obj(Json.read(diag,4L<<20),"diag"):new LinkedHashMap<>();
        List<Object> drules=dj.containsKey("rules")?Json.arr(dj.get("rules"),"rules",0,100):new ArrayList<>();
        ok(dwhy.contains("是纯贴图 MOD")&&dwhy.contains("已写出只读诊断 texture-diag/texture-only-diag.json")&&"gibox-texture-diag".equals(dj.get("format"))&&drules.size()==2
            &&String.valueOf(drules.get(0)).contains("first=0")&&String.valueOf(drules.get(0)).contains("kind=segment")&&String.valueOf(drules.get(0)).contains("role=diffuse")&&String.valueOf(drules.get(0)).contains("external=[CommandList\\GIMI\\SetTextures]")
            &&String.valueOf(drules.get(1)).contains("kind=hash-unknown")&&String.valueOf(drules.get(1)).contains("slot=ps-t1")&&dl.toString().contains("texture-diag-ini TextureOverrideBodyA: hash=d5ae71a3 match_first_index=0 kind=segment")
            &&!new File(base,"texture-only-diag").exists(),"a texture-only mod gets its read-only diagnostic: "+dwhy+"\n"+dl+"\n"+dj);
        ModelConverter.convert(src,null,new File(base,"texture-only-diag"),10000107,new ModelConverter.Options().textureDiag(diagDir),DECODE,new StringBuilder());
        ok(!diag.exists(),"a mod that converts leaves no texture diagnostic of that name");
        // As the App imports it (0.7.52): a texture-only model folder (texture.json + decoded textures, no mesh), with the
        // rules, the variants and the decoded textures; the App reads it as a converted model; no stale diagnostic.
        File texVar=GimiPatternsTest.mod(base,"texture-mod","[Constants]\nglobal persist $colour = 0\n[KeySwap]\nkey = 0\ntype = cycle\n$colour = 0,1\n[TextureOverrideBodyA]\nhash = d5ae71a3\nmatch_first_index = 0\nif $colour == 0\nResource\\GIMI\\Diffuse = ref ResourceBodyDiffuse\nendif\nrun = CommandList\\GIMI\\SetTextures\n[TextureOverrideEyes]\nhash = 848c5980\nps-t1 = ResourceBodyDiffuse\n[ResourceBodyDiffuse]\nfilename = CitlaliHeadDiffuse.dds\n");
        File texOut=new File(base,"texture-mod-out");StringBuilder ml=new StringBuilder();TextureDiag.write(diagDir,"texture-mod-out",new LinkedHashMap<>());
        ModelConverter.convert(texVar,null,texOut,10000107,new ModelConverter.Options().variants(true).textureDiag(diagDir).textureMods(true),DECODE,ml);
        Map<String,Object> tj=Json.obj(Json.read(new File(texOut,"texture.json"),4L<<20),"texture.json");ModelManifest mm=ModelManifest.read(texOut);
        ok("gibox-texture-mod".equals(tj.get("format"))&&!new File(texOut,"model.json").exists()&&new File(texOut,"textures/0.rgba").length()==4&&ModelManifest.exists(texOut)&&mm.textureMod&&mm.id==10000107
            &&mm.vars.get("$colour").equals(Arrays.asList(0,1))&&mm.defaults.get("$colour")==0&&Variants.read(texOut).values.containsKey("$colour")&&!TextureDiag.file(diagDir,"texture-mod-out").exists()
            &&String.valueOf(tj.get("diag")).contains("TextureOverrideBodyA")&&ml.toString().contains("纯贴图 MOD：2 / 2 条规则有可写入的贴图")&&ml.toString().contains("完成："),"a texture-only mod imports as one: "+ml+"\n"+tj);
        String noUse="";try{ModelConverter.convert(GimiPatternsTest.mod(base,"texture-ramp","[TextureOverrideX]\nhash = 1\nps-t3 = ResourceRamp\n[ResourceRamp]\nfilename = Ramp.dds\n"),null,new File(base,"texture-ramp-out"),10000107,new ModelConverter.Options().textureMods(true),DECODE,new StringBuilder());}catch(IllegalArgumentException e){noUse=e.getMessage();}
        ok(noUse.contains("没有能写入的贴图")&&!new File(base,"texture-ramp-out").exists(),"nothing writable: refused, no folder: "+noUse);
        StringBuilder failed=new StringBuilder();ModelConverter.convert(src,null,new File(base,"backend-failed"),10000107,new ModelConverter.Options().core(r->"{\"ok\":false,\"reason\":\"no core\"}"),DECODE,failed);
        ok(failed.toString().contains("3dm 后端未用于本次转换：逻辑核心无法运行此 INI：no core；按转换器自己的求值"),"a core that cannot run leaves the converter's own evaluation: "+failed);
    }
    // A program that follows the clock (NahidaPoS's Qswap: Q shows the locked outfit for a while, ] / [ select):
    // no order to rehearse, so every outfit its draws can show is converted and the logic data is "driven"; without
    // the rehearsal it is refused as before. Its cycle key with back = is listed with "back".
    // A scheme shipped switched off (disabled/Q.ini, paths relative to the mod folder as on PC once moved up) is
    // offered beside the one XXMI loads (not a DISABLED... file, not a copy), converted from the folder it would be switched on in, and its logic data
    // leaves the other scheme out.
    private static void drivenOutfits(File base)throws Exception{
        if(nativeChecker==null)return;
        ModelConverter.Rehearsal native_=request->{File req=File.createTempFile("rehearse-",".json",base);Files.write(req.toPath(),request.getBytes("UTF-8"));
            Process p=new ProcessBuilder(nativeChecker,"--rehearse",req.getAbsolutePath()).redirectErrorStream(true).start();java.io.ByteArrayOutputStream bytes=new java.io.ByteArrayOutputStream();byte[] chunk=new byte[65536];for(int n;(n=p.getInputStream().read(chunk))>0;)bytes.write(chunk,0,n);String reply=new String(bytes.toByteArray(),"UTF-8").trim();ok(p.waitFor()==0,"rehearsal tool: "+reply);return reply;};
        String keys="[KeySel]\nkey = ]\nback = [\ntype = cycle\n$sel = 0,1\n[KeyQ]\nkey = q\ntype = cycle\n$press = 1\n";
        String program="[Present]\nif $press == 1\n$start = time\n$press = 0\nendif\nif time - $start < 3\n$look = 1\nelse\n$look = $sel\nendif\n";
        String text=GimiPatternsTest.ini("global $look = 0\nglobal persist $sel = 0\nglobal $press = 0\nglobal $start = -100\n","if $look == 1\nvb0 = ResourceNextPosition\nendif\n","","")+"[ResourceNextPosition]\nstride = 40\nfilename = Next.buf\n"+keys+program;
        File src=GimiPatternsTest.mod(base,"driven",text);byte[] next=Files.readAllBytes(new File(src,"CitlaliPosition.buf").toPath());ByteBuffer.wrap(next).order(ByteOrder.LITTLE_ENDIAN).putFloat(0,99);GimiPatternsTest.put(src,"Next.buf",next);
        boolean refused=false;try{ModelConverter.convert(src,null,new File(base,"driven-none"),10000107,new ModelConverter.Options().variants(true),DECODE,new StringBuilder());}catch(IllegalArgumentException e){refused=e.getMessage().contains("暂不支持此 Present 动画程序");}
        ok(refused,"without the rehearsal a program that follows the clock is refused as before");
        StringBuilder log=new StringBuilder();File out=new File(base,"driven-out");ModelConverter.convert(src,null,out,10000107,new ModelConverter.Options().variants(true).rehearsal(native_),DECODE,log);
        Map<String,Object> manifest=Json.obj(Json.read(new File(out,"model.json"),4L<<20),"model"),logic=Json.obj(manifest.get("logic"),"logic");ModelManifest read=ModelManifest.read(out);
        ok(read.vars.get("$look").equals(Arrays.asList(0,1))&&Boolean.TRUE.equals(logic.get("driven"))&&read.driven&&Json.arr(logic.get("variants"),"variants",2,2).size()==2,"every outfit the draws can show, driven: "+read.vars+" "+logic.get("driven"));
        boolean back=false;for(ModelManifest.Key k:read.keys)back|=k.section.equals("KeySel")&&k.back;ok(back,"the cycle key with back = is listed with back");
        ok(log.toString().contains("Present 程序跟随游戏")&&log.toString().contains("读取了 time"),"the conversion log says why and what the core does: "+log);
        Process check=new ProcessBuilder(nativeChecker,out.getAbsolutePath()).inheritIO().start();ok(check.waitFor()==0,"the native driver picks every converted outfit of a driven model");
        // Switched-off scheme.
        File off=new File(src,"disabled");off.mkdirs();GimiPatternsTest.put(off,"Q.ini",text.replace("global $start = -100\n","global $start = -100\nglobal $only_q = 1\n"));
        GimiPatternsTest.put(off,"Q copy.ini",text);   // an exact copy of the enabled one: not a scheme
        GimiPatternsTest.put(src,"DISABLED old.ini",text.replace("global $look = 0\n","global $look = 0\nglobal $old = 1\n"));   // a switched-off file: a backup, not a scheme
        File q=new File(off,"Q.ini");List<String> schemes=ModLibrary.schemes(src);
        ok(schemes.equals(Arrays.asList("Citlali.ini","disabled/Q.ini"))&&Importer.scheme(src).equals("Citlali.ini"),"the switched-off scheme is offered, the loaded one stays in effect: "+schemes);
        ok(ModConfig.switchedOn(src,q).getCanonicalFile().equals(src.getCanonicalFile())&&ModConfig.switchedOn(src,new File(src,"DISABLED old.ini")).getCanonicalFile().equals(src.getCanonicalFile()),"a switched-off scheme is read from where it is switched on");
        File outQ=new File(base,"driven-q");ModelConverter.convert(src,null,outQ,10000107,new ModelConverter.Options().variants(true).rehearsal(native_).ini(q),DECODE,new StringBuilder());
        List<Object> files=Json.arr(Json.obj(Json.obj(Json.read(new File(outQ,"model.json"),4L<<20),"model").get("logic"),"logic").get("files"),"files",1,1);
        ok(Json.obj(files.get(0),"file").get("namespace").equals("Mods\\driven\\Q.ini")&&new String(Files.readAllBytes(new File(outQ,"logic/0.ini").toPath()),"UTF-8").contains("$only_q"),"the logic data is the chosen scheme only: "+files);
    }
    private static void animation(File base)throws Exception{
        String program="[Present]\nif $counter % $speed == 0\nif $frame < $last\n$frame = $frame + 1\nelse\n$frame = $first\nendif\nendif\npost $counter = $counter + 1\n";
        String text=GimiPatternsTest.ini("global $speed = 0.5\nglobal $counter = 0\nglobal $frame = 0\nglobal $first = 0\nglobal $last = 2\n",
            "if $frame == 1\nvb0 = ResourceNextPosition\nendif\n","","")+"[ResourceNextPosition]\nstride = 40\nfilename = Next.buf\n"+program;
        File src=GimiPatternsTest.mod(base,"animation",text),out=new File(base,"animation-out");byte[] next=Files.readAllBytes(new File(src,"CitlaliPosition.buf").toPath());ByteBuffer.wrap(next).order(ByteOrder.LITTLE_ENDIAN).putFloat(0,99);GimiPatternsTest.put(src,"Next.buf",next);
        GimiPatternsTest.put(src,"DISABLED old.ini",text);   // XXMI skips it, so the logic data must too
        StringBuilder log=new StringBuilder();ModelConverter.convert(src,null,out,10000107,new ModelConverter.Options().variants(true),DECODE,log);
        Map<String,Object> manifest=Json.obj(Json.read(new File(out,"model.json"),4L<<20),"model");List<Object> frames=Json.arr(Json.obj(manifest.get("animation"),"animation").get("frames"),"frames",3,3);
        // The logic core's data: the INI as XXMI names it, the draws replayed (skinning pass, then each part with its
        // first index), where each field is read, and per frame what each draw used.
        Map<String,Object> logic=Json.obj(manifest.get("logic"),"logic");Map<String,Object> file=Json.obj(Json.arr(logic.get("files"),"files",1,1).get(0),"file");
        ok("Mods\\animation\\Citlali.ini".equals(file.get("namespace"))&&"logic/0.ini".equals(file.get("file"))&&new String(Files.readAllBytes(new File(out,"logic/0.ini").toPath()),"UTF-8").equals(text),"the mod's INI is shipped as XXMI loads it: "+file);
        List<Object> passes=Json.arr(logic.get("passes"),"passes",3,3);ok("skin".equals(Json.obj(passes.get(0),"pass").get("name"))&&Json.obj(Json.obj(passes.get(0),"pass").get("bindings"),"b").equals(Json.of("vb0","11111111","vb1","22222222")),"the skinning pass: "+passes.get(0));
        Map<String,Object> body=Json.obj(passes.get(2),"pass");ok("CitlaliBody".equals(body.get("name"))&&((Number)Json.obj(body.get("call"),"call").get("first_index")).doubleValue()==3&&Json.obj(body.get("bindings"),"b").equals(Json.of("ib","44444444","vb1","33333333")),"a part draw: "+body);
        Map<String,Object> reads=Json.obj(Json.obj(logic.get("fields"),"fields").get("CitlaliHead"),"head");ok(reads.get("position").equals(Arrays.asList("skin","vb0"))&&reads.get("texcoord").equals(Arrays.asList("CitlaliHead","vb1")),"fields are read where GIMI binds them: "+reads);
        List<Object> records=Json.arr(logic.get("frames"),"frames",3,3);String head1=(String)Json.obj(Json.obj(records.get(1),"frame").get("draws"),"draws").get("CitlaliHead");
        String head="file:citlalihead.ib,type=buffer,format=dxgi_format_r32_uint",tex="file:citlalitexcoord.buf,type=buffer,stride=20",blend="file:citlaliblend.buf,type=buffer,stride=32";
        ok(head1.equals("kind=drawindexed;count=auto;start=0;base=0;ib="+head+";position=file:next.buf,stride=40;blend="+blend+";texcoord="+tex+";ps-t0=file:citlaliheaddiffuse.dds;ps-t1=-;ps-t2=-;|"),"frame 1 records its own position buffer: "+head1);
        ok(Json.obj(logic.get("resources"),"resources").get("resource\\mods\\animation\\citlali.ini\\citlaliheadib").equals(head)&&Integer.valueOf(2).equals(((Number)logic.get("signature")).intValue()),"resources by the core's key and identity: "+logic.get("resources"));
        ok(log.toString().contains("逻辑核心数据：1 个 INI、8 个资源、3 次绘制、3 帧"),"the conversion log names the logic data: "+log);
        if(nativeChecker!=null){Process check=new ProcessBuilder(nativeChecker,out.getAbsolutePath()).inheritIO().start();ok(check.waitFor()==0,"the native loader reads the logic data and its driver follows FrameLoop");}
        Files.delete(new File(src,"DISABLED old.ini").toPath());
        ok(ModelManifest.read(out).version==4&&ModelManifest.read(out).vars.isEmpty()&&log.toString().contains("完整转换"),"animation must not become a variant union");
        ok(log.toString().contains("动画骨骼要求：Part0 / Body，无骨骼名映射，按索引绑定至少 ")&&log.toString().contains("转换成功不代表已通过游戏骨骼匹配"),"conversion must disclose the indexed skeleton requirement before device playback");
        Map<String,Object> part=Json.obj(Json.arr(Json.obj(frames.get(1),"frame").get("parts"),"parts",1,64).get(0),"part");
        ok(Fixtures.number(part,"vertices")==6&&Fixtures.view(out,Json.obj(part.get("data"),"data").get("positions")).getFloat(0)==99,"frame-local geometry is preserved");
        ok(Boolean.TRUE.equals(Json.obj(manifest.get("animation"),"animation").get("stableWeights")),"the dynamic mode must have an explicit immutable-weight guarantee");
        // Rotating triangles changes first-use vertex order. A weight permutation must move all attributes and indices together.
        String permuted=text.replace("if $frame == 1\nvb0 = ResourceNextPosition\nendif\n", "if $frame == 1\nvb0 = ResourceNextPosition\nendif\n")
            .replace("ib = ResourceCitlaliHeadIB\n","if $frame == 1\nib = ResourceNextHead\nelse\nib = ResourceCitlaliHeadIB\nendif\n")
            .replace("ib = ResourceCitlaliBodyIB\n","if $frame == 1\nib = ResourceNextBody\nelse\nib = ResourceCitlaliBodyIB\nendif\n")
            +"[ResourceNextHead]\nformat = R32_UINT\nfilename = NextHead.ib\n[ResourceNextBody]\nformat = R32_UINT\nfilename = NextBody.ib\n";
        ByteBuffer skin=ByteBuffer.wrap(Files.readAllBytes(new File(src,"CitlaliBlend.buf").toPath())).order(ByteOrder.LITTLE_ENDIAN);for(int i=0;i<6;i++)skin.putInt(i*32+16,i%2);
        GimiPatternsTest.put(src,"CitlaliBlend.buf",skin.array());GimiPatternsTest.put(src,"NextHead.ib",ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN).putInt(1).putInt(2).putInt(0).array());
        GimiPatternsTest.put(src,"NextBody.ib",ByteBuffer.allocate(12).order(ByteOrder.LITTLE_ENDIAN).putInt(4).putInt(5).putInt(3).array());GimiPatternsTest.put(src,"Citlali.ini",permuted);
        File aligned=new File(base,"aligned-animation");ModelConverter.convert(src,null,aligned,10000107,new ModelConverter.Options().variants(true),DECODE,new StringBuilder());
        Map<String,Object> alignedManifest=Json.obj(Json.read(new File(aligned,"model.json"),4L<<20),"model");List<Object> alignedFrames=Json.arr(Json.obj(alignedManifest.get("animation"),"animation").get("frames"),"frames",3,3);
        byte[] initialWeights=Fixtures.data(aligned,0,"weights").array();Map<String,Object> alignedPart=Json.obj(Json.arr(Json.obj(alignedFrames.get(1),"frame").get("parts"),"parts",1,1).get(0),"part"),data=Json.obj(alignedPart.get("data"),"data");
        ByteBuffer weights=Fixtures.view(aligned,data.get("weights")),positions=Fixtures.view(aligned,data.get("positions")),indices=Fixtures.view(aligned,data.get("indices"));
        ok(Arrays.equals(initialWeights,weights.array())&&positions.getFloat(indices.getInt(0)*12)==1&&positions.getFloat(indices.getInt(4)*12)==2&&positions.getFloat(indices.getInt(8)*12)==99,"weight order must be fixed without changing the drawn triangles");
        String generation=ModelManifest.read(out).generation;GimiPatternsTest.put(src,"Citlali.ini",text.replace("post $counter = $counter + 1","post $counter = $counter + 1\n$frame = 0"));boolean rejected=false;
        try{ModelConverter.convert(src,null,out,10000107,new ModelConverter.Options().variants(true).replace(true),DECODE,new StringBuilder());}catch(IllegalArgumentException e){rejected=e.getMessage().contains("Present 动画程序");}
        ok(rejected&&generation.equals(ModelManifest.read(out).generation),"unsupported programs fail before replacing a working conversion");
        GimiPatternsTest.put(src,"Citlali.ini",text);File models=new File(base,"animation-models");models.mkdir();
        Importer.Result result=Importer.convert(src,10000107,models,new File(base,"no-skeleton"),DECODE,new StringBuilder());ok(result.summary.contains("模型动画：3 帧"),"the import dialog must explain full animation conversion");
        // The additional blend groups of a baked animation are NOT additional observed game bones. Strict remains
        // the default. A per-mod opt-in can retain supported associations, with provenance and no geometry loss.
        File library=new File(base,"animation-library/10000107");library.mkdirs();Fixtures.skeleton(library,10000107,"Scene/Avatar_Girl_Catalyst_Citlali",new String[]{"Body"},new String[]{"BoneA","BoneB"},Collections.singletonMap("Body",Fixtures.target("Body",6,2,2)));
        for(int i=0;i<6;i++){skin.putFloat(i*32,1).putFloat(i*32+4,1).putInt(i*32+16,119).putInt(i*32+20,i%2);}GimiPatternsTest.put(src,"CitlaliBlend.buf",skin.array());
        rejected=false;try{ModelConverter.convert(src,library,out,10000107,new ModelConverter.Options().replace(true),DECODE,new StringBuilder());}
        catch(IllegalArgumentException e){rejected=e.getMessage().contains("骨骼索引越界")&&e.getMessage().contains("6 个顶点里 6 个引用骨骼库外的骨骼（编号 119..119，库内 0..1）；其中 6 个去掉这些关联后其余权重总和仍为 1，0 个")&&e.getMessage().contains("忽略越界骨骼关联");}
        ok(rejected&&generation.equals(ModelManifest.read(out).generation),"strict animated import must retain the previous working model, and say how much is outside and where the experiment is");
        rejected=false;try{ModelConverter.convert(src,null,out,10000107,new ModelConverter.Options().replace(true).animationBoneFallback(true),DECODE,new StringBuilder());}catch(IllegalArgumentException e){rejected=e.getMessage().contains("需要完整骨骼库");}
        ok(rejected&&generation.equals(ModelManifest.read(out).generation),"fallback cannot guess a palette's valid boundary");
        byte[] originalBlend=skin.array().clone();StringBuilder fallbackLog=new StringBuilder();ModelConverter.convert(src,library,out,10000107,new ModelConverter.Options().replace(true).animationBoneFallback(true),DECODE,fallbackLog);
        Map<String,Object> fallbackManifest=Json.obj(Json.read(new File(out,"model.json"),4L<<20),"model"),fallbackAnimation=Json.obj(fallbackManifest.get("animation"),"animation");
        ok("retain-valid-experimental".equals(fallbackAnimation.get("bonePolicy"))&&fallbackLog.toString().contains("rawWeights=[1.0, 1.0")&&fallbackLog.toString().contains("animation-bone-fallback 帧 2，部件 "),"lossy policy and original associations must be disclosed");
        for(Object fr:Json.arr(fallbackAnimation.get("frames"),"frames",3,3)){Map<String,Object> p=Json.obj(Json.arr(Json.obj(fr,"frame").get("parts"),"parts",1,1).get(0),"part");ByteBuffer w=Fixtures.view(out,Json.obj(p.get("data"),"data").get("weights"));
            ok(Fixtures.number(p,"vertices")==6&&Fixtures.number(p,"indices")==6,"fallback never removes new geometry");for(int i=0;i<6;i++)ok(w.getFloat(32*i)==1&&w.getInt(32*i+16)<2&&w.getFloat(32*i+4)==0,"each vertex keeps its own supported bone");}
        ok(Arrays.equals(originalBlend,Files.readAllBytes(new File(src,"CitlaliBlend.buf").toPath())),"source blend data remains untouched");
        String compatibleGeneration=ModelManifest.read(out).generation;skin.putFloat(4,0);GimiPatternsTest.put(src,"CitlaliBlend.buf",skin.array());rejected=false;
        try{ModelConverter.convert(src,library,out,10000107,new ModelConverter.Options().replace(true).animationBoneFallback(true),DECODE,new StringBuilder());}catch(IllegalArgumentException e){rejected=e.getMessage().contains("有效权重全部引用库外骨骼");}
        ok(rejected&&compatibleGeneration.equals(ModelManifest.read(out).generation),"all-extra vertices must fail, never borrow a neighbour's weight");
        GimiPatternsTest.put(src,"Citlali.ini",text+"\n[CustomShaderPose]\nvs = Pose.hlsl\n");rejected=false;try{ModelConverter.convert(src,library,out,10000107,new ModelConverter.Options().replace(true).animationBoneFallback(true),DECODE,new StringBuilder());}catch(IllegalArgumentException e){rejected=e.getMessage().contains("自定义着色器");}ok(rejected,"a custom shader requires its own semantics instead of this fallback");
        GimiPatternsTest.put(src,"Citlali.ini",text+"\n[CommandListPose]\nvs-t10 = ResourcePose\n");rejected=false;try{ModelConverter.convert(src,library,out,10000107,new ModelConverter.Options().replace(true).animationBoneFallback(true),DECODE,new StringBuilder());}catch(IllegalArgumentException e){rejected=e.getMessage().contains("自定义矩阵资源");}ok(rejected,"matrix resources cannot be ignored");
        GimiPatternsTest.put(src,"CitlaliBlend.buf",originalBlend);GimiPatternsTest.put(src,"Citlali.ini",text.replace(program,""));rejected=false;
        try{ModelConverter.convert(src,library,new File(base,"static-fallback-refused"),10000107,new ModelConverter.Options().animationBoneFallback(true),DECODE,new StringBuilder());}catch(IllegalArgumentException e){rejected=e.getMessage().contains("骨骼索引越界");}
        ok(rejected,"ordinary models keep strict validation even when the per-mod experiment is enabled");GimiPatternsTest.put(src,"Citlali.ini",text);
        File libraryRoot=library.getParentFile();ok(!Importer.animationBoneFallback(src),"per-mod fallback starts off");Importer.setAnimationBoneFallback(src,true);ok(Importer.animationBoneFallback(src),"per-mod option is read back");
        result=Importer.convert(src,10000107,models,libraryRoot,DECODE,new StringBuilder());ok(result.summary.contains("动画骨骼兼容：开启（实验）"),"import summary must disclose the experiment");
        Importer.setAnimationBoneFallback(src,false);rejected=false;try{Importer.convert(src,10000107,models,libraryRoot,DECODE,new StringBuilder());}catch(IllegalArgumentException e){rejected=e.getMessage().contains("骨骼索引越界");}ok(rejected&&!Importer.animationBoneFallback(src),"disabling restores strict conversion");
    }
    // A texture-only face override (GIMI FaceHeadDiffuse) becomes a material and a texture swap onto the game's Face renderer.
    // One that changes with a value only the face section reads is left out and said so; two face files at once are refused.
    private static void face(File base)throws Exception{
        String face="[TextureOverrideCitlaliFaceHeadDiffuse]\nhash = 55555555\nps-t0 = ResourceCitlaliFaceHeadDiffuse\n[ResourceCitlaliFaceHeadDiffuse]\nfilename = CitlaliFaceHeadDiffuse.dds\n";
        String text=GimiPatternsTest.ini("global $look = 0\n","","","")+face;File src=GimiPatternsTest.mod(base,"face",text);GimiPatternsTest.put(src,"CitlaliFaceHeadDiffuse.dds",new byte[128]);
        File out=new File(base,"face-out");StringBuilder log=new StringBuilder();ModelConverter.convert(src,null,out,10000107,new ModelConverter.Options().variants(true),DECODE,log);
        Map<String,Object> manifest=Json.obj(Json.read(new File(out,"model.json"),4L<<20),"model");Map<String,Object> swap=Json.obj(Json.arr(manifest.get("textures"),"textures",1,1).get(0),"texture");
        ok("Face".equals(swap.get("renderer"))&&"FaceTexture".equals(swap.get("material"))&&log.toString().contains("脸部贴图：CitlaliFaceHeadDiffuse.dds -> 游戏的 Face 渲染器"),"the face texture becomes a texture swap: "+log);
        boolean material=false;for(Object raw:Json.arr(manifest.get("materials"),"materials",1,64)){Map<String,Object> m=Json.obj(raw,"material");material|="FaceTexture".equals(m.get("name"))&&m.get("main")!=null;}
        ok(material,"the swap's material carries the face texture");
        String varying=GimiPatternsTest.ini("global $look = 0\n","","","")+"[TextureOverrideCitlaliFaceHeadDiffuse]\nhash = 55555555\nif $look == 0\nps-t0 = ResourceCitlaliFaceHeadDiffuse\nelse\nps-t0 = ResourceCitlaliFaceAlt\nendif\n"
            +"[ResourceCitlaliFaceHeadDiffuse]\nfilename = CitlaliFaceHeadDiffuse.dds\n[ResourceCitlaliFaceAlt]\nfilename = CitlaliFaceAlt.dds\n[KeyLook]\nkey = l\ntype = cycle\n$look = 0,1\n";
        GimiPatternsTest.put(src,"CitlaliFaceAlt.dds",new byte[128]);GimiPatternsTest.put(src,"Citlali.ini",varying);log=new StringBuilder();
        ModelConverter.convert(src,null,out,10000107,new ModelConverter.Options().variants(true).replace(true),DECODE,log);manifest=Json.obj(Json.read(new File(out,"model.json"),4L<<20),"model");
        ok(manifest.get("textures")==null&&log.toString().contains("脸部贴图随款式或动画帧变化，暂不导入"),"a face that changes with a value only it reads is left out: "+log);
        GimiPatternsTest.put(src,"Citlali.ini",text+"[TextureOverrideCitlaliFaceDiffuseTwo]\nhash = 66666666\nps-t0 = ResourceCitlaliFaceAlt\n[ResourceCitlaliFaceAlt]\nfilename = CitlaliFaceAlt.dds\n");boolean rejected=false;
        try{ModelConverter.convert(src,null,new File(base,"face-two"),10000107,new ModelConverter.Options().variants(true),DECODE,new StringBuilder());}catch(IllegalArgumentException e){rejected=e.getMessage().contains("脸部贴图有多个来源");}
        ok(rejected,"two face files in one assignment are refused");
        // A frame animation whose every frame binds the same face texture swaps it too (the Traveler mod's layout).
        String program="[Present]\nif $counter % $speed == 0\nif $frame < $last\n$frame = $frame + 1\nelse\n$frame = $first\nendif\nendif\npost $counter = $counter + 1\n";
        String animated=GimiPatternsTest.ini("global $speed = 0.5\nglobal $counter = 0\nglobal $frame = 0\nglobal $first = 0\nglobal $last = 2\n","if $frame == 1\nvb0 = ResourceNextPosition\nendif\n","","")
            +"[ResourceNextPosition]\nstride = 40\nfilename = Next.buf\n"+program+face;
        GimiPatternsTest.put(src,"Next.buf",Files.readAllBytes(new File(src,"CitlaliPosition.buf").toPath()));GimiPatternsTest.put(src,"Citlali.ini",animated);log=new StringBuilder();
        File frames=new File(base,"face-frames");ModelConverter.convert(src,null,frames,10000107,new ModelConverter.Options().variants(true),DECODE,log);manifest=Json.obj(Json.read(new File(frames,"model.json"),4L<<20),"model");
        ok(ModelManifest.read(frames).version==4&&Json.arr(manifest.get("textures"),"textures",1,1).size()==1&&log.toString().contains("脸部贴图：CitlaliFaceHeadDiffuse.dds"),"an animation keeps its face texture: "+log);
    }
    /** The logic data of an INI-only mod (no geometry is built): tests/fixtures/logic-signature. A part with two
     *  drawindexed whose ranges come from a variable, another INI with a [Resource] of the same name, a DISABLED INI. */
    @SuppressWarnings("unchecked")
    private static void logicSignature(File base)throws Exception{File src=new File("tests/fixtures/logic-signature").getCanonicalFile(),out=new File(base,"logic-signature");out.mkdirs();
        ModelConverter.Ini ini=new ModelConverter.Ini(src,new File(src,"Hero.ini"));ModelConverter.Animation an=ModelConverter.Animation.parse(ini);ok(an!=null,"the fixture is a Present loop");
        java.lang.reflect.Method logic=ModelConverter.class.getDeclaredMethod("logic",ModelConverter.Ini.class,File.class,ModelConverter.Animation.class,Map.class,File.class,StringBuilder.class);logic.setAccessible(true);
        StringBuilder log=new StringBuilder();Map<String,Object> data=(Map<String,Object>)logic.invoke(null,ini,src,an,new HashMap<String,String>(),out,log);ok(data!=null,"logic data: "+log);
        List<Object> files=Json.arr(data.get("files"),"files",2,2);ok(Json.obj(files.get(0),"f").get("namespace").equals("Mods\\logic-signature\\extra.ini")&&Json.obj(files.get(1),"f").get("namespace").equals("Mods\\logic-signature\\Hero.ini"),"both INIs, DISABLED skipped: "+files);
        Map<String,Object> res=Json.obj(data.get("resources"),"resources");String head="file:head.ib,type=buffer,format=dxgi_format_r32_uint",body="file:body.ib,type=buffer,format=dxgi_format_r32_uint";
        ok(head.equals(res.get("resource\\mods\\logic-signature\\hero.ini\\heroheadib"))&&"file:other.ib,type=buffer,format=dxgi_format_r32_uint".equals(res.get("resource\\mods\\logic-signature\\extra.ini\\heroheadib"))&&"file:textures/diffuse.dds".equals(res.get("resource\\mods\\logic-signature\\hero.ini\\herodiffuse"))&&res.size()==7,"one key per INI, same name kept apart: "+res);
        List<Object> frames=Json.arr(data.get("frames"),"frames",2,2);String common=";position=file:position.buf,type=buffer,stride=40;blend=file:blend.buf,type=buffer,stride=32;texcoord=file:texcoord.buf,type=buffer,stride=20;ps-t0=file:textures/diffuse.dds;ps-t1=-;ps-t2=-;|";
        String body0=(String)Json.obj(Json.obj(frames.get(0),"frame").get("draws"),"d").get("HeroBody"),body1=(String)Json.obj(Json.obj(frames.get(1),"frame").get("draws"),"d").get("HeroBody"),head0=(String)Json.obj(Json.obj(frames.get(0),"frame").get("draws"),"d").get("HeroHead");
        ok(body0.equals("kind=drawindexed;count=3;start=0;base=0;ib="+body+common+"kind=drawindexed;count=3;start=3;base=0;ib="+body+common),"two draws, ranges from $half, in command order: "+body0);
        ok(body1.equals("kind=drawindexed;count=6;start=0;base=0;ib="+body+common)&&head0.equals("kind=drawindexed;count=auto;start=0;base=0;ib="+head+common),"frame 1 and the auto draw: "+body1+" / "+head0);}
    private static String nativeChecker;   // tools/test_host.py passes build/host/core_test
    // The [Key] sections for the App's panel: every type with a key= or back= (the first type= counts, an unknown one is
    // activate, as 3DMigoto takes it); a section without a key, a commented-out type and a name 3DMigoto would not take are
    // left out. Presets as KeyOverrideCycle splits them: by position, blank or non-numeric cells left out; "special" with a
    // condition or a run at that position (the App then only steps).
    static void logicKeys(){
        String a="[KeyA]\r\nkey = 1\r\ntype = cycle\r\ntype = hold\r\n$x = 0,1\r\n[KeyB]\nback = 2\ntype = Toggle\n$x = 1\n[KeyHold]\nkey = 3\ntype = hold\n$x = 1\n"
            +"[KeyPlain]\nkey = 4\nkey = no_modifiers 4\n$x = 2\n[KeyNone]\ntype = cycle\n$x = 0,1\n[KeyNote]\nkey = 5\n;type = cycle\n[Key]\nkey = 6\ntype = cycle\n[TextureOverrideKey]\nkey = 7\ntype = cycle\n";
        List<Object> keys=ModelConverter.logicKeys(Arrays.asList(a,"[keyC]\nkey = 8\ntype = toggle\n"));
        List<Object> none=new ArrayList<>();Map<String,Object> empty=new LinkedHashMap<>();
        ok(keys.equals(Arrays.asList(
            Json.of("file",0,"section","KeyA","type","cycle","bind",Arrays.asList("1"),"presets",Arrays.asList(Json.of("values",Json.of("$x","0"),"special",false),Json.of("values",Json.of("$x","1"),"special",false))),
            Json.of("file",0,"section","KeyB","type","toggle","bind",none,"presets",Arrays.asList(Json.of("values",Json.of("$x","1"),"special",false))),
            Json.of("file",0,"section","KeyHold","type","hold","bind",Arrays.asList("3"),"presets",Arrays.asList(Json.of("values",Json.of("$x","1"),"special",false))),
            Json.of("file",0,"section","KeyPlain","type","activate","bind",Arrays.asList("4","no_modifiers 4"),"presets",Arrays.asList(Json.of("values",Json.of("$x","2"),"special",false))),
            Json.of("file",0,"section","KeyNote","type","activate","bind",Arrays.asList("5"),"presets",Arrays.asList(Json.of("values",empty,"special",false))),
            Json.of("file",1,"section","keyC","type","toggle","bind",Arrays.asList("8"),"presets",Arrays.asList(Json.of("values",empty,"special",false))))),"logic keys: "+keys);
        List<Object> back=ModelConverter.logicKeys(Arrays.asList("[KeySwap]\nkey = ]\nback = [\ntype = cycle\n$x = 0,1\n"));
        ok(back.size()==1&&Json.obj(back.get(0),"key").get("back").equals(true),"a cycle with back = is listed with back: "+back);
        List<Object> special=ModelConverter.logicKeys(Arrays.asList("[KeyLook]\nkey = 1\ntype = cycle\n$a = 0,1,,2\n$b = 1,x,1\ncondition = ,,$on == 1\nrun = ,CommandListB\n[KeyExpr]\nkey = 2\n$a = $b + 1\n"));
        ok(special.equals(Arrays.asList(
            Json.of("file",0,"section","KeyLook","type","cycle","bind",Arrays.asList("1"),"presets",Arrays.asList(
                Json.of("values",Json.of("$a","0","$b","1"),"special",false),Json.of("values",Json.of("$a","1"),"special",true),
                Json.of("values",Json.of("$b","1"),"special",true),Json.of("values",Json.of("$a","2"),"special",false))),
            Json.of("file",0,"section","KeyExpr","type","activate","bind",Arrays.asList("2"),"presets",Arrays.asList(Json.of("values",empty,"special",true))))),"cycle presets by position, special ones: "+special);
    }
    public static void main(String[] args)throws Exception{
        nativeChecker=args.length>0?args[0]:null;File base=Files.createTempDirectory("gibox-compatibility-").toFile();
        try{logicKeys();legacy(base);hohe(base);animation(base);rehearsedAnimation(base);drivenOutfits(base);staticBackend(base);logicSignature(base);face(base);System.out.println("Compatibility: PASS (3dm backend for static, outfit and animated models, logic core data for animations and outfits, rehearsed Present programs, driven outfits, switched-off schemes, App keys (every type, presets by position, special presets, back), face texture swap, layout precedence, variants, R16, main INI, duplicates, PC filename casing, TexFx ranges, ambiguity, INI schemes)");}
        finally{ModelConverter.remove(base);}
    }
}
