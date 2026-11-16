package com.gibox.tool;
import java.io.*;
import java.nio.file.*;
import java.util.*;

/** Bundled eye hashes must resolve without any device capture and override the Body fallback. */
public final class DeviceHashesTest {
 static void ok(boolean value,String why){if(!value)throw new AssertionError(why);}
 public static void main(String[] args)throws Exception{
  File base=Files.createTempDirectory("gibox-eye-hashes-").toFile(),shared=new File(base,"shared"),bones=new File(base,"bones");shared.mkdirs();
  Files.copy(new File("app/src/main/assets/device-hashes.json").toPath(),new File(shared,"device-hashes.json").toPath());
  String outfit="avatar_girl_sword_playergirlcostumecwxr";DeviceHashes table=DeviceHashes.load(shared);Map<String,String> renderers=table.renderers(10000007,outfit);
  Map<String,Map<String,Object>> targets=new HashMap<>();targets.put("Body",Fixtures.target("Body",31179,2,2));targets.put("Pupil",Fixtures.target("Pupil",246,1,2));
  Fixtures.skeleton(bones,10000007,"Scene/Avatar_Girl_Sword_PlayerGirlCostumeCWXR",new String[]{"Body","Pupil"},new String[]{"Eye_L","Eye_R"},targets);
  for(String hash:new String[]{"0e247223","36f018fc","3967859e"}){
   ok("Pupil".equals(renderers.get(hash)),"missing bundled Pupil hash "+hash);
   ok(!table.renderers(10000007,"avatar_girl_sword_playergirl").containsKey(hash),"costume eye hash leaked to original outfit");
   String ini="[TextureOverrideLumineSkinEyesPosition]\nvb0 = ResourceCitlaliPosition\n[TextureOverrideLumineSkinEyesBlend]\nvb1 = ResourceCitlaliBlend\n[TextureOverrideLumineSkinEyesTexcoord]\nvb1 = ResourceCitlaliTexcoord\n[TextureOverrideLumineSkinEyesA]\nhash = "+hash+"\nib = ResourceCitlaliHeadIB\nps-t0 = ResourceCitlaliHeadDiffuse\ndrawindexed = 3,0,0\n"+GimiPatternsTest.RES;
   File src=GimiPatternsTest.mod(base,"eyes-"+hash,ini),out=new File(base,"out-"+hash);StringBuilder log=new StringBuilder();
   ModelConverter.convert(src,bones,out,10000007,new ModelConverter.Options().fallback("Body").deviceRenderers(renderers),data->new byte[]{1,0,0,0,1,0,0,0,-1,-1,-1,-1},log);
   Map<String,Object> part=Fixtures.part(out,0);ok("Pupil".equals(part.get("renderer"))&&Fixtures.number(part,"bones")==2,"eyes fell back to Body: "+log);
   Map<String,Object> model=Json.obj(Json.read(new File(out,"model.json"),4L<<20),"model");ok(((List<?>)model.get("parts")).size()==1,"unexpected replacement parts");
   ok(log.toString().contains("LumineSkinEyesA -> Pupil")&&!log.toString().contains("按默认渲染器"),"hash mapping did not take precedence");
  }
  ModelConverter.remove(base);System.out.println("Bundled eye hashes: PASS (three upload forms, costume scope, Pupil palette, no device capture or Body fallback)");
 }
}
