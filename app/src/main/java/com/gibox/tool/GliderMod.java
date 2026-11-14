package com.gibox.tool;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.regex.*;

/** Signature Gliders' renderer-owner condition, translated rather than simulated as a global Present counter. */
final class GliderMod {
    private GliderMod() {}
    private static final Set<String> WINGS=new HashSet<>(Arrays.asList("FirstFlight","Companionship","Descension","AzureWind","GoldenFlight","ConsealingSnow","Feasting","Stormrider","ShimmeringGalaxy","Forest","StarlitFeast","MercifulWrathfulWaters","TransientDreams","SoaringFlame","Delicacies","FatesCourseIntertwined"));
    static File ini(File root){for(File f:ModConfig.allInis(root)){String text=ModConfig.source(f);if(text.toLowerCase(Locale.ROOT).contains("signaturegliders")&&text.contains("[TextureOverride")&&text.contains("Glider1024.dds"))return f;}return null;}
    private static String key(String s){return s.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9]","");}
    private static Map<String,String> values(ModelConverter.Section s){Map<String,String> out=new LinkedHashMap<>();for(String l:s.lines){int p=l.indexOf('=');if(p>=0)out.put(l.substring(0,p).trim().toLowerCase(Locale.ROOT),l.substring(p+1).trim());}return out;}
    static Importer.Result install(File root,File dest,File shared,ModelConverter.TextureDecoder decoder,StringBuilder log)throws Exception {
        Outfits outfits=Outfits.parse(new String(Files.readAllBytes(new File(shared,"outfits.json").toPath()),StandardCharsets.UTF_8));
        List<Object> rules=new ArrayList<>(),resources=new ArrayList<>();Set<String> owners=new HashSet<>();
        for(File file:ModConfig.allInis(root)){
            if(!ModConfig.source(file).toLowerCase(Locale.ROOT).contains("signaturegliders"))continue;
            ModelConverter.Ini ini=new ModelConverter.Ini(root,file);String active=null,character=null;ModelConverter.Section texture=null;
            for(ModelConverter.Section s:ini.sections.values()){
                if(s.name.toLowerCase(Locale.ROOT).matches("textureoverride.+position"))for(String line:s.lines){Matcher m=Pattern.compile("(\\$[A-Za-z0-9_]+Active)\\s*=\\s*2").matcher(line);if(m.matches()){Json.check(active==null,"风之翼角色检测段有歧义："+file);active=m.group(1).toLowerCase(Locale.ROOT);character=s.name.substring(15,s.name.length()-8);}}
            }
            Json.check(active!=null,"风之翼缺少可识别的角色检测："+file);
            // This family applies the same diffuse to all wings. Verify that invariant before dropping texture hashes.
            String resource=null;int modelRules=0;Set<String> wingNames=new HashSet<>();
            for(ModelConverter.Section s:ini.sections.values())if(s.name.toLowerCase(Locale.ROOT).startsWith("textureoverride")&&s.name.endsWith("1024")){
                Map<String,String> v=values(s);String ref=v.get("ps-t0");Json.check(ref!=null,"风之翼贴图绑定缺失："+s.name);
                String wing=s.name.substring(15,s.name.length()-4);Json.check(WINGS.contains(wing)&&wingNames.add(wing),"未知或重复风之翼："+s.name);
                boolean conditional=false,inside=false,bound=false;for(String line:s.lines){String cmd=line.toLowerCase(Locale.ROOT).replaceAll("\\s+","");
                    if(cmd.startsWith("if")){Json.check(!conditional&&cmd.equals("if"+active+">0"),"风之翼条件尚不支持："+s.name);conditional=true;inside=true;}
                    else if(cmd.equals("endif")){Json.check(inside,"风之翼条件不完整："+s.name);inside=false;}
                    else if(cmd.startsWith("ps-t0=")){Json.check(inside&&!bound,"风之翼条件尚不支持："+s.name);bound=true;}
                    else Json.check(cmd.matches("hash=[0-9a-f]{8}")||cmd.equals("match_priority=0")||cmd.equals("$"+wing.toLowerCase(Locale.ROOT)+"active=1"),"风之翼命令尚不支持："+line);
                }
                Json.check(conditional&&bound&&!inside,"风之翼条件尚不支持："+s.name);String normalized=ref.toLowerCase(Locale.ROOT);Json.check(resource==null||resource.equals(normalized),"同一角色按不同风之翼使用不同图片，尚不能忽略贴图 hash："+file);resource=normalized;modelRules++;
            }
            Json.check(modelRules==16,"风之翼规则不完整或格式不同："+file+"（预期 16 种，实际 "+modelRules+"）");
            texture=ini.sections.get(resource);Json.check(texture!=null,"风之翼贴图资源缺失："+resource);String filename=values(texture).get("filename");Json.check(filename!=null,"风之翼缺少图片文件："+resource);
            String pc=character;Map<String,String> aliases=new HashMap<>();aliases.put("Ayaka","KamisatoAyaka");aliases.put("AyakaSpringbloomMissive","AyakaSpringbloom");aliases.put("Aether","TravelerBoy");aliases.put("Lumine","TravelerGirl");pc=aliases.getOrDefault(pc,pc);
            Outfits.Outfit outfit=outfits.byPc(pc);int id=outfit==null?0:outfit.character;String prefab=outfit==null?"":outfit.prefab;
            int skin=character.equals("JeanSea")?200301:character.equals("HuTaoCherries")?204601:character.equals("NilouBreeze")?207001:0;
            if(skin!=0)for(Characters.Avatar a:Characters.all())for(Outfits.Outfit o:outfits.of(a.id))if(o.skin==skin){id=a.id;prefab=o.prefab;}
            if(id==0)for(Characters.Avatar a:Characters.all())for(String alias:a.aliases)if(key(alias).equals(key(character))){Json.check(id==0||id==a.id,"风之翼角色名有歧义："+character);id=a.id;}
            Json.check(id!=0,"无法确认风之翼角色或衣装："+character);
            String owner=id+":"+prefab;Json.check(owners.add(owner),"风之翼角色条件重复："+owner);
            File image=TextureDiag.resolve(file.getParentFile(),filename);String rn="resourceglider"+rules.size();
            resources.add(Json.of("name",rn,"file",root.getCanonicalFile().toPath().relativize(image.getCanonicalFile().toPath()).toString(),"exists",image.isFile()));
            rules.add(Json.of("section","GliderOwner"+character,"kind","glider-owner","ownerId",id,"ownerAvatar",prefab,"textures",Arrays.asList(Json.of("slot","ps-t0","role","diffuse","resource",rn,"condition",""))));
            log.append("风之翼角色：").append(character).append(" → ").append(id).append(prefab.isEmpty()?"": " / "+prefab).append('\n');
        }
        Json.check(!rules.isEmpty(),"未找到风之翼角色规则");
        Map<String,Object> doc=Json.of("format","gibox-texture-diag","version",1,"name",dest.getName(),"ini","SignatureGliders","character",Characters.SCENE,"rules",rules,"resources",resources,"variables",Collections.emptyList());
        log.append("风之翼：按实际所属角色和衣装匹配，不模拟全局 Present 计数；图鉴图标暂不替换。图片在游戏中按需读取，未展开角色不预载。\n");
        ModelConverter.Options opt=new ModelConverter.Options().replace(true).source(root.getAbsolutePath(),ModLibrary.signature(root));
        Importer.Result result=new Importer.Result();result.id=Characters.SCENE;result.model=TextureMod.installDocument(root,Characters.SCENE,dest,opt,decoder,log,doc);result.summary="按风之翼所属角色和衣装选择贴图；图鉴图标暂不替换";return result;
    }
}
