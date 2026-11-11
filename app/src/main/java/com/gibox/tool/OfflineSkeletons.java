package com.gibox.tool;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.*;

/**
 * The skeleton table made from the game's own files (tools/make_offline_skeletons.py, assets/offline-skeletons.json,
 * installed in the shared folder): per skinned mesh its bone names, parents and bind poses, and per prefab
 * (Avatar_Girl_Catalyst_Citlali, NPC_..., Monster_..., Flycloak_...) the record of each renderer. Against the 35 bone
 * libraries recorded on a device every renderer was the same, bit for bit, so a conversion takes it first (1.0.4): a
 * character or an outfit converts without having been shown in the game. A prefab the table lacks (a character newer
 * than the table) falls back to the library the game records.
 *
 * The file is one record per line: the header, the meshes, a line opening the prefabs, the prefabs. The meshes stay
 * as text until a prefab asks for them.
 */
final class OfflineSkeletons {
    static final String FILE = "offline-skeletons.json";
    final String build;
    private final long modified;
    private final List<String> meshes = new ArrayList<>();
    private final Map<String, List<Object[]>> prefabs = new HashMap<>();   // lower-case owner -> [renderer, mesh index]
    private final Map<String, String> owners = new HashMap<>();            // lower-case owner -> as written

    private static File cachedFile; private static long cachedStamp; private static OfflineSkeletons cached;

    /** The table in the shared folder, or null when it is not there or cannot be read. */
    static synchronized OfflineSkeletons load(File sharedDir) {
        File f = new File(sharedDir, FILE);
        if (!f.isFile()) return null;
        long stamp = f.lastModified() * 31 + f.length();
        if (f.equals(cachedFile) && stamp == cachedStamp) return cached;
        try { cached = new OfflineSkeletons(f); } catch (Exception e) { cached = null; }
        cachedFile = f; cachedStamp = stamp;
        return cached;
    }

    private OfflineSkeletons(File f) throws Exception {
        modified = f.lastModified();
        try (BufferedReader in = new BufferedReader(new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8), 1 << 16)) {
            String head = in.readLine();
            Json.check(head != null && head.startsWith("{\"format\":\"gibox-offline-skeletons\",\"version\":1,") && head.endsWith("\"meshes\":["), "不是 GIBox 离线骨骼表");
            Object b = ((Map<?, ?>) Json.parse(head + "]}")).get("build");
            build = b instanceof String ? (String) b : "";
            boolean inPrefabs = false;
            for (String line; (line = in.readLine()) != null; ) {
                line = line.trim();
                if (line.isEmpty()) continue;
                if (line.equals("],\"prefabs\":[")) { inPrefabs = true; continue; }
                if (line.equals("]}")) break;
                if (line.endsWith(",")) line = line.substring(0, line.length() - 1);
                if (!inPrefabs) { meshes.add(line); continue; }
                Map<String, Object> p = Json.obj(Json.parse(line), "prefab");
                String owner = Json.str(p.get("owner"), "owner", 256);
                List<Object[]> rs = new ArrayList<>();
                for (Object r : Json.arr(p.get("renderers"), "renderers", 1, 256)) {
                    List<Object> x = Json.arr(r, "renderer", 2, 2);
                    rs.add(new Object[]{Json.str(x.get(0), "renderer", 256), (int) Json.integer(x.get(1), "mesh", 0, Integer.MAX_VALUE)});
                }
                prefabs.put(owner.toLowerCase(Locale.ROOT), rs); owners.put(owner.toLowerCase(Locale.ROOT), owner);
            }
        }
    }

    boolean has(String owner) { return owner != null && prefabs.containsKey(owner.toLowerCase(Locale.ROOT)); }

    /**
     * A character's prefab in the table: the outfit's prefab when one is chosen (avatar_girl_catalyst_citlalicostumexia),
     * else the one Avatar_&lt;body&gt;_&lt;weapon&gt;_&lt;name&gt; for the character's internal name; null when the table has none.
     */
    String prefabFor(int id, String outfitPrefab) {
        if (outfitPrefab != null && !outfitPrefab.isEmpty()) return has(outfitPrefab) ? owners.get(outfitPrefab.toLowerCase(Locale.ROOT)) : null;
        Characters.Avatar a = Characters.byId(id);
        if (a == null) return null;
        String found = null;
        for (String model : a.model)
            for (String k : prefabs.keySet())
                if (k.matches("avatar_[a-z]+_[a-z]+_" + java.util.regex.Pattern.quote(model.toLowerCase(Locale.ROOT)))) {
                    if (found != null && !found.equals(k)) return null;   // two prefabs answer: neither
                    found = k;
                }
        return found == null ? null : owners.get(found);
    }

    /**
     * The prefab's renderers as a bone library (skeleton.json, the format the game records) in dir/&lt;prefab&gt;: written
     * when missing or older than the table. Renderers whose mesh lacks a submesh count are written without a target.
     */
    ModLibrary.Character library(String owner, int id, File dir) throws Exception {
        List<Object[]> rs = prefabs.get(owner.toLowerCase(Locale.ROOT));
        Json.check(rs != null, "离线骨骼表里没有 " + owner);
        File out = new File(dir, owner.toLowerCase(Locale.ROOT)), file = new File(out, "skeleton.json");
        String root = "Offline/" + owner + "(Clone)/" + owner;
        if (!file.isFile() || file.lastModified() < modified) {
            List<Object> renderers = new ArrayList<>(); Set<String> seen = new HashSet<>();
            for (Object[] r : rs) {
                String name = (String) r[0]; int index = (Integer) r[1];
                if (index >= meshes.size() || !seen.add(name.toLowerCase(Locale.ROOT)) || renderers.size() >= 64) continue;
                Map<String, Object> m = Json.obj(Json.parse(meshes.get(index)), "mesh");
                List<Object> bones = Json.arr(m.get("bones"), "bones", 1, 512);
                Map<String, Object> rec = new LinkedHashMap<>();
                rec.put("name", name);
                if (m.get("submeshes") instanceof Number)
                    rec.put("target", Json.of("renderer", name, "mesh", m.get("mesh"), "vertices", m.get("vertices"), "submeshes", m.get("submeshes"), "bones", bones.size()));
                rec.put("bones", bones); rec.put("parents", m.get("parents")); rec.put("bindposes", m.get("bindposes"));
                renderers.add(rec);
            }
            Json.check(!renderers.isEmpty(), "离线骨骼表里 " + owner + " 没有可用的渲染器");
            Map<String, Object> j = Json.of("format", "gibox-skeleton", "version", 1, "id", id, "root", root, "source", "offline", "build", build, "renderers", renderers);
            Json.check(out.isDirectory() || out.mkdirs(), "无法创建离线骨骼目录：" + out);
            File tmp = new File(out, "skeleton.json.tmp");
            try (FileOutputStream o = new FileOutputStream(tmp)) { o.write(Json.write(j).getBytes(StandardCharsets.UTF_8)); o.getFD().sync(); }
            Json.check(tmp.renameTo(file), "无法写入离线骨骼记录：" + file);
        }
        return new ModLibrary.Character(id, out, root, "");
    }

    /** The characters (ids) the table has a prefab for: their original look, or one of their outfits. */
    Set<Integer> characters() {
        Set<Integer> out = new TreeSet<>();
        for (Characters.Avatar a : Characters.all()) if (prefabFor(a.id, null) != null) out.add(a.id);
        Outfits table = Outfits.current();
        for (Characters.Avatar a : Characters.all()) for (Outfits.Outfit o : table.of(a.id)) if (has(o.prefab)) out.add(a.id);
        return out;
    }
}
