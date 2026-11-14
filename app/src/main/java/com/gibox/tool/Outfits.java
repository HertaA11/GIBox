package com.gibox.tool;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

/**
 * GIBox's own table of each character's outfits (assets/outfits.json, tools/make_outfits.py): the original look and every
 * skin, with the game's skin ID, the prefab the runtime reads from a renderer ("avatar_lady_pole_shenhecostumedai"), the
 * Chinese and English names and the GI-Model-Importer-Assets names of the same outfit. A skin is its own model with its
 * own bone list: a mod is converted against its outfit's bone library (skeletons/<id>/<prefab> for a skin) and the
 * runtime shows it only on that outfit. Characters without skins are not listed: their only outfit is the original.
 */
public final class Outfits {
    public static final class Outfit {
        public final int character, skin; public final String prefab, name, en; public final List<String> pc;
        Outfit(int character, int skin, String prefab, String name, String en, List<String> pc) { this.character = character; this.skin = skin; this.prefab = prefab; this.name = name; this.en = en; this.pc = pc; }
        public boolean original() { return !costume(prefab); }
        /** "冷花幽露", or "原皮" for the original look. */
        @Override public String toString() { return name; }
    }

    private final Map<Integer, List<Outfit>> byCharacter = new TreeMap<>();
    private final Map<String, Outfit> byPrefab = new HashMap<>(), byPc = new HashMap<>();

    /** The table in use: the App installs the one it ships (AppState), the host tests the asset file. */
    private static volatile Outfits current = new Outfits();
    public static Outfits current() { return current; }
    public static void use(Outfits t) { current = t == null ? new Outfits() : t; }

    public static Outfits parse(String text) {
        Map<String, Object> j = Json.obj(Json.parse(text), "outfits.json");
        Json.check("gibox-outfits".equals(j.get("format")) && Json.integer(j.get("version"), "version", 1, 1) == 1, "不是 GIBox 外观对照表");
        Outfits t = new Outfits();
        for (Object c : Json.arr(j.get("characters"), "characters", 0, 4096)) {
            Map<String, Object> ch = Json.obj(c, "character"); int id = (int) Json.integer(ch.get("id"), "id", 10000000, 10001999); List<Outfit> list = new ArrayList<>();
            for (Object o : Json.arr(ch.get("outfits"), "outfits", 2, 64)) {
                Map<String, Object> x = Json.obj(o, "outfit"); String prefab = (String) x.get("prefab");
                Json.check(prefab != null && prefab.startsWith("avatar_") && prefab.matches("[a-z0-9_]{1,128}"), "外观对照表：prefab " + prefab);
                List<String> pc = new ArrayList<>(); if (x.get("pc") instanceof List) for (Object p : (List<?>) x.get("pc")) if (p instanceof String) pc.add((String) p);
                Outfit out = new Outfit(id, (int) Json.integer(x.get("skin"), "skin", 0, 99999999), prefab, String.valueOf(x.get("name")), String.valueOf(x.get("en")), Collections.unmodifiableList(pc));
                list.add(out); t.byPrefab.put(prefab, out); for (String p : pc) t.byPc.put(p, out);
            }
            Json.check(list.get(0).original(), "外观对照表：" + id + " 的第一套不是原皮");
            t.byCharacter.put(id, Collections.unmodifiableList(list));
        }
        return t;
    }
    public static Outfits load(File f) throws Exception { return parse(new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8)); }

    /** The outfits of a character, the original first; empty when it has no skins. */
    public List<Outfit> of(int character) { List<Outfit> l = byCharacter.get(character); return l == null ? Collections.emptyList() : l; }
    public Outfit byPrefab(String prefab) { return prefab == null ? null : byPrefab.get(prefab); }
    public Outfit byPc(String name) { return byPc.get(name); }
    public Outfit original(int character) { List<Outfit> l = of(character); return l.isEmpty() ? null : l.get(0); }
    /** The outfit's name for a prefab ("冷花幽露"); the prefab itself when the table does not know it, "" for none. */
    public String label(String prefab) { if (prefab == null || prefab.isEmpty()) return ""; Outfit o = byPrefab(prefab); return o == null ? prefab : o.name; }

    // ---------------------------------------------------------------- bone library folders (as runtime.cpp libraryDirOf)
    /** Every skin prefab is the original's with "costume<name>" appended. */
    public static boolean costume(String prefab) { return prefab != null && prefab.contains("costume"); }
    /** skeletons/<id> for the original look (or no outfit), skeletons/<id>/<prefab> for a skin. */
    public static File libraryDir(File boneRoot, int id, String prefab) {
        File main = new File(boneRoot, Integer.toString(id));
        return costume(prefab) && prefab.matches("[a-z0-9_]{1,128}") ? new File(main, prefab) : main;
    }
    /** The avatar a skeleton record was taken from ("root", the transform chain down to the first bone): the nearest
     *  "Avatar_" node above the bone, lower case, "_manekin" dropped. "" when none. */
    public static String chainAvatar(String root) {
        if (root == null) return "";
        String[] parts = root.split("/");
        for (int i = parts.length - 1; i >= 0; i--) {
            String n = parts[i].toLowerCase(Locale.ROOT).replace("(clone)", "").trim();
            if (n.startsWith("avatar_")) { int m = n.indexOf("_manekin"); return m < 0 ? n : n.substring(0, m); }
        }
        return "";
    }

    // ---------------------------------------------------------------- which outfit a mod is for
    /** What the INI's hashes say: the outfit (null: none of them names one of this character's outfits), how it was
     *  found, and outfits the hashes contradict each other on. */
    public static final class Guess {
        public Outfit outfit; public String how = ""; public final Set<String> conflict = new TreeSet<>();
    }
    /**
     * The outfit of `character` a mod's TextureOverride hashes name. An index-buffer hash a capture tied to a prefab
     * (hash-map.json on this device, device-hashes.json shipped) names it directly; otherwise the PC hash database's
     * component with that index buffer names a PC character, which this table places. A hash two outfits share in the
     * PC data (JeanCN, XingqiuBamboo carry the original's) decides nothing.
     */
    public Guess guess(int character, Collection<String> hashes, File sharedDir) {
        Guess g = new Guess(); if (of(character).isEmpty() || hashes.isEmpty()) return g;
        // The hash tables as DeviceHashes holds them (read once per file change; this device's own map wins there too).
        DeviceHashes tables = DeviceHashes.load(sharedDir);
        Map<String, String> device = new HashMap<>();
        for (String h : hashes) { DeviceHashes.Mesh m = tables.mesh(h); if (m != null) device.put(h, m.avatar); }
        Map<String, Set<String>> pc = new HashMap<>();
        try { Map<String, Object> j = Json.obj(Json.read(new File(sharedDir, "pc-hashes.json"), 64L << 20), "pc-hashes.json");
            for (Object c : Json.arr(j.get("characters"), "characters", 0, 1024)) { Map<String, Object> ch = Json.obj(c, "character");
                if (ch.get("components") instanceof List) for (Object p : (List<?>) ch.get("components")) { Object ib = p instanceof Map ? ((Map<?, ?>) p).get("ib") : null;
                    if (ib instanceof String && !((String) ib).isEmpty()) pc.computeIfAbsent(((String) ib).toLowerCase(Locale.ROOT), k -> new TreeSet<>()).add(String.valueOf(ch.get("name"))); } } }
        catch (Exception ignored) {}
        Map<Outfit, List<String>> votes = new LinkedHashMap<>();
        for (String h : hashes) {
            Outfit o = null; String why = null;
            Outfit d = byPrefab(device.get(h));
            if (d != null && d.character == character) { o = d; why = h + "（手机哈希表：" + d.prefab + "）"; }
            else if (pc.containsKey(h)) {
                Set<Outfit> named = new LinkedHashSet<>(); for (String n : pc.get(h)) { Outfit x = byPc(n); if (x != null && x.character == character) named.add(x); }
                if (named.size() == 1) { o = named.iterator().next(); why = h + "（PC " + String.join("/", pc.get(h)) + "）"; }
            }
            if (o != null) votes.computeIfAbsent(o, k -> new ArrayList<>()).add(why);
        }
        if (votes.size() == 1) { Map.Entry<Outfit, List<String>> e = votes.entrySet().iterator().next(); g.outfit = e.getKey(); g.how = String.join("，", e.getValue()); }
        else for (Outfit o : votes.keySet()) g.conflict.add(o.name);
        return g;
    }
}
