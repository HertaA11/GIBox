package com.gibox.tool;

import java.io.File;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * What the app needs from a GIBox model folder's model.json (format "gibox-model", versions 1–4; the full check of the
 * data is the runtime's, model.hpp): the character, the variants, where it was converted from. learned.json adds the
 * character ID the runtime found for a model converted by name.
 */
public final class ModelManifest {
    public int id;
    /** Character ID found in game for a model converted by name (learned.json), else 0. */
    public int learnedId;
    public final List<String> names = new ArrayList<>();
    public String name = "", generation = "", sourcePath = "", sourceSignature = "";
    public final Map<String, List<Integer>> vars = new LinkedHashMap<>();
    public final List<Map<String, Integer>> combos = new ArrayList<>();
    public int defaultCombo;
    /** The value each variable starts with (version 2 "variants.default"; version 1: the default combination's). */
    public final Map<String, Integer> defaults = new LinkedHashMap<>();
    public int version;
    /** Version 3 "replaceWhen": the variables that switch the whole replacement on, and the values it is on for. Empty: always on. */
    public final List<String> replaceVars = new ArrayList<>();
    public final List<List<Integer>> replaceAllow = new ArrayList<>();
    /** A part's target is a lower level of detail (converted against a skeleton library recorded while the character was
     *  still loading, before 0.6.5): the bone indices do not fit the full model, it has to be converted again. Else "". */
    public String lodMesh = "";
    /** character.outfit (0.7.94): the outfit's prefab (Outfits), "" for a model made before outfits. */
    public String outfit = "";
    /** A [Key] of the model's INI logic the App can press (model.json "logic.keys", 0.7.37): with the logic core on, a
     *  press goes to the core as a key event. */
    public static final class Key {
        public int file; public String section = "", type = ""; public boolean back;
        /** Its key= values as the INI writes them (labels only: the App never reads the keyboard). */
        public final List<String> binds = new ArrayList<>();
        /** What each preset assigns (a cycle: one per position; the other types: one); empty for models before 0.7.73. */
        public final List<Preset> presets = new ArrayList<>();
        /** A preset may be chosen directly: every preset assigns something and none has a condition or a run. */
        public boolean selectable() { if (!type.equals("cycle") || presets.size() < 2) return false; for (Preset p : presets) if (p.special || p.values.isEmpty()) return false; return true; }
    }
    /** One preset of a [Key]: variable → value as written, and "special" when it has a condition or a run (or, outside a
     *  cycle, a value that is not a plain number): choosing it directly could differ from stepping to it, so it is only stepped. */
    public static final class Preset { public final Map<String, String> values = new LinkedHashMap<>(); public boolean special; }
    /** The logic data says the mod's own program picks the outfit (logic.driven): with the logic core on, the 款式 table
     *  is not its input. */
    public boolean driven;
    /** A texture-only mod (texture.json, TextureMod): no mesh; variants as in version 2. */
    public boolean textureMod;
    public final List<Key> keys = new ArrayList<>();

    /** A lower level of detail by mesh name (Body_LOD1, Body_LOD3 …); the plain name and _LOD0 are the full mesh (core.hpp lowerLod). */
    public static boolean lowerLod(String mesh) {
        if (mesh == null) return false;
        String o = mesh.toLowerCase(java.util.Locale.ROOT).replace("(clone)", "").replace(" instance", "").trim();
        int p = o.lastIndexOf("_lod"); if (p < 0 || p + 4 >= o.length()) return false;
        String d = o.substring(p + 4); return d.matches("[0-9]+") && !d.matches("0+");
    }

    public static boolean exists(File dir) { return new File(dir, "model.json").isFile() || TextureMod.is(dir); }

    public static ModelManifest read(File dir) throws Exception {
        if (TextureMod.is(dir)) return readTextureMod(dir);
        Map<String, Object> m = Json.obj(Json.read(new File(dir, "model.json"), 4L << 20), "model.json");
        Json.check("gibox-model".equals(m.get("format")), "不是 GIBox 模型：" + dir.getName());
        long version = Json.integer(m.get("version"), "version", 1, 1000000); Json.check(version >= 1 && version <= 4, "模型格式版本 " + version + " 不支持（新版 GIBox 转换的？）：" + dir.getName());
        ModelManifest r = new ModelManifest(); r.version = (int) version;
        r.name = Json.str(m.get("name"), "name", 512); r.generation = Json.str(m.get("generation"), "generation", 64);
        Map<String, Object> c = Json.obj(m.get("character"), "character");
        r.id = (int) Json.integer(c.get("id"), "character.id", 0, 10001999); Json.check(r.id == 0 || r.id >= 10000000, "角色 ID 越界");
        if (c.get("names") != null) for (Object n : Json.arr(c.get("names"), "character.names", 0, 16)) { String s = Json.str(n, "character.names", 40); Json.check(s.matches("[a-z0-9]{3,40}"), "角色名格式：" + s); r.names.add(s); }
        Json.check(r.id != 0 || !r.names.isEmpty(), "模型没有角色 ID 也没有角色名");
        if (c.get("outfit") != null) { r.outfit = Json.str(c.get("outfit"), "character.outfit", 128); Json.check(r.outfit.matches("avatar_[a-z0-9_]{1,121}"), "外观格式：" + r.outfit); }
        if (version == 4) Json.check(m.get("animation") instanceof Map, "动画格式缺少 animation");
        Object v = m.get("variants");
        if (v != null) {
            Map<String, Object> vo = Json.obj(v, "variants");
            for (Object x : Json.arr(vo.get("vars"), "variants.vars", 0, 16)) {
                Map<String, Object> e = Json.obj(x, "variants.vars"); String n = Json.str(e.get("name"), "variants.vars.name", 64); Json.check(n.length() > 1 && n.startsWith("$") && !r.vars.containsKey(n), "款式变量：" + n);
                List<Integer> values = new ArrayList<>(); for (Object k : Json.arr(e.get("values"), n, 1, 64)) values.add((int) Json.integer(k, n, -1000000, 1000000)); r.vars.put(n, values);
            }
            if (version >= 2) {
                Map<String, Object> d = Json.obj(vo.get("default"), "variants.default");
                for (String n : r.vars.keySet()) { int x = (int) Json.integer(d.get(n), "variants.default." + n, -1000000, 1000000); Json.check(r.vars.get(n).contains(x), "默认值不在可选值里：" + n); r.defaults.put(n, x); }
                Json.check(d.size() == r.vars.size(), "variants.default 里有未声明的变量");
            } else {
            for (Object x : Json.arr(vo.get("combos"), "variants.combos", 1, 32)) {
                Map<String, Integer> combo = new LinkedHashMap<>();
                for (Map.Entry<String, Object> e : Json.obj(x, "variants.combos").entrySet()) { Json.check(r.vars.containsKey(e.getKey()), "款式组合里有未声明的变量：" + e.getKey()); combo.put(e.getKey(), (int) Json.integer(e.getValue(), e.getKey(), -1000000, 1000000)); }
                Json.check(combo.size() == r.vars.size(), "款式组合必须给每个变量赋值"); r.combos.add(combo);
            }
            r.defaultCombo = (int) Json.integer(vo.get("default"), "variants.default", 0, r.combos.size() - 1);
            r.defaults.putAll(r.combos.get(r.defaultCombo));
            }
        }
        Object gate = m.get("replaceWhen");
        if (gate != null) {
            Json.check(version >= 3, "replaceWhen 需要模型格式版本 3");
            Map<String, Object> g = Json.obj(gate, "replaceWhen");
            for (Object x : Json.arr(g.get("vars"), "replaceWhen.vars", 1, 16)) { String n = Json.str(x, "replaceWhen.vars", 64); Json.check(r.vars.containsKey(n) && !r.replaceVars.contains(n), "replaceWhen 变量：" + n); r.replaceVars.add(n); }
            for (Object x : Json.arr(g.get("allow"), "replaceWhen.allow", 1, 4096)) {
                List<Integer> row = new ArrayList<>(); List<?> a = Json.arr(x, "replaceWhen.allow", r.replaceVars.size(), r.replaceVars.size());
                for (int i = 0; i < a.size(); i++) { int value = (int) Json.integer(a.get(i), "replaceWhen.allow", -1000000, 1000000); Json.check(r.vars.get(r.replaceVars.get(i)).contains(value), "replaceWhen 取值不在可选值里：" + r.replaceVars.get(i)); row.add(value); }
                r.replaceAllow.add(row);
            }
        }
        if (m.get("parts") instanceof List) for (Object p : (List<?>) m.get("parts")) {
            if (!(p instanceof Map) || !(((Map<?, ?>) p).get("target") instanceof Map)) continue;
            Object mesh = ((Map<?, ?>) ((Map<?, ?>) p).get("target")).get("mesh");
            if (mesh instanceof String && lowerLod((String) mesh)) { r.lodMesh = (String) mesh; break; }
        }
        Object s = m.get("source");
        if (s != null) { Map<String, Object> so = Json.obj(s, "source"); if (so.get("path") instanceof String) r.sourcePath = (String) so.get("path"); if (so.get("signature") instanceof String) r.sourceSignature = (String) so.get("signature"); }
        File learned = new File(dir, "learned.json");
        if (learned.isFile()) try {
            Map<String, Object> l = Json.obj(Json.read(learned, 1L << 20), "learned.json");
            if ("gibox-learned".equals(l.get("format")) && l.get("id") != null) { int id = (int) Json.integer(l.get("id"), "learned.id", 0, 10001999); if (id == 0 || id >= 10000000) r.learnedId = id; }
        } catch (Exception ignored) {}
        // Older models have no keys; a list the runtime would refuse is not offered.
        try {
            Object logic = m.get("logic");
            if (logic instanceof Map) r.driven = Json.bool(((Map<?, ?>) logic).get("driven"), "logic.driven", false);
            if (logic instanceof Map && ((Map<?, ?>) logic).get("keys") != null) for (Object x : Json.arr(((Map<?, ?>) logic).get("keys"), "logic.keys", 0, 256)) {
                Map<String, Object> e = Json.obj(x, "logic.keys"); Key k = new Key();
                k.file = (int) Json.integer(e.get("file"), "logic.keys.file", 0, 63); k.section = Json.str(e.get("section"), "logic.keys.section", 256); k.type = Json.str(e.get("type"), "logic.keys.type", 16);
                Json.check(k.type.equals("cycle") || k.type.equals("toggle") || k.type.equals("activate") || k.type.equals("hold"), "logic.keys.type"); k.back = Json.bool(e.get("back"), "logic.keys.back", false);
                if (e.get("bind") != null) for (Object b : Json.arr(e.get("bind"), "logic.keys.bind", 0, 8)) k.binds.add(Json.str(b, "logic.keys.bind", 128));
                if (e.get("presets") != null) for (Object x2 : Json.arr(e.get("presets"), "logic.keys.presets", 0, 64)) {
                    Map<String, Object> po = Json.obj(x2, "logic.keys.presets"); Preset p = new Preset(); p.special = Json.bool(po.get("special"), "logic.keys.presets.special", false);
                    for (Map.Entry<String, Object> pv : Json.obj(po.get("values"), "logic.keys.presets.values").entrySet()) { Json.check(pv.getKey().startsWith("$") && pv.getKey().length() <= 64, "logic.keys.presets.values"); p.values.put(pv.getKey(), Json.str(pv.getValue(), "logic.keys.presets.values", 32)); }
                    k.presets.add(p);
                }
                r.keys.add(k);
            }
        } catch (Exception e) { r.keys.clear(); }
        return r;
    }

    /** texture.json (format "gibox-texture-mod", version 1): name, generation, character, variants, source. */
    static ModelManifest readTextureMod(File dir) throws Exception {
        Map<String, Object> m = Json.obj(Json.read(new File(dir, TextureMod.FILE), 16L << 20), TextureMod.FILE);
        Json.check("gibox-texture-mod".equals(m.get("format")), "不是 GIBox 纯贴图 MOD：" + dir.getName());
        long version = Json.integer(m.get("version"), "version", 1, 1000000); Json.check(version == 1, "纯贴图格式版本 " + version + " 不支持（新版 GIBox 转换的？）：" + dir.getName());
        ModelManifest r = new ModelManifest(); r.version = 2; r.textureMod = true;
        r.name = Json.str(m.get("name"), "name", 512); r.generation = Json.str(m.get("generation"), "generation", 64);
        r.id = (int) Json.integer(Json.obj(m.get("character"), "character").get("id"), "character.id", 10000000, 10001999);
        Map<String, Object> vo = Json.obj(m.get("variants"), "variants"); Map<String, Object> d = Json.obj(vo.get("default"), "variants.default");
        for (Object x : Json.arr(vo.get("vars"), "variants.vars", 0, 16)) {
            Map<String, Object> e = Json.obj(x, "variants.vars"); String n = Json.str(e.get("name"), "variants.vars.name", 64); Json.check(n.length() > 1 && n.startsWith("$") && !r.vars.containsKey(n), "款式变量：" + n);
            List<Integer> values = new ArrayList<>(); for (Object k : Json.arr(e.get("values"), n, 1, 64)) values.add((int) Json.integer(k, n, -1000000, 1000000)); r.vars.put(n, values);
            int v = (int) Json.integer(d.get(n), "variants.default." + n, -1000000, 1000000); Json.check(values.contains(v), "默认值不在可选值里：" + n); r.defaults.put(n, v);
        }
        Object s = m.get("source");
        if (s != null) { Map<String, Object> so = Json.obj(s, "source"); if (so.get("path") instanceof String) r.sourcePath = (String) so.get("path"); if (so.get("signature") instanceof String) r.sourceSignature = (String) so.get("signature"); }
        return r;
    }

    /** The character this model is for: its own ID, else the one the runtime learned. */
    public int character() { return id != 0 ? id : learnedId; }
}
