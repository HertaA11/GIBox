package com.gibox.tool;
import java.io.File;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.util.*;
public final class GliderModTest {
    private static void ok(boolean value,String why){if(!value)throw new AssertionError(why);}
    static String ini(String owner){StringBuilder s=new StringBuilder("namespace=Test\\SignatureGliders\n[Constants]\nglobal $"+owner+"Active = 0\n[TextureOverride"+owner+"Position]\nhash = 12345678\n$"+owner+"Active = 2\n[Present]\npost $"+owner+"Active = $"+owner+"Active - 1\n");
        String[] wings={"FirstFlight","Companionship","Descension","AzureWind","GoldenFlight","ConsealingSnow","Feasting","Stormrider","ShimmeringGalaxy","Forest","StarlitFeast","MercifulWrathfulWaters","TransientDreams","SoaringFlame","Delicacies","FatesCourseIntertwined"};
        for(int i=0;i<16;i++)s.append("[TextureOverride").append(wings[i]).append("1024]\nhash = ").append(String.format("%08x",i)).append("\nif $").append(owner).append("Active > 0\nps-t0 = ResourceGlider\nendif\n");return s+"[ResourceGlider]\nfilename = Glider1024.dds\n";}
    private static void put(File root,String owner)throws Exception{File d=new File(root,owner);d.mkdirs();Files.write(new File(d,owner+".ini").toPath(),ini(owner).getBytes("UTF-8"));Files.write(new File(d,"Glider1024.dds").toPath(),new byte[]{1});}
    public static void main(String[] args)throws Exception {File base=Files.createTempDirectory("gibox-glider-").toFile();try{
        File mod=new File(base,"mod");put(mod,"Albedo");put(mod,"JeanSea");
        ok(GliderMod.ini(mod)!=null&&Importer.schemes(mod).size()==1,"character INIs are one package");
        File assets=new File("app/src/main/assets"),models=new File(base,"models");models.mkdirs();
        ok(Importer.guess(mod,"mod",new File(assets,"skeletons")).recommended.contains(Characters.SCENE_AVATAR),"scene recommendation");
        Importer.Result result=Importer.convert(mod,Characters.SCENE,models,new File(assets,"skeletons"),bytes->ByteBuffer.allocate(24).order(ByteOrder.LITTLE_ENDIAN).putInt(2).putInt(2).put(new byte[16]).array(),new StringBuilder());
        Map<String,Object> manifest=Json.obj(Json.read(new File(result.model,"texture.json"),4L<<20),"manifest");Map<String,Object> doc=Json.obj(manifest.get("diag"),"diag");List<Object> rules=Json.arr(doc.get("rules"),"rules",2,2);
        ok(Json.arr(manifest.get("textures"),"textures",2,2).size()==2,"one diffuse per character, not each PC resolution and icon");
        boolean costume=false;for(Object raw:rules){Map<String,Object> r=Json.obj(raw,"rule");ok("glider-owner".equals(r.get("kind")),"owner rule");if(String.valueOf(r.get("ownerAvatar")).contains("qincostumesea"))costume=true;}ok(costume,"JeanSea uses verified outfit");
        File bad=new File(base,"bad");put(bad,"Albedo");File ini=new File(bad,"Albedo/Albedo.ini");Files.write(ini.toPath(),GliderModTest.ini("Albedo").replace("if $AlbedoActive > 0","if $Unknown > 0").getBytes("UTF-8"));
        try{GliderMod.install(bad,new File(base,"bad-out"),assets,bytes->new byte[0],new StringBuilder());throw new AssertionError("unknown owner condition accepted");}catch(IllegalArgumentException expected){ok(expected.getMessage().contains("条件"),"unsupported conditions rejected");}
        System.out.println("Glider packages: PASS (combined import, scene recommendation, per-owner resources, verified costume, unknown conditions refused)");
    }finally{ModelConverter.remove(base);}}
}
