package com.gibox.tool;

import java.io.File;
import java.util.*;

/**
 * The index-buffer hashes this device's game uploads, tied to the mesh that showed them: the map shipped with the App
 * (GIBox/device-hashes.json) and this device's own captures (GIBox/hash-map.json, which wins). An IB hash here is
 * byte for byte the one 3DMigoto computes on PC for the same mesh, so a mod's TextureOverride hash names the game's
 * renderer and character directly, whatever the mod calls its parts ("Hood" for Alyosha's Hat_Off).
 */
public final class DeviceHashes {
    public static final class Mesh {
        public final String ib, avatar, renderer;
        Mesh(String ib, String avatar, String renderer) { this.ib = ib; this.avatar = avatar; this.renderer = renderer; }
    }
    private final Map<String, Mesh> byIb = new HashMap<>();
    private final Map<String, Set<Integer>> pcOwners = new HashMap<>();

    public static DeviceHashes load(File sharedDir) {
        DeviceHashes d = new DeviceHashes();
        for (String f : new String[]{"device-hashes.json", "hash-map.json"}) {   // this device's own map wins
            try { Map<String, Object> j = Json.obj(Json.read(new File(sharedDir, f), 64L << 20), f);
                for (Object m : Json.arr(j.get("meshes"), "meshes", 0, 65536)) { Map<String, Object> e = Json.obj(m, "mesh");
                    if (!(e.get("ib") instanceof String) || !(e.get("avatar") instanceof String) || ((String) e.get("avatar")).isEmpty()) continue;
                    String renderer = e.get("renderer") instanceof String ? (String) e.get("renderer") : "", mesh = e.get("mesh") instanceof String ? (String) e.get("mesh") : "";
                    if (renderer.isEmpty()) continue;
                    // The renderer as the game names it: the mesh's spelling when it is the same name (Hat_Off), else as captured.
                    String name = mesh.toLowerCase(Locale.ROOT).equals(renderer.toLowerCase(Locale.ROOT)) ? mesh : renderer;
                    String ib = ((String) e.get("ib")).toLowerCase(Locale.ROOT);
                    d.byIb.put(ib, new Mesh(ib, ((String) e.get("avatar")).toLowerCase(Locale.ROOT), name)); } }
            catch (Exception ignored) {}
        }
        try { Map<String, Object> j = Json.obj(Json.read(new File(sharedDir, "pc-hashes.json"), 64L << 20), "pc-hashes.json");
            for (Object c : Json.arr(j.get("characters"), "characters", 0, 1024)) { Map<String, Object> ch = Json.obj(c, "character");
                int id = ch.get("avatar") instanceof Number ? ((Number) ch.get("avatar")).intValue() : 0; if (id < 10000000 || id > 10001999) continue;
                if (ch.get("components") instanceof List) for (Object p : (List<?>) ch.get("components")) { Object ib = p instanceof Map ? ((Map<?, ?>) p).get("ib") : null;
                    if (ib instanceof String && !((String) ib).isEmpty()) d.pcOwners.computeIfAbsent(((String) ib).toLowerCase(Locale.ROOT), k -> new TreeSet<>()).add(id); } } }
        catch (Exception ignored) {}
        return d;
    }

    public Mesh mesh(String ib) { return ib == null ? null : byIb.get(ib.toLowerCase(Locale.ROOT)); }

    /** The character a captured avatar prefab is: an outfit the table knows, else the character whose model name is
     *  one of the prefab's words (avatar_boy_pole_alyosha). 0 when none or more than one. */
    public static int character(String avatar) {
        Outfits.Outfit o = Outfits.current().byPrefab(avatar); if (o != null) return o.character;
        String base = avatar.contains("costume") ? avatar.substring(0, avatar.indexOf("costume")) : avatar;
        List<String> words = Arrays.asList(base.split("_")); int found = 0;
        for (Characters.Avatar a : Characters.all()) for (String m : a.model) if (words.contains(m.toLowerCase(Locale.ROOT))) { if (found != 0 && found != a.id) return 0; found = a.id; }
        return found;
    }
    /** Whether a captured prefab is this character's: the outfit's own prefab when one is named, else any of its prefabs. */
    static boolean belongs(String avatar, int id, String outfit) {
        if (outfit != null && !outfit.isEmpty()) return avatar.equals(outfit.toLowerCase(Locale.ROOT));
        return character(avatar) == id;
    }

    /** IB hash -> the renderer it is on, for this character (and outfit, when named). */
    public Map<String, String> renderers(int id, String outfit) {
        Map<String, String> out = new TreeMap<>();
        for (Mesh m : byIb.values()) if (belongs(m.avatar, id, outfit)) out.put(m.ib, m.renderer);
        return out;
    }

    /** The characters a mod's hashes name: this device's captures (by the prefab) and the PC database (by AvatarId);
     *  a hash naming more than one character says nothing. Character ID -> the hashes. */
    public Map<Integer, List<String>> characters(Collection<String> hashes) {
        Map<Integer, List<String>> out = new TreeMap<>();
        for (String h : new TreeSet<>(hashes)) {
            Set<Integer> who = new TreeSet<>();
            Mesh m = mesh(h); if (m != null) { int c = character(m.avatar); if (c != 0) who.add(c); }
            Set<Integer> pc = pcOwners.get(h.toLowerCase(Locale.ROOT)); if (pc != null) who.addAll(pc);
            if (who.size() == 1) out.computeIfAbsent(who.iterator().next(), k -> new ArrayList<>()).add(h);
        }
        return out;
    }
}
