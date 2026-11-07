package com.gibox.tool;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.*;

/**
 * Mod folder helpers shared by the importer: the skeleton library the runtime records (skeletons/<id>/skeleton.json),
 * the folder holding a mod's INI, name tokens, archive unpacking, and modelswap.txt (the character chosen for a mod).
 */
public final class ModLibrary {
    private ModLibrary() {}

    /** A character the runtime has seen: its skeleton library folder plus the transform chain above its skeleton. */
    public static final class Character {
        public final int id; public final File bones; public final String root;
        /** A renderer recorded at a lower level of detail (libraries written before 0.6.5, while the character was still loading); else "". */
        public final String lodMesh;
        Character(int id, File bones, String root, String lodMesh) { this.id = id; this.bones = bones; this.root = root; this.lodMesh = lodMesh; }
    }

    /** skeleton.json files are large (every bindpose); their id and root are read once per file version. */
    private static final Map<String, Object[]> HEADS = new HashMap<>();
    private static Object[] head(File f) throws Exception {
        String key = f.getPath(); long stamp = f.lastModified() * 31 + f.length();
        synchronized (HEADS) { Object[] h = HEADS.get(key); if (h != null && (Long) h[0] == stamp) return h; }
        Map<String, Object> j = Json.obj(Json.read(f, 64L << 20), "skeleton.json");
        Json.check("gibox-skeleton".equals(j.get("format")) && Json.integer(j.get("version"), "version", 1, 1000000) == 1, "不是 GIBox 骨骼记录，或版本不支持");
        int id = (int) Json.integer(j.get("id"), "id", 10000000, 10001999); String root = j.get("root") instanceof String ? (String) j.get("root") : "";
        String lod = "";
        if (j.get("renderers") instanceof List) for (Object r : (List<?>) j.get("renderers")) {
            Object t = r instanceof Map ? ((Map<?, ?>) r).get("target") : null, mesh = t instanceof Map ? ((Map<?, ?>) t).get("mesh") : null;
            if (mesh instanceof String && ModelManifest.lowerLod((String) mesh)) { lod = (String) mesh; break; }
        }
        Object[] h = {stamp, id, root, lod}; synchronized (HEADS) { HEADS.put(key, h); } return h;
    }
    public static List<Character> library(File boneRoot) {
        Map<Integer, Character> byId = new TreeMap<>(); File[] dirs = boneRoot.listFiles(); if (dirs == null) return new ArrayList<>();
        Arrays.sort(dirs, Comparator.comparing(File::getName));
        for (File d : dirs) try {
            File info = new File(d, "skeleton.json"); if (!d.isDirectory() || d.getName().startsWith(".") || !info.isFile()) continue;
            Object[] h = head(info); int id = (Integer) h[1];
            Character c = new Character(id, d, (String) h[2], (String) h[3]); Character prior = byId.get(id);
            // The library folder (named exactly by id) wins over timestamped manual exports.
            if (prior == null || d.getName().equals(Integer.toString(id))) byId.put(id, c);
        } catch (Exception ignored) {}
        return new ArrayList<>(byId.values());
    }
    public static Character recorded(File boneRoot, int id) { for (Character c : library(boneRoot)) if (c.id == id) return c; return null; }
    /**
     * The library of one outfit (Outfits): a skin's own folder skeletons/<id>/<prefab>; the original look's skeletons/<id>
     * unless that holds a skin (recorded there before 0.7.94: it counts for that skin, never for the original). prefab
     * null or "": skeletons/<id> as it is (a character without skins).
     */
    public static Character recorded(File boneRoot, int id, String prefab) {
        Character main = null; File mainFile = new File(new File(boneRoot, Integer.toString(id)), "skeleton.json");
        try { if (mainFile.isFile()) { Object[] h = head(mainFile); if ((Integer) h[1] == id) main = new Character(id, mainFile.getParentFile(), (String) h[2], (String) h[3]); } } catch (Exception ignored) {}
        if (prefab == null || prefab.isEmpty()) return main;
        String had = main == null ? "" : Outfits.chainAvatar(main.root);
        if (Outfits.costume(prefab)) {
            File own = new File(Outfits.libraryDir(boneRoot, id, prefab), "skeleton.json");
            try { if (own.isFile()) { Object[] h = head(own); if ((Integer) h[1] == id) return new Character(id, own.getParentFile(), (String) h[2], (String) h[3]); } } catch (Exception ignored) {}
            return prefab.equals(had) ? main : null;
        }
        return main == null || (!had.isEmpty() && !had.equals(prefab)) ? null : main;
    }
    /** The outfits of a character that have a library (prefabs; the original's as "" when the table has no name for it). */
    public static Set<String> recordedOutfits(File boneRoot, int id, Outfits table) {
        Set<String> out = new TreeSet<>(); for (Outfits.Outfit o : table.of(id)) if (recorded(boneRoot, id, o.prefab) != null) out.add(o.prefab); return out;
    }

    private static final Set<String> GENERIC = new HashSet<>(Arrays.asList(
        "avatar", "girl", "boy", "lady", "male", "loli", "female", "catalyst", "sword", "claymore", "pole", "bow", "clone", "model", "root", "scene", "bip001", "bip",
        "texture", "override", "resource", "position", "blend", "texcoord", "texcoords", "diffuse", "lightmap", "normalmap", "normal", "head", "body", "dress", "extra", "face",
        "hair", "skin", "buf", "dds", "ini", "mod", "mods", "constants", "present", "commandlist", "global", "key", "swap", "toggle", "vertexlimitraise", "shader", "vb", "ib",
        "remap", "merged", "default", "outfit", "skirt", "the", "and", "for", "with", "new", "test", "fix", "orfix", "version", "manekin"));
    /** Distinctive lower-case words of a name (3+ letters, not generic): Avatar_Girl_Catalyst_Citlali → citlali. */
    static Set<String> tokens(String s) {
        Set<String> out = new LinkedHashSet<>(); StringBuilder cur = new StringBuilder(); char prev = 0;
        for (int i = 0; i <= s.length(); i++) {
            char c = i < s.length() ? s.charAt(i) : ' ';
            boolean split = !java.lang.Character.isLetterOrDigit(c) || (java.lang.Character.isUpperCase(c) && java.lang.Character.isLowerCase(prev)) || (java.lang.Character.isDigit(c) != java.lang.Character.isDigit(prev) && cur.length() > 0);
            if (split && cur.length() > 0) { String t = cur.toString().toLowerCase(Locale.ROOT); if (t.length() >= 3 && !GENERIC.contains(t) && !t.matches("[0-9]+")) out.add(t); cur.setLength(0); }
            if (java.lang.Character.isLetterOrDigit(c)) cur.append(c);
            prev = c;
        }
        return out;
    }
    /** Find the geometry INI even when a PC package includes help/menu and face-texture sidecars. */
    static File modRoot(File dir) { return mainIni(dir).getParentFile(); }
    /** The INI that is converted: the scheme stored in modelswap.txt ("ini"), else the only model INI. */
    static File mainIni(File mod) { return ModConfig.find(mod, overrides(mod).get("ini")); }
    static File ini(File mod) { return ModConfig.mainIni(mod); }
    /** Alternative model INIs of a package, as paths relative to the mod folder; one entry or none: nothing to choose. */
    static List<String> schemes(File mod) { List<String> out = new ArrayList<>(); for (File f : ModConfig.schemeInis(mod)) out.add(ModConfig.relative(mod, f)); return out; }
    /** A package is usable when it has one model INI, several to choose from, or a single INI (its own error later). */
    static void check(File dir) { if (ModConfig.geometryInis(dir).size() <= 1) ModConfig.find(dir); }

    /** Content signature: relative path, size and mtime of every file. Stored in model.json "source". */
    public static String signature(File mod) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-1"); List<File> files = new ArrayList<>(); walk(mod, files, 0);
        files.sort(Comparator.comparing(File::getPath)); String base = mod.getPath();
        for (File f : files) md.update((f.getPath().substring(base.length()) + "|" + f.length() + "|" + f.lastModified() + "\n").getBytes(StandardCharsets.UTF_8));
        StringBuilder s = new StringBuilder(); for (byte b : md.digest()) s.append(String.format("%02x", b)); return s.toString();
    }
    private static void walk(File d, List<File> out, int depth) {
        File[] a = d.listFiles(); if (a == null || depth > 8) return;
        for (File f : a) { if (f.isDirectory()) walk(f, out, depth + 1); else if (out.size() < 5000 && !f.getName().equals("modelswap.txt") && !f.getName().equals(".gibox.tmp")) out.add(f); }
    }
    static String modelName(String folder) {
        String n = folder.replaceAll("[/\\\\:*?\"<>|\\p{Cntrl}]", "_").trim(); while (n.startsWith(".")) n = n.substring(1);
        if (n.isEmpty()) n = "mod"; return n.length() > 80 ? n.substring(0, 80).trim() : n;
    }
    /** modelswap.txt in a mod folder: "id 10000107" (the character chosen at import), "map Part=Renderer;...", "name 模型名". */
    static Map<String, String> overrides(File mod) {
        Map<String, String> o = new LinkedHashMap<>(); File f = new File(mod, "modelswap.txt"); if (!f.isFile()) return o;
        try { for (String l : new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8).split("\\r?\\n")) { String t = l.trim(); int sp = t.indexOf(' '); if (sp > 0) o.put(t.substring(0, sp).toLowerCase(Locale.ROOT), t.substring(sp + 1).trim()); } } catch (Exception ignored) {}
        return o;
    }
    static void setOverride(File mod, String key, String value) throws Exception {
        Map<String, String> o = overrides(mod); if (value == null) o.remove(key); else o.put(key, value);
        StringBuilder s = new StringBuilder(); for (Map.Entry<String, String> e : o.entrySet()) s.append(e.getKey()).append(' ').append(e.getValue()).append('\n');
        File tmp = new File(mod, ".gibox.tmp"); Files.write(tmp.toPath(), s.toString().getBytes(StandardCharsets.UTF_8));
        Files.move(tmp.toPath(), new File(mod, "modelswap.txt").toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
    }

    /** A broken or unsafe archive (not a file-name charset problem). */
    static final class Rejected extends IllegalArgumentException { Rejected(String s) { super(s); } }
    /** Unpacks a zip (UTF-8 names, then GBK) with a path guard, at most 5000 entries and 2 GB. */
    static void unzip(File zip, File dest) throws Exception {
        Exception first = null;
        for (java.nio.charset.Charset cs : new java.nio.charset.Charset[]{StandardCharsets.UTF_8, java.nio.charset.Charset.forName("GBK")}) {
            ModelConverter.remove(dest); if (!dest.mkdirs()) throw new IllegalStateException("无法创建：" + dest);
            try (java.util.zip.ZipFile z = new java.util.zip.ZipFile(zip, cs)) {
                String root = dest.getCanonicalPath() + File.separator; long total = 0; int n = 0; byte[] buf = new byte[65536];
                for (java.util.Enumeration<? extends java.util.zip.ZipEntry> en = z.entries(); en.hasMoreElements(); ) {
                    java.util.zip.ZipEntry e = en.nextElement(); if (++n > 5000) throw new Rejected("压缩包文件过多");
                    File out = new File(dest, e.getName()).getCanonicalFile(); if (!out.getPath().startsWith(root)) throw new Rejected("压缩包路径越界：" + e.getName());
                    if (e.isDirectory()) { out.mkdirs(); continue; } out.getParentFile().mkdirs();
                    try (java.io.InputStream in = z.getInputStream(e); java.io.OutputStream o = new java.io.FileOutputStream(out)) { int k; while ((k = in.read(buf)) > 0) { total += k; if (total > (2L << 30)) throw new Rejected("压缩包解压后超过 2 GB"); o.write(buf, 0, k); } }
                }
                return;
            } catch (Rejected e) { ModelConverter.remove(dest); throw e; }
            // Entry names that are not valid in this charset: java.util.zip reports ZipException or IllegalArgumentException.
            catch (java.util.zip.ZipException | IllegalArgumentException e) { if (first == null) first = e; }
        }
        ModelConverter.remove(dest);
        throw new IllegalArgumentException("无法解压（不是 zip，或文件名编码无法识别；rar / 7z 请先重新压缩成 .zip）：" + (first == null ? "" : first.getMessage()));
    }
}
