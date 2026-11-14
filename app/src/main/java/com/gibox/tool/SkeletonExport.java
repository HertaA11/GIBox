package com.gibox.tool;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

/** A user's bone libraries as a contribution in exactly the bundled offline table's format. */
final class SkeletonExport {
    static File write(File shared, int id) throws Exception {
        File root = new File(shared, "skeletons/" + id);
        List<File> sources = new ArrayList<>();
        collect(root, sources);
        // Export the game capture when present; otherwise the built-in original and each available outfit.
        if (sources.isEmpty()) {
            OfflineSkeletons table = OfflineSkeletons.load(shared);
            if (table != null) {
                Set<String> owners = new LinkedHashSet<>();
                String original = table.prefabFor(id, null); if (original != null) owners.add(original);
                for (Outfits.Outfit o : Outfits.current().of(id)) { String p = table.prefabFor(id, o.prefab); if (p != null) owners.add(p); }
                for (String owner : owners) sources.add(new File(table.library(owner, id, new File(shared, "skeletons-offline")).bones, "skeleton.json"));
            }
        }
        Json.check(!sources.isEmpty(), "暂无骨骼。请打开游戏图鉴中的该角色，停留数秒，采集完成后再导出。");
        List<Object> meshes = new ArrayList<>(), prefabs = new ArrayList<>(); Set<String> seen = new HashSet<>();
        String build = "";
        for (File source : sources) {
            ModelConverter.bones(source, id); // Validate lengths, finite floats, parents, full-detail meshes.
            Map<String,Object> j = Json.obj(Json.read(source, 64L << 20), "skeleton.json");
            String owner = Outfits.chainAvatar(String.valueOf(j.get("root")));
            Json.check(!owner.isEmpty(), "骨骼记录缺少所属预制体：" + source);
            Json.check(seen.add(owner), "同一预制体有多份记录，请先核对：" + owner);
            String b = j.get("build") instanceof String ? (String) j.get("build") : "";
            if (b.isEmpty()) { OfflineSkeletons t = OfflineSkeletons.load(shared); if ("offline".equals(j.get("source")) && t != null) b = t.build; }
            Json.check(!b.isEmpty(), "这份骨骼记录没有游戏 Build ID，请先点重采骨骼，再导出供补充内置表");
            Json.check(build.isEmpty() || b.isEmpty() || build.equals(b), "记录来自不同游戏版本，请分开导出");
            if (!b.isEmpty()) build = b;
            List<Object> refs = new ArrayList<>();
            for (Object raw : Json.arr(j.get("renderers"), "renderers", 1, 64)) {
                Map<String,Object> r = Json.obj(raw, "renderer");
                Map<String,Object> target = Json.obj(r.get("target"), "target（请先重新采集以补全网格信息）");
                int index = meshes.size();
                meshes.add(Json.of("mesh_id", "capture:" + owner + ":" + index, "name", r.get("name"), "mesh", target.get("mesh"),
                    "vertices", target.get("vertices"), "submeshes", target.get("submeshes"), "kinds", Collections.singletonList("character"),
                    "owner_count", 1, "bones", r.get("bones"), "parents", r.get("parents"), "bindposes", r.get("bindposes")));
                refs.add(Arrays.asList(r.get("name"), index));
            }
            prefabs.add(Json.of("owner", owner, "kind", "character", "renderers", refs));
        }
        StringBuilder body = new StringBuilder("{\"format\":\"gibox-offline-skeletons\",\"version\":1,\"build\":")
            .append(Json.write(build).trim()).append(",\"meshes\":[\n");
        append(body, meshes); body.append("],\"prefabs\":[\n"); append(body, prefabs); body.append("]}\n");
        File dir = new File(shared, "skeleton-exports"); Json.check(dir.isDirectory() || dir.mkdirs(), "无法创建导出目录");
        File dest = new File(dir, id + "-" + System.currentTimeMillis() + ".json"), tmp = new File(dir, dest.getName() + ".tmp");
        Files.write(tmp.toPath(), body.toString().getBytes(StandardCharsets.UTF_8));
        Json.check(tmp.renameTo(dest), "导出写入失败"); return dest;
    }
    private static void append(StringBuilder s, List<Object> entries) { for (int i=0;i<entries.size();i++) s.append(compact(entries.get(i))).append(i+1<entries.size()?",\n":"\n"); }
    private static String compact(Object value) {
        String raw=Json.write(value); StringBuilder out=new StringBuilder(); boolean quoted=false, escaped=false;
        for(int i=0;i<raw.length();i++) { char c=raw.charAt(i);
            if(quoted) { out.append(c); if(escaped) escaped=false; else if(c=='\\') escaped=true; else if(c=='"') quoted=false; }
            else if(c=='"') { quoted=true; out.append(c); } else if(!Character.isWhitespace(c)) out.append(c);
        }
        return out.toString();
    }
    private static void collect(File dir, List<File> out) {
        File own = new File(dir, "skeleton.json"); if (own.isFile()) out.add(own);
        File[] children=dir.listFiles(); if(children!=null) for(File d:children) if(d.isDirectory()&&!d.getName().startsWith(".")) collect(d,out);
    }
}
