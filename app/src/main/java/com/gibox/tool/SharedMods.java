package com.gibox.tool;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

/** One conversion and one set of mod choices; games receive payloads in their own readable media directories. */
public final class SharedMods {
    private static final String[] COMMON={"enabled","selected","variables","keys","keySeq"};
    private static final String MARK=".shared-generation";
    private final File state,cache,deleted;
    private static final Map<String,Object> LOCKS=new HashMap<>();
    private final Object modelLock;
    private final LinkedHashMap<String,File> games;
    public SharedMods(File files,Map<String,File> roots){state=new File(files,"shared-mod-control.json");cache=new File(files,"converted");deleted=new File(files,"converted-deleted");games=new LinkedHashMap<>(roots);synchronized(LOCKS){modelLock=LOCKS.computeIfAbsent(files.getAbsolutePath(),k->new Object());}}
    private static Map<String,Object> read(File file)throws Exception{return Json.obj(Json.read(file,8L<<20),file.getName());}
    private static Map<String,Object> local(File file)throws Exception{return file.isFile()?read(file):new LinkedHashMap<>();}
    private static void write(File file,String text)throws Exception{
        Files.createDirectories(file.getParentFile().toPath());File tmp=File.createTempFile(".gibox-",".tmp",file.getParentFile());
        try{try(FileOutputStream out=new FileOutputStream(tmp)){out.write(text.getBytes(StandardCharsets.UTF_8));out.getFD().sync();}Files.move(tmp.toPath(),file.toPath(),StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);}finally{Files.deleteIfExists(tmp.toPath());}
    }
    private File root(String pkg){File result=games.get(pkg);Json.check(result!=null,"未配置游戏目录："+pkg);return result;}
    public synchronized void initialize(String preferred)throws Exception{
        if(!state.isFile()){
            Map<String,Object> seed=new LinkedHashMap<>();String seedPkg=preferred;long seq=0;List<String> order=new ArrayList<>(games.keySet());if(order.remove(preferred))order.add(0,preferred);
            for(String pkg:order){File file=new File(root(pkg),"control.json");Map<String,Object> doc=local(file);if(seed.isEmpty()&&!doc.isEmpty()){seed.putAll(doc);seedPkg=pkg;}Object n=doc.get("keySeq");if(n instanceof Number)seq=Math.max(seq,((Number)n).longValue());
                if(file.isFile()&&!new File(root(pkg),"control-before-shared.json").exists())Files.copy(file.toPath(),new File(root(pkg),"control-before-shared.json").toPath());}
            Map<String,Object> common=new LinkedHashMap<>();for(String key:COMMON)if(seed.containsKey(key))common.put(key,seed.get(key));
            common.putIfAbsent("enabled",false);common.putIfAbsent("selected",new ArrayList<>());common.putIfAbsent("variables",new LinkedHashMap<>());
            restoreChoices(root(seedPkg),common);common.put("sharedModControl",true);common.put("keys",new LinkedHashMap<>());common.put("keySeq",seq);write(state,Json.write(common));
        }
        replicate(null,null,read(state));
    }
    /** Seed declared integer outfit variables from the chosen client's matching INI, once at migration. */
    private static void restoreChoices(File root,Map<String,Object> common){
        Map<String,Object> all=common.get("variables") instanceof Map?Json.obj(common.get("variables"),"variables"):new LinkedHashMap<>();
        File[] models=new File(root,"models").listFiles();if(models!=null)for(File model:models)try{
            File manifest=new File(model,"model.json"),persist=new File(root,"logic-persist/"+model.getName()+".json");if(!manifest.isFile()||!persist.isFile())continue;
            Map<String,Object> doc=read(manifest),logic=Json.obj(doc.get("logic"),"logic"),saved=read(persist);
            if(!"gibox-logic-persist".equals(saved.get("format"))||!Objects.equals(logic.get("main"),saved.get("main")))continue;
            Map<String,Object> values=Json.obj(saved.get("values"),"values");Map<String,Object> choice=all.get(model.getName()) instanceof Map?Json.obj(all.get(model.getName()),"choice"):new LinkedHashMap<>();
            Map<String,Set<Integer>> allowed=new LinkedHashMap<>();
            if(doc.get("variants") instanceof Map){Map<String,Object> variants=Json.obj(doc.get("variants"),"variants");for(Object raw:Json.arr(variants.get("vars"),"vars",0,256)){Map<String,Object> var=Json.obj(raw,"var");String key=Json.str(var.get("name"),"name",128);for(Object n:Json.arr(var.get("values"),"values",1,4096))if(n instanceof Number)allowed.computeIfAbsent(key,k->new HashSet<>()).add(((Number)n).intValue());}}
            if(logic.get("keys") instanceof List)for(Object raw:(List<?>)logic.get("keys")){Map<String,Object> key=Json.obj(raw,"key");if(!"cycle".equals(key.get("type"))||!(key.get("presets") instanceof List))continue;List<?> presets=(List<?>)key.get("presets");if(presets.size()<2||presets.stream().anyMatch(x->Boolean.TRUE.equals(Json.obj(x,"preset").get("special"))))continue;
                for(Object entry:presets)for(Map.Entry<String,Object> value:Json.obj(Json.obj(entry,"preset").get("values"),"values").entrySet())try{double n=Double.parseDouble(String.valueOf(value.getValue()));if(Double.isFinite(n)&&n==(int)n)allowed.computeIfAbsent(value.getKey(),k->new HashSet<>()).add((int)n);}catch(NumberFormatException ignored){}}
            for(Map.Entry<String,Set<Integer>> var:allowed.entrySet()){String key=var.getKey();Object value=values.get("$\\"+String.valueOf(logic.get("main")).toLowerCase(Locale.ROOT)+"\\"+key.substring(1));if(value==null)value=values.get(key);
                if(value instanceof Number&&Double.isFinite(((Number)value).doubleValue())&&((Number)value).doubleValue()==((Number)value).intValue()&&var.getValue().contains(((Number)value).intValue()))choice.put(key,value);}
            if(!choice.isEmpty())all.put(model.getName(),choice);
        }catch(Exception ignored){}
        common.put("variables",all);
    }
    public synchronized String loadControl(String pkg)throws Exception{Map<String,Object> doc=local(new File(root(pkg),"control.json"));if(state.isFile())doc.putAll(read(state));return Json.write(doc);}
    public synchronized void saveControl(String pkg,String raw)throws Exception{
        Map<String,Object> doc=Json.obj(Json.parse(raw),"control"),common=local(state);for(String key:COMMON)if(doc.containsKey(key))common.put(key,doc.get(key));
        write(state,Json.write(common));replicate(pkg,doc,common);
    }
    private void replicate(String changed,Map<String,Object> updated,Map<String,Object> common)throws Exception{
        List<String> failed=new ArrayList<>();for(String pkg:games.keySet())try{Map<String,Object> doc=pkg.equals(changed)?new LinkedHashMap<>(updated):local(new File(root(pkg),"control.json"));doc.putAll(common);
            String text=Json.write(doc);File target=new File(root(pkg),"control.json");if(!target.isFile()||!new String(Files.readAllBytes(target.toPath()),StandardCharsets.UTF_8).equals(text))write(target,text);
        }catch(Exception e){failed.add(pkg+"："+e.getMessage());}
        if(!failed.isEmpty())throw new IOException("同步 MOD 开关失败："+String.join("；",failed));
    }
    private static boolean payload(File dir){return new File(dir,"model.json").isFile()||new File(dir,"texture.json").isFile()||new File(dir,"loading.json").isFile();}
    private static boolean runtime(String name){return name.equals("learned.json")||name.equals("texture-mapping.json")||name.startsWith("bonemap-")||name.equals(MARK)||name.endsWith(".tmp");}
    private static void copy(File from,File to,boolean payloadOnly)throws Exception{
        Json.check(!Files.isSymbolicLink(from.toPath()),"转换目录不能包含符号链接："+from);
        if(from.isDirectory()){Files.createDirectories(to.toPath());File[] list=from.listFiles();if(list==null)throw new IOException("无法读取 "+from);for(File child:list)if(!payloadOnly||!runtime(child.getName()))copy(child,new File(to,child.getName()),payloadOnly);}
        else Files.copy(from.toPath(),to.toPath(),StandardCopyOption.REPLACE_EXISTING);
    }
    private static boolean same(File a,File b)throws Exception{for(String name:new String[]{"model.json","texture.json","loading.json"}){File x=new File(a,name),y=new File(b,name);if(x.isFile()!=y.isFile())return false;if(x.isFile()&&!Arrays.equals(Files.readAllBytes(x.toPath()),Files.readAllBytes(y.toPath())))return false;}return true;}
    private static String marker(File dir)throws Exception{File f=new File(dir,MARK);return f.isFile()?new String(Files.readAllBytes(f.toPath()),StandardCharsets.UTF_8):"";}
    private static void name(String n){Json.check(n!=null&&!n.isEmpty()&&!n.startsWith(".")&&!n.contains("/")&&!n.contains("\\"),"MOD 名称无效");}
    private static void replace(File from,File to,boolean retainBackup)throws Exception{
        File backup=new File(to.getParentFile().getParentFile(),".shared-old-"+UUID.randomUUID());boolean moved=false;
        if(to.exists()){Files.move(to.toPath(),backup.toPath(),StandardCopyOption.ATOMIC_MOVE);moved=true;}
        try{Files.createDirectories(to.getParentFile().toPath());Files.move(from.toPath(),to.toPath(),StandardCopyOption.ATOMIC_MOVE);}catch(Exception e){if(moved)Files.move(backup.toPath(),to.toPath(),StandardCopyOption.ATOMIC_MOVE);throw e;}
        if(moved&&!retainBackup)ModelConverter.remove(backup);
    }
    public <T> T convert(java.util.concurrent.Callable<T> action)throws Exception{synchronized(modelLock){return action.call();}}
    private void remember(File source)throws Exception{
        String n=source.getName();name(n);Json.check(payload(source),"没有可分发的转换产物："+n);Files.createDirectories(cache.getParentFile().toPath());
        File stage=Files.createTempDirectory(cache.getParentFile().toPath(),".shared-convert-").toFile();try{copy(source,stage,true);String generation=UUID.randomUUID().toString();write(new File(stage,MARK),generation);replace(stage,new File(cache,n),false);write(new File(source,MARK),generation);}finally{if(stage.exists())ModelConverter.remove(stage);}
    }
    private void deploy(String pkg,String n,boolean migration)throws Exception{
        File source=new File(cache,n),dest=new File(new File(root(pkg),"models"),n);String generation=marker(source);if(payload(dest)&&generation.equals(marker(dest)))return;
        Files.createDirectories(root(pkg).toPath());File stage=Files.createTempDirectory(root(pkg).toPath(),".shared-deploy-").toFile();try{copy(source,stage,false);
            if(payload(dest)&&same(source,dest)){File[] files=dest.listFiles();if(files!=null)for(File f:files)if(runtime(f.getName())&&!f.getName().equals(MARK)&&!f.getName().endsWith(".tmp"))copy(f,new File(stage,f.getName()),false);}
            replace(stage,dest,migration&&payload(dest)&&!same(source,dest));
        }finally{if(stage.exists())ModelConverter.remove(stage);}
    }
    /** Existing conversions are adopted once, active game's copy first; no converter is run during migration. */
    public void prepare(String preferred)throws Exception{synchronized(modelLock){
        List<String> order=new ArrayList<>(games.keySet());if(order.remove(preferred))order.add(0,preferred);
        for(String pkg:order){File[] list=new File(root(pkg),"models").listFiles();if(list!=null)for(File dir:list)if(dir.isDirectory()&&!dir.getName().startsWith(".")&&payload(dir)){if(new File(deleted,dir.getName()).exists())ModelConverter.remove(dir);else if(!payload(new File(cache,dir.getName())))remember(dir);}}
        File[] list=cache.listFiles();if(list!=null)for(File model:list)if(model.isDirectory()&&payload(model)){if(new File(deleted,model.getName()).exists())ModelConverter.remove(model);else for(String pkg:games.keySet())deploy(pkg,model.getName(),true);}
        synchronized(this){replicate(null,null,read(state));}
    }}
    public List<String> publish(File source)throws Exception{synchronized(modelLock){
        remember(source);Files.deleteIfExists(new File(deleted,source.getName()).toPath());List<String> failed=new ArrayList<>();for(String pkg:games.keySet())try{deploy(pkg,source.getName(),false);}catch(Exception e){failed.add(pkg+"：分发失败（"+e.getMessage()+"）");}return failed;
    }}
    public void delete(String n)throws Exception{synchronized(modelLock){name(n);write(new File(deleted,n),"deleted");ModelConverter.remove(new File(cache,n));for(String pkg:games.keySet())Importer.deleteModel(n,new File(root(pkg),"models"));}}
}
