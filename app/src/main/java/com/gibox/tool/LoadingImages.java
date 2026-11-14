package com.gibox.tool;

import java.io.*;
import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.regex.*;

final class LoadingImages {
    static final String FILE="loading.json";
    static boolean is(File dir){return new File(dir,FILE).isFile();}
    static File ini(File mod){if(mod==null||!mod.isDirectory())return null;for(File f:ModConfig.allInis(mod))if(Pattern.compile("(?im)^\\s*\\[ResourceLS\\.\\d+\\]\\s*$").matcher(ModConfig.source(f)).find())return f;return null;}
    static Importer.Result install(File mod,File dest,ModelConverter.TextureDecoder decoder,StringBuilder log)throws Exception{
        File ini=ini(mod);Json.check(ini!=null,"没有找到加载图片列表（ResourceLS.0 等），请选择对应的加载背景 MOD");
        ModelConverter.Ini parsed=new ModelConverter.Ini(ini.getParentFile(),ini);Map<Integer,File> images=new TreeMap<>();
        for(ModelConverter.Section s:parsed.sections.values()){
            Matcher m=Pattern.compile("(?i)^ResourceLS\\.(\\d+)$").matcher(s.name);if(!m.matches())continue;
            int index=Integer.parseInt(m.group(1));Json.check(index<64,"加载图片最多 64 张");
            for(String line:s.lines){String[] kv=line.split("=",2);if(kv.length==2&&kv[0].trim().equalsIgnoreCase("filename"))images.put(index,TextureDiag.resolve(ini.getParentFile(),kv[1].trim()));}
        }
        Json.check(!images.isEmpty(),"加载图片列表为空");boolean hide=Pattern.compile("(?im)^\\s*(?:global\\s+)?\\$skip_ui\\s*=\\s*1\\s*(?:;.*)?$").matcher(ModConfig.source(ini)).find();
        File parent=dest.getAbsoluteFile().getParentFile();Json.check(parent.isDirectory()||parent.mkdirs(),"无法创建模型目录");File tmp=Files.createTempDirectory(parent.toPath(),".loading-import-").toFile();
        try{new File(tmp,"images").mkdirs();List<Object> rows=new ArrayList<>();long total=0;
            for(File f:images.values()){
                Json.check(f.isFile()&&f.length()<=64L<<20,"加载图片不存在或过大："+f.getName());String ext=f.getName().toLowerCase(Locale.ROOT);Json.check(ext.endsWith(".dds")||ext.endsWith(".png"),"加载图片仅支持 DDS / PNG");
                byte[] raw=decoder.decode(Files.readAllBytes(f.toPath()));Json.check(raw.length>=8,"图片解码返回值无效");ByteBuffer b=ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);int w=b.getInt(),h=b.getInt();
                Json.check(w>0&&h>0&&w<=2048&&h<=2048&&raw.length==8+4L*w*h,"加载图片尺寸无效或超过 2048");
                byte[] pixels=new byte[raw.length-8];for(int y=0;y<h;y++)System.arraycopy(raw,8+y*w*4,pixels,(h-1-y)*w*4,w*4);
                total+=pixels.length;Json.check(total<=256L<<20,"加载图片总计超过 256 MiB");String file="images/"+rows.size()+".rgba";Files.write(new File(tmp,file).toPath(),pixels);rows.add(Json.of("file",file,"width",w,"height",h));
                log.append("加载图片：").append(f.getName()).append(" → ").append(w).append('x').append(h).append('\n');
            }
            Map<String,Object> doc=Json.of("format","gibox-loading-images","version",1,"name",dest.getName(),"generation","l"+System.currentTimeMillis(),"images",rows,"hideUi",hide,"source",Json.of("path",mod.getAbsolutePath(),"signature",ModLibrary.signature(mod)));
            Files.write(new File(tmp,FILE).toPath(),Json.write(doc).getBytes(StandardCharsets.UTF_8));Files.write(new File(tmp,"conversion.log").toPath(),log.toString().getBytes(StandardCharsets.UTF_8));
            ModelConverter.commit(tmp,dest,true);Importer.Result result=new Importer.Result();result.id=Characters.LOADING;result.model=dest;result.summary="加载图片："+rows.size()+" 张，每次加载随机切换；在 MOD 详情中可切换是否隐藏加载提示";return result;
        }finally{if(tmp.exists())ModelConverter.remove(tmp);}
    }
    static ModelManifest summary(File dir)throws Exception{
        Map<String,Object> j=Json.obj(Json.read(new File(dir,FILE),1L<<20),FILE);Json.check("gibox-loading-images".equals(j.get("format"))&&Json.integer(j.get("version"),"version",1,1)==1,"加载图片格式无效");
        for(Object row:Json.arr(j.get("images"),"images",1,64)){Map<String,Object> r=Json.obj(row,"image");Json.check(Json.str(r.get("file"),"file",64).matches("images/[0-9]+\\.rgba"),"加载图片路径无效");Json.integer(r.get("width"),"width",1,2048);Json.integer(r.get("height"),"height",1,2048);}
        ModelManifest m=new ModelManifest();m.id=Characters.LOADING;m.version=2;m.name=Json.str(j.get("name"),"name",512);m.generation=Json.str(j.get("generation"),"generation",64);
        m.vars.put("$skip_ui",Arrays.asList(0,1));m.defaults.put("$skip_ui",Boolean.TRUE.equals(j.get("hideUi"))?1:0);
        if(j.get("source") instanceof Map){Map<String,Object> s=Json.obj(j.get("source"),"source");m.sourcePath=Json.str(s.get("path"),"source.path",4096);m.sourceSignature=Json.str(s.get("signature"),"source.signature",128);}return m;
    }
}
