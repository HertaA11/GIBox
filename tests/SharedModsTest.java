package com.gibox.tool;
import java.io.*;import java.nio.charset.StandardCharsets;import java.nio.file.*;import java.util.*;
public final class SharedModsTest {
 static void ok(boolean b,String why){if(!b)throw new AssertionError(why);}
 static void put(File root,String name,String text)throws Exception{File file=new File(root,name);Files.createDirectories(file.getParentFile().toPath());Files.write(file.toPath(),text.getBytes(StandardCharsets.UTF_8));}
 static String text(File root,String name)throws Exception{return new String(Files.readAllBytes(new File(root,name).toPath()),StandardCharsets.UTF_8);}
 static Map<String,Object> doc(File root)throws Exception{return Json.obj(Json.read(new File(root,"control.json"),1L<<20),"control");}
 public static void main(String[] args)throws Exception{
  File base=Files.createTempDirectory("gibox-shared-mods-").toFile(),files=new File(base,"private"),cn=new File(base,"cn"),global=new File(base,"global"),b=new File(base,"b");
  Map<String,File> roots=new LinkedHashMap<>();roots.put("cn",cn);roots.put("global",global);
  put(cn,"control.json","{\"enabled\":true,\"selected\":[\"A\"],\"variables\":{\"A\":{\"$look\":1}},\"keySeq\":5,\"keys\":{\"A\":[{\"seq\":5}]},\"exportBones\":7,\"logLevel\":\"debug\"}");
  put(global,"control.json","{\"enabled\":false,\"selected\":[\"Other\"],\"keySeq\":12,\"exportBones\":2,\"logLevel\":\"warning\"}");
  put(global,"status.json","global status");put(global,"hash-map.json","global hashes");put(global,"logic-persist/A.json","local persist");
  put(cn,"models/Persisted/model.json","{\"logic\":{\"main\":\"main\"},\"variants\":{\"vars\":[{\"name\":\"$look\",\"values\":[0,1,2]}]}}");
  put(cn,"logic-persist/Persisted.json","{\"format\":\"gibox-logic-persist\",\"main\":\"main\",\"values\":{\"$look\":2}}");
  SharedMods shared=new SharedMods(files,roots);shared.initialize("cn");
  ok(doc(global).get("selected").equals(Collections.singletonList("A"))&&Boolean.TRUE.equals(doc(global).get("enabled")),"switch migration");
  ok(((Number)doc(global).get("exportBones")).intValue()==2&&doc(global).get("logLevel").equals("warning"),"local requests/settings changed");
  ok(((Number)((Map<?,?>)((Map<?,?>)doc(cn).get("variables")).get("Persisted")).get("$look")).intValue()==2,"active client's saved style was lost at migration");
  ok(((Number)doc(cn).get("keySeq")).intValue()==12&&((Map<?,?>)doc(cn).get("keys")).isEmpty(),"old key events replayed or sequence regressed");
  ok(new File(cn,"control-before-shared.json").isFile()&&new File(global,"control-before-shared.json").isFile(),"legacy controls not backed up");
  Map<String,Object> changed=Json.obj(Json.parse(shared.loadControl("global")),"control");changed.put("enabled",false);changed.put("selected",Collections.emptyList());changed.put("variables",Json.of("A",Json.of("$look",2)));changed.put("keys",Json.of("A",Arrays.asList(Json.of("seq",13,"section","KeyLook"))));changed.put("keySeq",13);shared.saveControl("global",Json.write(changed));
  ok(Boolean.FALSE.equals(doc(cn).get("enabled"))&&doc(cn).get("selected").equals(Collections.emptyList())&&Json.write(doc(cn).get("variables")).contains("2"),"global choices did not reach CN");
  ok(((Number)doc(cn).get("exportBones")).intValue()==7&&doc(cn).get("logLevel").equals("debug"),"CN diagnostics/request overwritten");
  File source=new File(cn,"models/A");put(source,"model.json","{\"generation\":\"one\"}");put(source,"model.bin","payload-one");put(source,"logic/main.ini","INI");put(source,"learned.json","CN learned");put(source,"texture-mapping.json","CN textures");put(source,"bonemap-Part0.tsv","CN bones");
  File target=new File(global,"models/A");put(target,"model.json","{\"generation\":\"old-global\"}");put(target,"model.bin","old payload");put(target,"learned.json","global old");shared.prepare("cn");
  ok(text(target,"model.bin").equals("payload-one")&&text(target,"logic/main.ini").equals("INI"),"payload not distributed");
  ok(new File(source,"learned.json").isFile()&&!new File(target,"learned.json").exists()&&!new File(target,"texture-mapping.json").exists()&&!new File(target,"bonemap-Part0.tsv").exists(),"runtime metadata leaked across games");
  ok(Arrays.stream(global.listFiles()).anyMatch(x->x.getName().startsWith(".shared-old-")),"conflicting legacy model not backed up");
  ok(text(global,"status.json").equals("global status")&&text(global,"hash-map.json").equals("global hashes")&&text(global,"logic-persist/A.json").equals("local persist"),"runtime outputs overwritten");
  put(target,"learned.json","global new");shared.publish(source);ok(text(target,"learned.json").equals("global new"),"same conversion lost local mapping");
  put(source,"model.json","{\"generation\":\"two\"}");put(source,"model.bin","payload-two");ok(shared.publish(source).isEmpty(),"distribution failed");ok(text(target,"model.bin").equals("payload-two")&&!new File(target,"learned.json").exists(),"new conversion retained incompatible mapping");
  File bad=new File(base,"blocked");Files.write(bad.toPath(),new byte[]{1});Map<String,File> badRoots=new LinkedHashMap<>(roots);badRoots.put("blocked",bad);
  SharedMods partial=new SharedMods(files,badRoots);List<String> failures=partial.publish(source);ok(failures.size()==1&&failures.get(0).contains("blocked")&&text(target,"model.bin").equals("payload-two"),"distribution failure lost a valid client payload");
  Path link=new File(source,"external-link").toPath();Files.createSymbolicLink(link,bad.toPath());boolean refused=false;try{shared.publish(source);}catch(IllegalArgumentException expected){refused=true;}finally{Files.delete(link);}ok(refused&&text(new File(files,"converted/A"),"model.bin").equals("payload-two"),"failed snapshot replaced canonical payload");
  roots.put("b",b);SharedMods reopened=new SharedMods(files,roots);reopened.initialize("global");reopened.prepare("global");ok(text(b,"models/A/model.bin").equals("payload-two")&&Boolean.FALSE.equals(doc(b).get("enabled")),"newly installed game did not get existing conversion/state");
  reopened.delete("A");ok(!source.exists()&&!target.exists()&&!new File(b,"models/A").exists()&&!new File(files,"converted/A").exists(),"deletion did not reach every copy");
  put(b,"models/A/model.json","{\"generation\":\"leftover\"}");put(b,"models/A/model.bin","leftover");reopened.prepare("cn");ok(!new File(b,"models/A").exists(),"deleted model resurrected from stale client");
  ModelConverter.remove(base);System.out.println("Shared mods: PASS (one payload, migration and backups, global switches/variants/key events, local diagnostics and mappings, late-installed client, deletion)");
 }
}
