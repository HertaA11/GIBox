package com.gibox.tool;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.RandomAccessFile;
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
 * The file is one record per line: the header, the meshes, a line opening the prefabs, the prefabs. Only the prefab
 * index is kept in memory; a mesh's line is read from the file (by its offset) when a prefab asks for it. A character
 * is found by its internal name through an index made once (1.0.4: matching every prefab name per character on the
 * App's main thread stalled the character list for seconds).
 */
final class OfflineSkeletons {
    static final String FILE = "offline-skeletons.json";
    final String build;
    private final long modified;
    private final File file;
    private long[] meshAt = new long[1024]; private int[] meshLength = new int[1024]; private int meshCount;
    private final Map<String, List<String>> byName = new HashMap<>();      // character internal name -> lower-case Avatar_<body>_<weapon>_<name> owners
    private Set<Integer> characters;
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

    /** One line of the file and the byte offset it starts at. */
    private static final class Lines {
        private final BufferedInputStream in; private final ByteArrayOutputStream buf = new ByteArrayOutputStream(1 << 16); long at, start;
        Lines(File f) throws Exception { in = new BufferedInputStream(new FileInputStream(f), 1 << 16); }
        String next() throws Exception {
            buf.reset(); start = at; int c;
            while ((c = in.read()) >= 0) { at++; if (c == '\n') break; buf.write(c); }
            if (c < 0 && buf.size() == 0) return null;
            return new String(buf.toByteArray(), StandardCharsets.UTF_8);
        }
        void close() throws Exception { in.close(); }
    }

    private OfflineSkeletons(File f) throws Exception {
        modified = f.lastModified(); file = f;
        Lines in = new Lines(f);
        try {
            String head = in.next();
            if (head != null && head.startsWith("\uFEFF")) head = head.substring(1);
            Json.check(head != null && head.startsWith("{\"format\":\"gibox-offline-skeletons\",\"version\":1,") && head.endsWith("\"meshes\":["), "不是 GIBox 离线骨骼表");
            Object b = ((Map<?, ?>) Json.parse(head + "]}")).get("build");
            build = b instanceof String ? (String) b : "";
            boolean inPrefabs = false;
            for (String line; (line = in.next()) != null; ) {
                long start = in.start; int bytes = (int) (in.at - in.start);
                line = line.trim();
                if (line.isEmpty()) continue;
                if (line.equals("],\"prefabs\":[")) { inPrefabs = true; continue; }
                if (line.equals("]}")) break;
                if (line.endsWith(",")) line = line.substring(0, line.length() - 1);
                if (!inPrefabs) {
                    if (meshCount == meshAt.length) { meshAt = Arrays.copyOf(meshAt, meshCount * 2); meshLength = Arrays.copyOf(meshLength, meshCount * 2); }
                    meshAt[meshCount] = start; meshLength[meshCount++] = bytes; continue;
                }
                Map<String, Object> p = Json.obj(Json.parse(line), "prefab");
                String owner = Json.str(p.get("owner"), "owner", 256);
                List<Object[]> rs = new ArrayList<>();
                for (Object r : Json.arr(p.get("renderers"), "renderers", 1, 256)) {
                    List<Object> x = Json.arr(r, "renderer", 2, 2);
                    rs.add(new Object[]{Json.str(x.get(0), "renderer", 256), (int) Json.integer(x.get(1), "mesh", 0, Integer.MAX_VALUE)});
                }
                String key = owner.toLowerCase(Locale.ROOT);
                prefabs.put(key, rs); owners.put(key, owner);
                String[] parts = key.split("_");
                if (parts.length == 4 && parts[0].equals("avatar")) byName.computeIfAbsent(parts[3], k -> new ArrayList<>()).add(key);
            }
        } finally { in.close(); }
    }

    /** A mesh record, read from its line in the file. */
    private Map<String, Object> mesh(int index) throws Exception {
        Json.check(index >= 0 && index < meshCount, "离线骨骼表的网格序号无效：" + index);
        byte[] b = new byte[meshLength[index]];
        try (RandomAccessFile r = new RandomAccessFile(file, "r")) { r.seek(meshAt[index]); r.readFully(b); }
        String line = new String(b, StandardCharsets.UTF_8).trim();
        if (line.endsWith(",")) line = line.substring(0, line.length() - 1);
        return Json.obj(Json.parse(line), "mesh");
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
            for (String k : byName.getOrDefault(model.toLowerCase(Locale.ROOT), Collections.emptyList())) {
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
                if (index >= meshCount || !seen.add(name.toLowerCase(Locale.ROOT)) || renderers.size() >= 64) continue;
                Map<String, Object> m = mesh(index);
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
    synchronized Set<Integer> characters() {
        if (characters != null) return characters;
        Set<Integer> out = new TreeSet<>();
        for (Characters.Avatar a : Characters.all()) if (prefabFor(a.id, null) != null) out.add(a.id);
        Outfits table = Outfits.current();
        for (Characters.Avatar a : Characters.all()) for (Outfits.Outfit o : table.of(a.id)) if (has(o.prefab)) out.add(a.id);
        return characters = Collections.unmodifiableSet(out);
    }
}
