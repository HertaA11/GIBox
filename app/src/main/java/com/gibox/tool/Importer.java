package com.gibox.tool;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.*;

/**
 * Manual import, in the app (never in the game): a PC mod folder or .zip is copied into the Mods folder (GIBox's own
 * private storage, mods/<游戏包名>/<名称>), the user picks its character (the guessed one is offered first), and it is
 * converted into the game's media folder, GIBox/models/<名称> (model.json + model.bin). The game only loads finished
 * model folders and reloads them when they change.
 *
 * With the character's skeleton in the skeleton library (recorded by the game once the character was on the field) the
 * conversion maps bones by name and knows the character's meshes, so it shows up everywhere at once. Without it the
 * model keeps the game's own bone order (GIMI mods do) and its meshes are identified the first time the character
 * appears: on the field, or by its model name on the party and detail pages.
 */
public final class Importer {
    private Importer() {}

    /** One entry of the Mods list: a mod folder and/or the model converted from it. */
    public static final class Mod {
        public String name = ""; public File source, model; public int id; public boolean converted; public String sourceName = "";
        /** Needs converting again: for another character than the one chosen now, unreadable, or against a lower level of detail. */
        public boolean outdated;
        /** Why, when it is not the character: shown on the detail page. */
        public String reason = "";
        /** The outfit (prefab, Outfits) the model was converted for, else the one chosen for it; "" for none. */
        public String outfit = "";
    }

    public static String suggestName(File source) { return suggestName(source.getName()); }
    /** Mods entry name for a picked folder or archive name. */
    public static String suggestName(String picked) { return ModLibrary.modelName(stripZip(picked)); }
    static boolean inside(File f, File dir) throws Exception { String a = f.getCanonicalPath(), b = dir.getCanonicalPath(); return a.equals(b) || a.startsWith(b + File.separator); }
    /** A free name in the Mods folder: "名称", "名称 (2)", ... */
    public static String freeName(File modsRoot, File modelsRoot, String name) {
        if (!new File(modsRoot, name).exists() && !new File(modelsRoot, name).exists()) return name;
        for (int i = 2; ; i++) { String n = ModLibrary.modelName(name + " (" + i + ")"); if (!new File(modsRoot, n).exists() && !new File(modelsRoot, n).exists()) return n; }
    }

    /** Where a mod comes from: a folder or .zip on disk, or a document the system file picker returned (SafSource). */
    public interface Source {
        /** The name it was picked under (folder or archive name), used for the Mods entry and the character guess. */
        String name();
        /** Copies or unpacks the mod into an empty folder. */
        void copyTo(File dir, StringBuilder log) throws Exception;
    }
    /** A folder (copied) or .zip (unpacked) on disk. */
    public static Source file(final File source) {
        return new Source() {
            @Override public String name() { return source.getName(); }
            @Override public void copyTo(File dir, StringBuilder log) throws Exception {
                if (!source.exists()) throw new IllegalArgumentException("找不到：" + source);
                if (source.isFile() && source.getName().toLowerCase(Locale.ROOT).endsWith(".zip")) { ModLibrary.unzip(source, dir); log.append("已解压：").append(source.getName()).append('\n'); }
                else if (source.isDirectory()) { copy(source, dir, 0); log.append("已复制：").append(source).append('\n'); }
                else throw new IllegalArgumentException("请选择 MOD 的 .zip 压缩包（rar / 7z 请先重新压缩成 .zip）");
            }
        };
    }
    static File pendingRoot(File modsRoot) { return new File(modsRoot, ".pending"); }
    /**
     * Copies a picked mod into Mods/.pending/<name> and checks it holds exactly one mod. It shows up in the Mods list only
     * after commit(), i.e. once a character was chosen and confirmed; discard() drops it.
     */
    public static File prepare(Source source, File modsRoot, StringBuilder log) throws Exception {
        File root = pendingRoot(modsRoot); if (!root.isDirectory() && !root.mkdirs()) throw new IllegalStateException("无法创建：" + root);
        File dir = new File(root, ModLibrary.modelName(stripZip(source.name()))); ModelConverter.remove(dir);
        if (!dir.mkdirs()) throw new IllegalStateException("无法创建：" + dir);
        try { source.copyTo(dir, log); ModLibrary.check(dir); return dir; }
        catch (Exception e) { ModelConverter.remove(dir); throw e; }
    }
    /** Moves a prepared mod to Mods/<name>, replacing a previous copy of that name; the old copy's modelswap.txt is kept. */
    public static File commit(File pending, File modsRoot, String name) throws Exception {
        File dest = new File(modsRoot, ModLibrary.modelName(name));
        Map<String, String> keep = dest.isDirectory() ? ModLibrary.overrides(dest) : new LinkedHashMap<>();
        ModelConverter.remove(dest); Files.move(pending.toPath(), dest.toPath(), StandardCopyOption.ATOMIC_MOVE);
        // The previous copy's choices (character, mapping, name) carry over unless this import made its own; a stored
        // INI scheme only while that INI still exists in the new files.
        Map<String, String> now = ModLibrary.overrides(dest);
        for (Map.Entry<String, String> e : keep.entrySet()) if (!now.containsKey(e.getKey()) && (!e.getKey().equals("ini") || ModConfig.scheme(dest, e.getValue()) != null)) ModLibrary.setOverride(dest, e.getKey(), e.getValue());
        return dest;
    }
    public static void discard(File pending) { try { if (pending != null) ModelConverter.remove(pending); } catch (Exception ignored) {} }
    /** Leftovers of imports that were never confirmed (app closed on the character page). */
    public static void discardAll(File modsRoot) { discard(pendingRoot(modsRoot)); }
    static String stripZip(String n) { return n.toLowerCase(Locale.ROOT).endsWith(".zip") ? n.substring(0, n.length() - 4) : n; }

    /**
     * Copies a mod folder, or unpacks a .zip, into modsRoot/name in one step (prepare + commit). A folder that already
     * is in the Mods folder is used where it is.
     */
    public static File stage(File source, File modsRoot, String name, StringBuilder log) throws Exception {
        if (!modsRoot.isDirectory() && !modsRoot.mkdirs()) throw new IllegalStateException("无法创建 Mods 文件夹：" + modsRoot);
        if (source.isDirectory() && inside(source, modsRoot)) { ModLibrary.check(source); log.append("使用 Mods 文件夹中的：").append(source).append('\n'); return source; }
        if (source.isDirectory() && inside(modsRoot, source)) throw new IllegalArgumentException("不能导入包含 Mods 文件夹的目录：" + source);
        File pending = prepare(file(source), modsRoot, log);
        try { return commit(pending, modsRoot, name); } finally { discard(pending); }
    }
    static void copy(File from, File to, int depth) throws Exception {
        if (depth > 8) throw new IllegalArgumentException("目录层级过深：" + from);
        if (from.isDirectory()) { if (!to.isDirectory() && !to.mkdirs()) throw new IllegalStateException("无法创建：" + to); File[] a = from.listFiles(); if (a != null) for (File f : a) copy(f, new File(to, f.getName()), depth + 1); }
        else Files.copy(from.toPath(), to.toPath(), StandardCopyOption.REPLACE_EXISTING);
    }

    /** The character the mod most likely belongs to, from the name it was imported under and its INI. */
    public static Characters.Guess guess(File mod, String importedAs, File boneRoot) {
        try { File ini = ModLibrary.schemes(mod).size() > 1 && !ModLibrary.overrides(mod).containsKey("ini") ? ModConfig.geometryInis(mod).get(0) : ModLibrary.mainIni(mod);
            Characters.Guess g = Characters.guess(ini.getParentFile(), ini, importedAs, ModLibrary.library(boneRoot));
            // The INI's hashes, where this device's hash map or the PC database names their character, weigh more than names.
            try { Characters.hashEvidence(g, DeviceHashes.load(boneRoot.getAbsoluteFile().getParentFile()).characters(ModelConverter.overrideHashes(ini.getParentFile(), ini))); }
            catch (Exception ignored) {}
            return g; }
        catch (Exception e) { return new Characters.Guess(); }
    }

    public static final class Result { public File model; public int id; public boolean library; public String summary = ""; }
    /**
     * Converts a staged mod for character `id` into modelsRoot/<name> (replacing the previous conversion) and records
     * the choice in the mod's modelswap.txt, so a re-import or re-conversion keeps it.
     */
    /** The model INIs to choose from (relative paths); fewer than two: nothing to choose. */
    public static List<String> schemes(File mod) { try { return ModLibrary.schemes(mod); } catch (Exception e) { return new ArrayList<>(); } }
    /** The chosen scheme, or "" when the package has only one. */
    /** The scheme in effect: the one chosen, else the only model INI XXMI would load (the switched-off ones offered
     *  beside it are a choice, not a question); "" when there is no single one. */
    public static String scheme(File mod) { String s = ModLibrary.overrides(mod).get("ini"); if (s != null) return s;
        try { List<File> on = ModConfig.geometryInis(mod); return on.size() == 1 ? ModConfig.relative(mod, on.get(0)) : ""; } catch (Exception e) { return ""; } }
    public static void setScheme(File mod, String rel) throws Exception {
        if (ModConfig.scheme(mod, rel) == null) throw new IllegalArgumentException("不是此 MOD 中的模型 INI：" + rel);
        ModLibrary.setOverride(mod, "ini", rel);
    }
    /** The outfit chosen for the mod (modelswap.txt "outfit", a prefab of Outfits); null: the one its hashes name. */
    public static String outfit(File mod) { return ModLibrary.overrides(mod).get("outfit"); }
    public static void setOutfit(File mod, String prefab) throws Exception { ModLibrary.setOverride(mod, "outfit", prefab == null || prefab.isEmpty() ? null : prefab); }
    /** Which outfit of `id` the mod's INI hashes name (the picker's suggestion); an empty guess when they name none. */
    public static Outfits.Guess guessOutfit(File mod, int id, File boneRoot) {
        try { File ini = ModLibrary.mainIni(mod); return Outfits.current().guess(id, ModelConverter.overrideHashes(ModConfig.switchedOn(mod, ini), ini), boneRoot.getAbsoluteFile().getParentFile()); }
        catch (Exception e) { return new Outfits.Guess(); }
    }
    public static boolean animationBoneFallback(File mod) { return "retain-valid".equals(ModLibrary.overrides(mod).get("animationbones")); }
    public static void setAnimationBoneFallback(File mod, boolean enabled) throws Exception { ModLibrary.setOverride(mod, "animationbones", enabled ? "retain-valid" : null); }
    public static Result convert(File mod, int id, File modelsRoot, File boneRoot, ModelConverter.TextureDecoder decoder, StringBuilder log) throws Exception {
        if (!Characters.isScene(id) && (id < 10000000 || id > 10001999)) throw new IllegalArgumentException("角色 ID 无效：" + id);
        File ini = ModLibrary.mainIni(mod), src = ModConfig.switchedOn(mod, ini); ModLibrary.setOverride(mod, "id", Integer.toString(id));
        Map<String, String> o = ModLibrary.overrides(mod); String name = ModLibrary.modelName(o.containsKey("name") ? o.get("name") : mod.getName());
        // The outfit (Outfits, 0.7.94): the one chosen with the character (modelswap.txt "outfit"), else the one the INI's
        // hashes name, else the original. A mod made for another outfit than the one chosen is refused: its bone indices
        // follow that outfit's bone list, and on another outfit the model is torn apart (Shenhe's Frostflower Dew mod on
        // the original look, 0.7.93).
        Outfits table = Outfits.current(); Outfits.Outfit outfit = null; String outfitNote = "";
        if (!table.of(id).isEmpty()) {
            Outfits.Guess g = table.guess(id, ModelConverter.overrideHashes(src, ini), boneRoot.getAbsoluteFile().getParentFile());
            outfit = table.byPrefab(o.get("outfit")); if (outfit != null && outfit.character != id) outfit = null;
            if (outfit == null) outfit = g.outfit != null ? g.outfit : table.original(id);
            if (g.outfit != null && g.outfit != outfit)
                throw new IllegalArgumentException("此 MOD 适用于「" + g.outfit.name + "」（INI 哈希 " + g.how + "），但所选外观为「" + outfit.name + "」：两套外观的骨骼不同，转换后会错位。请改选「" + g.outfit.name + "」");
            outfitNote = "外观：" + outfit.name + "（" + outfit.prefab + "）" + (g.outfit != null ? "；依据 INI 哈希识别：" + g.how
                : g.conflict.isEmpty() ? "；未能依据 INI 哈希识别外观，按所选外观转换" : "；INI 哈希指向多套外观（" + String.join("、", g.conflict) + "），按所选外观转换");
        }
        // The skeleton: the table made from the game's files first (1.0.4: equal, bit for bit, to what the game records),
        // then the library the game recorded on this device.
        File sharedDir = boneRoot.getAbsoluteFile().getParentFile(); ModLibrary.Character recorded = null; String boneSource = "";
        OfflineSkeletons offline = OfflineSkeletons.load(sharedDir);
        String offlinePrefab = offline == null ? null : offline.prefabFor(id, outfit != null ? outfit.prefab : null);
        if (offlinePrefab != null) try { recorded = offline.library(offlinePrefab, id, new File(sharedDir, "skeletons-offline")); boneSource = "离线骨骼表（" + offlinePrefab + "）"; }
            catch (Exception e) { log.append("离线骨骼表不可用：").append(e.getMessage()).append("，改用本机记录的骨骼库\n"); }
        if (recorded == null) { recorded = outfit != null ? ModLibrary.recorded(boneRoot, id, outfit.prefab) : ModLibrary.recorded(boneRoot, id); if (recorded != null) boneSource = "本机记录的骨骼库"; }
        Characters.Avatar avatar = Characters.byId(id);
        // A record of a lower level of detail (made right after loading, before 0.6.5) has a shorter bone list than the
        // full model the mod is made for: it is not used. The game records the full model the next time the character appears.
        String lodLibrary = recorded != null ? recorded.lodMesh : ""; if (!lodLibrary.isEmpty()) recorded = null;
        ModelConverter.Options opt = new ModelConverter.Options().mapping(o.containsKey("map") ? o.get("map") : "").variants(true).replace(true).fallback("Body").ini(ini);
        opt.deviceRenderers(DeviceHashes.load(boneRoot.getAbsoluteFile().getParentFile()).renderers(id, outfit != null ? outfit.prefab : null));
        opt.pcTextures(DeviceHashes.pcTextures(boneRoot.getAbsoluteFile().getParentFile()));
        opt.animationBoneFallback(animationBoneFallback(mod)).textureDiag(textureDiagDir(modelsRoot)).textureMods(true);
        opt.core(request -> { try { return NativeBridge.draws(request); } catch (UnsatisfiedLinkError e) { throw new IllegalStateException("3dm 后端不可用：" + e.getMessage()); } });
        opt.rehearsal(request -> { try { return NativeBridge.rehearse(request); } catch (UnsatisfiedLinkError e) { throw new IllegalStateException("逻辑核心预演不可用：" + e.getMessage()); } });
        if (o.containsKey("ini")) log.append("INI 方案：").append(o.get("ini")).append('\n');
        opt.source(mod.getAbsolutePath(), ModLibrary.signature(mod));
        if (outfit != null) { opt.outfit(outfit.prefab); log.append(outfitNote).append('\n'); }
        Result r = new Result(); r.id = id; r.library = recorded != null;
        if (!boneSource.isEmpty()) log.append("骨骼来源：").append(boneSource).append('\n');
        log.append("角色：").append(Characters.nameOf(id)).append("（").append(id).append("）").append(r.library ? "，使用骨骼库：" + recorded.bones : lodLibrary.isEmpty() ? (outfit != null ? "，骨骼库中尚无此外观" : "，骨骼库中尚无此角色") + "：按游戏原骨骼顺序转换" : "，骨骼库中此角色的记录为低精度模型 " + lodLibrary + "，不使用：按游戏原骨骼顺序转换").append('\n');
        if (!r.library && avatar != null && !avatar.model.isEmpty()) opt.names(avatar.model);
        r.model = ModelConverter.convert(src, r.library ? recorded.bones : null, new File(modelsRoot, name), id, opt, decoder, log);
        if (TextureMod.is(r.model) && Characters.isScene(id)) { r.summary = "NPC / 怪物 / 风之翼：纯贴图 MOD，按索引缓冲 hash 在场景和当前角色身上找对应的网格（风之翼在展开时写入）。本机 hash 表里还没有的网格，把调试页的 hash 模式设为“匹配”或“采集”并重启游戏，让它出现一次"; return r; }
        if (TextureMod.is(r.model)) { r.summary = Characters.nameOf(id) + "（" + id + "）：纯贴图 MOD，只换游戏模型上的贴图。进游戏后自动替换确认过的部件（调试页里可换替换方式）；匹配不上的显示“等待映射”"; return r; }
        String who = Characters.nameOf(id) + (outfit != null ? "·" + outfit.name : "");
        r.summary = who + "（" + id + "）" + (r.library ? "：骨骼库已有" + (outfit != null ? "此外观" : "该角色") + "，进入游戏即可替换" : opt.reusedSkeleton ? "：复用本 MOD 已有骨骼记录，目标网格信息已保留"
            : outfit != null && !outfit.original() ? "：骨骼库中尚无此外观，在大世界穿上该外观或在衣装页预览后识别部件"
            : "：骨骼库中尚无该角色，" + (opt.names.isEmpty() ? "需要该角色在大世界出场一次后生效" : "在大世界、队伍界面或角色详情页出现时识别部件"));
        for (String l : log.toString().split("\n")) if (l.startsWith("款式变量") || l.startsWith("模型动画：") || l.startsWith("动画骨骼兼容：") || l.contains("个顶点没有权重")) r.summary += "\n" + l;
        return r;
    }

    /** Mods folder entries, plus converted models without a source (older imports and manual conversions), by name. */
    public static List<Mod> list(File modsRoot, File modelsRoot) {
        Map<String, Mod> byName = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
        File[] mods = modsRoot.listFiles(f -> f.isDirectory() && !f.getName().startsWith(".")); if (mods == null) mods = new File[0];
        for (File d : mods) {
            Map<String, String> o = ModLibrary.overrides(d); Mod m = new Mod(); m.source = d; m.sourceName = d.getName(); if (o.containsKey("outfit")) m.outfit = o.get("outfit");
            m.name = ModLibrary.modelName(o.containsKey("name") ? o.get("name") : d.getName());
            try { m.id = o.containsKey("id") ? Integer.parseInt(o.get("id").trim()) : 0; } catch (NumberFormatException e) { m.id = 0; }
            byName.put(m.name, m);
        }
        File[] models = modelsRoot.listFiles(f -> f.isDirectory() && !f.getName().startsWith(".")); if (models == null) models = new File[0];
        for (File d : models) {
            if (!ModelManifest.exists(d)) continue;
            // An unreadable model.json (damaged, or written by a newer GIBox) shows up as needing a re-conversion.
            int built = 0; boolean broken = false; String lod = "", outfit = "";
            try { ModelManifest man = ModelManifest.read(d); built = man.character(); lod = man.lodMesh; outfit = man.outfit; } catch (Exception e) { broken = true; }
            Mod m = byName.get(d.getName()); if (m == null) { m = new Mod(); m.name = d.getName(); byName.put(m.name, m); }
            m.model = d; m.converted = true; if (!outfit.isEmpty()) m.outfit = outfit; if (broken) { m.outdated = true; m.reason = "模型文件无法读取"; }
            if (!lod.isEmpty()) { m.outdated = true; m.reason = "转换时骨骼库记录的是低精度模型 " + lod + "，骨骼不匹配"; }
            if (m.source == null) m.id = built;
            else {
                if (m.id != 0 && built != 0 && built != m.id) m.outdated = true;
                if (m.id == 0 && built != 0) m.id = built;
            }
        }
        return new ArrayList<>(byName.values());
    }
    /** One Mods entry the merge renamed because another game's Mods folder had a different mod of that name. */
    public static final class Renamed { public final String from, to; Renamed(String from, String to) { this.from = from; this.to = to; } }
    /**
     * Moves the entries of a pre-0.7.101 per-game Mods folder (`legacy`) into the shared one (`modsRoot`). An entry
     * the shared folder lacks is moved; one it holds with the same content (ModLibrary.contentKey) is dropped as a
     * duplicate; a different mod of the same name is moved under a free name, and that game's converted model (in
     * `modelsRoot`) is renamed with it. `legacy` is removed once empty. Returns the renames; `log` gets one line each.
     */
    public static List<Renamed> mergeMods(File modsRoot, File legacy, File modelsRoot, StringBuilder log) throws Exception {
        List<Renamed> renamed = new ArrayList<>();
        if (!legacy.isDirectory()) return renamed;
        if (!modsRoot.isDirectory() && !modsRoot.mkdirs()) throw new IllegalStateException("无法创建 Mods 文件夹：" + modsRoot);
        discard(pendingRoot(legacy));
        File[] entries = legacy.listFiles(f -> f.isDirectory() && !f.getName().startsWith(".")); if (entries == null) entries = new File[0];
        Arrays.sort(entries, Comparator.comparing(File::getName));
        for (File d : entries) {
            File dest = new File(modsRoot, d.getName());
            if (!dest.exists()) { Files.move(d.toPath(), dest.toPath()); log.append("迁移：").append(d.getName()).append('\n'); continue; }
            if (dest.isDirectory() && ModLibrary.contentKey(d).equals(ModLibrary.contentKey(dest))) { ModelConverter.remove(d); log.append("重复，保留一份：").append(d.getName()).append('\n'); continue; }
            Map<String, String> o = ModLibrary.overrides(d); String model = ModLibrary.modelName(o.containsKey("name") ? o.get("name") : d.getName());
            String free = freeName(modsRoot, modelsRoot, d.getName());
            dest = new File(modsRoot, free); Files.move(d.toPath(), dest.toPath());
            if (o.containsKey("name")) ModLibrary.setOverride(dest, "name", free);
            File oldModel = new File(modelsRoot, model), newModel = new File(modelsRoot, free);
            if (oldModel.isDirectory() && !newModel.exists()) Files.move(oldModel.toPath(), newModel.toPath());
            renamed.add(new Renamed(model, free)); log.append("同名但内容不同，改名：").append(d.getName()).append(" → ").append(free).append('\n');
        }
        File[] left = legacy.listFiles(); if (left == null || left.length == 0) legacy.delete();
        return renamed;
    }
    /** GIBox/texture-diag, next to the models folder: read-only diagnostics of texture-only mods (TextureDiag). */
    public static File textureDiagDir(File modelsRoot) { return new File(modelsRoot.getAbsoluteFile().getParentFile(), TextureDiag.DIR); }
    /** Removes the mod's copy in the Mods folder, its converted model and a texture-only diagnostic of that name. */
    public static void delete(Mod m) throws Exception { if (m.model != null) ModelConverter.remove(m.model); if (m.source != null) ModelConverter.remove(m.source); }
    public static void delete(Mod m, File modelsRoot) throws Exception { delete(m); TextureDiag.remove(textureDiagDir(modelsRoot), m.name); }
    /** Another game's converted model of a deleted mod, and its texture-only diagnostic. */
    public static void deleteModel(String name, File modelsRoot) throws Exception {
        File d = new File(modelsRoot, ModLibrary.modelName(name)); if (d.isDirectory() && inside(d, modelsRoot)) ModelConverter.remove(d);
        TextureDiag.remove(textureDiagDir(modelsRoot), name);
    }
}
