package com.gibox.tool;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

/**
 * Playable characters: AvatarId and Chinese name (124 up to 7.1 and six of 7.2, tests/avatar_ids.txt), the internal model name
 * (skeleton root Avatar_Girl_Catalyst_Citlali → citlali) and the names mods use for them, for search and for the
 * recommended character of an imported mod.
 */
public final class Characters {
    private Characters() {}

    public static final class Avatar {
        public final int id; public final String name;
        /** Internal model names, as in the game's skeleton root; written to match.txt for an import without a bone library. */
        public final List<String> model;
        /** Lower-case names a mod may use (internal and English names, joined forms such as hutao, raidenshogun). */
        public final List<String> aliases;
        Avatar(int id, String name, String model, String extra) {
            this.id = id; this.name = name; this.model = split(model);
            List<String> all = new ArrayList<>(this.model); for (String s : split(extra)) if (!all.contains(s)) all.add(s); aliases = Collections.unmodifiableList(all);
        }
        @Override public String toString() { return name + "  " + id; }
    }
    private static List<String> split(String s) { List<String> out = new ArrayList<>(); for (String w : s.trim().split("\\s+")) if (!w.isEmpty()) out.add(w); return Collections.unmodifiableList(out); }
    private static Avatar a(int id, String name, String model, String extra) { return new Avatar(id, name, model, extra); }

    private static final Avatar[] ALL = {
        a(10000002, "神里绫华", "ayaka", "kamisatoayaka"),
        a(10000003, "琴", "qin", "jean"),
        a(10000005, "空（男旅行者）", "playerboy", "aether travelerboy traveler"),
        a(10000006, "丽莎", "lisa", ""),
        a(10000007, "荧（女旅行者）", "playergirl", "lumine travelergirl traveler"),
        a(10000014, "芭芭拉", "barbara", ""),
        a(10000015, "凯亚", "kaeya", ""),
        a(10000016, "迪卢克", "diluc", ""),
        a(10000020, "雷泽", "razor", ""),
        a(10000021, "安柏", "ambor", "amber"),
        a(10000022, "温迪", "venti", ""),
        a(10000023, "香菱", "xiangling", ""),
        a(10000024, "北斗", "beidou", ""),
        a(10000025, "行秋", "xingqiu", ""),
        a(10000026, "魈", "xiao", ""),
        a(10000027, "凝光", "ningguang", ""),
        a(10000029, "可莉", "klee", ""),
        a(10000030, "钟离", "zhongli", ""),
        a(10000031, "菲谢尔", "fischl", ""),
        a(10000032, "班尼特", "bennett", ""),
        a(10000033, "达达利亚", "tartaglia", "childe"),
        a(10000034, "诺艾尔", "noel", "noelle"),
        a(10000035, "七七", "qiqi", ""),
        a(10000036, "重云", "chongyun", ""),
        a(10000037, "甘雨", "ganyu", ""),
        a(10000038, "阿贝多", "albedo", ""),
        a(10000039, "迪奥娜", "diona", ""),
        a(10000041, "莫娜", "mona", ""),
        a(10000042, "刻晴", "keqing", ""),
        a(10000043, "砂糖", "sucrose", ""),
        a(10000044, "辛焱", "xinyan", ""),
        a(10000045, "罗莎莉亚", "rosaria", ""),
        a(10000046, "胡桃", "hutao", ""),
        a(10000047, "枫原万叶", "kazuha", "kaedeharakazuha"),
        a(10000048, "烟绯", "feiyan", "yanfei"),
        a(10000049, "宵宫", "yoimiya", ""),
        a(10000050, "托马", "tohma", "thoma"),
        a(10000051, "优菈", "eula", ""),
        a(10000052, "雷电将军", "shougun", "raiden raidenshogun shogun raidenei"),
        a(10000053, "早柚", "sayu", ""),
        a(10000054, "珊瑚宫心海", "kokomi", "sangonomiyakokomi"),
        a(10000055, "五郎", "gorou", ""),
        a(10000056, "九条裟罗", "sara", "kujousara"),
        a(10000057, "荒泷一斗", "itto", "aratakiitto"),
        a(10000058, "八重神子", "yae", "yaemiko"),
        a(10000059, "鹿野院平藏", "heizo", "heizou shikanoinheizou"),
        a(10000060, "夜兰", "yelan", ""),
        a(10000061, "绮良良", "momoka", "kirara"),
        a(10000062, "埃洛伊", "aloy", ""),
        a(10000063, "申鹤", "shenhe", ""),
        a(10000064, "云堇", "yunjin", ""),
        a(10000065, "久岐忍", "shinobu", "kukishinobu"),
        a(10000066, "神里绫人", "ayato", "kamisatoayato"),
        a(10000067, "柯莱", "collei", ""),
        a(10000068, "多莉", "dori", ""),
        a(10000069, "提纳里", "tighnari", ""),
        a(10000070, "妮露", "nilou", ""),
        a(10000071, "赛诺", "cyno", ""),
        a(10000072, "坎蒂丝", "candace", ""),
        a(10000073, "纳西妲", "nahida", ""),
        a(10000074, "莱依拉", "layla", ""),
        a(10000075, "流浪者", "wanderer", "scaramouche kunikuzushi balladeer"),
        a(10000076, "珐露珊", "faruzan", ""),
        a(10000077, "瑶瑶", "yaoyao", ""),
        a(10000078, "艾尔海森", "alhatham", "alhaitham"),
        a(10000079, "迪希雅", "dehya", ""),
        a(10000080, "米卡", "mika", ""),
        a(10000081, "卡维", "kaveh", ""),
        a(10000082, "白术", "baizhuer", "baizhu"),
        a(10000083, "琳妮特", "linette", "lynette"),
        a(10000084, "林尼", "liney", "lyney"),
        a(10000085, "菲米尼", "freminet", ""),
        a(10000086, "莱欧斯利", "wriothesley", ""),
        a(10000087, "那维莱特", "neuvillette", ""),
        a(10000088, "夏洛蒂", "charlotte", ""),
        a(10000089, "芙宁娜", "furina", ""),
        a(10000090, "夏沃蕾", "chevreuse", ""),
        a(10000091, "娜维娅", "navia", ""),
        a(10000092, "嘉明", "gaming", ""),
        a(10000093, "闲云", "liuyun", "xianyun"),
        a(10000094, "千织", "chiori", ""),
        a(10000095, "希格雯", "sigewinne", ""),
        a(10000096, "阿蕾奇诺", "arlecchino", ""),
        a(10000097, "赛索斯", "sethos", ""),
        a(10000098, "克洛琳德", "clorinde", ""),
        a(10000099, "艾梅莉埃", "emilie", ""),
        a(10000100, "卡齐娜", "kachina", ""),
        a(10000101, "基尼奇", "kinich", ""),
        a(10000102, "玛拉妮", "mualani", ""),
        a(10000103, "希诺宁", "xilonen", ""),
        a(10000104, "恰斯卡", "chasca", ""),
        a(10000105, "欧洛伦", "olorun", "ororon"),
        a(10000106, "玛薇卡", "mavuika", ""),
        a(10000107, "茜特菈莉", "citlali", ""),
        a(10000108, "蓝砚", "lanyan", ""),
        a(10000109, "梦见月瑞希", "mizuki", "yumemizukimizuki"),
        a(10000110, "伊安珊", "iansan", ""),
        a(10000111, "瓦雷莎", "varesa", ""),
        a(10000112, "爱可菲", "escoffier", ""),
        a(10000113, "伊法", "ifa", ""),
        a(10000114, "丝柯克", "skirk", ""),
        a(10000115, "塔利雅", "dahlia", ""),
        a(10000116, "伊涅芙", "ineffa", ""),
        a(10000117, "奇偶·男性", "", ""),
        a(10000118, "奇偶·女性", "", ""),
        a(10000119, "菈乌玛", "", "lauma"),
        a(10000120, "菲林斯", "", "flins"),
        a(10000121, "爱诺", "", "aino"),
        a(10000122, "奈芙尔", "", "nefer"),
        a(10000123, "杜林", "", "durin"),
        a(10000124, "雅珂达", "", "jahoda"),
        a(10000125, "哥伦比娅", "", "columbina"),
        a(10000126, "兹白", "", "zibai"),
        // 10000127 on: internal names from the game data (AvatarExcelConfigData iconName UI_AvatarIcon_<name>, as in the
        // GuraFoundation/YuanShenResources extraction); on the device the same names in the skeleton roots (lohen, nicole,
        // marionettenew, vodyanitsa, vesna, linnea, mitya, valeriy). 10000136, 10000137, 10000147, 10000149: names as the
        // user gave them, internal names from the same data.
        a(10000127, "叶洛亚", "illuga", ""),
        a(10000128, "法尔伽", "", "varka"),
        a(10000129, "洛恩", "lohen", ""),
        a(10000130, "莉奈娅", "linnea", ""),
        a(10000131, "尼可", "nicole", ""),
        a(10000132, "布伦妮", "prune", ""),
        a(10000133, "桑多涅", "marionettenew", "sandrone"),
        // 10000134 / 10000135: the travelers of the TPS mode (the data: PlayerBoy / PlayerGirl with a crossbow). No model
        // name or alias, so a traveler mod is still guessed as 10000005 / 10000007.
        a(10000134, "空（TPS 模式）", "", ""),
        a(10000135, "荧（TPS 模式）", "", ""),
        a(10000136, "米提亚", "mitya", ""),
        a(10000137, "瓦列里", "valeriy", ""),
        a(10000140, "沃雅妮莎", "vodyanitsa", ""),
        a(10000143, "薇斯纳", "vesna", ""),
        a(10000147, "达妮卡", "danica", ""),
        a(10000148, "阿罗夏", "alyosha", "aloysha"),
        a(10000149, "安娜丝塔夏", "anastasya", "tsaritsa"),
        a(10000150, "奥黛塔", "odette", "")
    };

    public static List<Avatar> all() { return Collections.unmodifiableList(Arrays.asList(ALL)); }
    /** Not a character (1.0.3): a texture-only mod for NPCs and monsters, whose meshes the game side finds anywhere in the
     *  scene by the index-buffer hashes its rules name (texture_diag.hpp kScene). Listed last in the picker. */
    public static final int SCENE = 1;
    public static final Avatar SCENE_AVATAR = new Avatar(SCENE, "NPC / 怪物 / 风之翼", "", "npc monster monsters enemy enemies glider flycloak 怪物 敌人 风之翼 滑翔");
    public static boolean isScene(int id) { return id == SCENE; }
    public static Avatar byId(int id) { if (id == SCENE) return SCENE_AVATAR; for (Avatar a : ALL) if (a.id == id) return a; return null; }
    public static String nameOf(int id) { Avatar a = byId(id); return a != null ? a.name : id == 0 ? "未指定角色" : "角色 " + id; }

    /** By ID or by name (Chinese, English or internal), in list order. An empty query lists everyone. */
    public static List<Avatar> search(String query) {
        String q = query == null ? "" : query.trim().toLowerCase(Locale.ROOT);
        if (q.isEmpty()) { List<Avatar> out = new ArrayList<>(all()); out.add(SCENE_AVATAR); return out; }
        String compact = q.replaceAll("[^a-z0-9]", ""); List<Avatar> out = new ArrayList<>();
        for (Avatar a : ALL) {
            boolean hit = Integer.toString(a.id).contains(q) || a.name.toLowerCase(Locale.ROOT).contains(q);
            if (!hit && compact.length() >= 2 && !compact.matches("[0-9]+")) for (String s : a.aliases) if (s.contains(compact)) { hit = true; break; }
            if (hit) out.add(a);
        }
        boolean scene = SCENE_AVATAR.name.toLowerCase(Locale.ROOT).contains(q); for (String s : SCENE_AVATAR.aliases) scene = scene || s.contains(q);
        if (scene) out.add(SCENE_AVATAR);
        return out;
    }

    /** Characters a mod most likely belongs to (several only when they score the same), best first, and the words seen. */
    public static final class Guess {
        public final List<Avatar> recommended = new ArrayList<>(); public final Map<Integer, Integer> scores = new LinkedHashMap<>(); public String evidence = "";
        public boolean recommends(int id) { for (Avatar a : recommended) if (a.id == id) return true; return false; }
    }
    /** Words of a name: split at anything not a letter or digit, at lower→upper case changes and at letter/digit changes. */
    static List<String> words(String s) {
        List<String> out = new ArrayList<>(); StringBuilder cur = new StringBuilder(); char prev = 0;
        for (int i = 0; i <= s.length(); i++) {
            char c = i < s.length() ? s.charAt(i) : ' '; boolean alnum = c < 128 && Character.isLetterOrDigit(c);
            boolean split = !alnum || (Character.isUpperCase(c) && Character.isLowerCase(prev)) || (cur.length() > 0 && Character.isDigit(c) != Character.isDigit(prev));
            if (split && cur.length() > 0) { out.add(cur.toString().toLowerCase(Locale.ROOT)); cur.setLength(0); }
            if (alnum) cur.append(c);
            prev = alnum ? c : 0;
        }
        return out;
    }
    /** Every word plus every two or three adjacent words joined (Hu Tao → hutao, RaidenShogun → raidenshogun). */
    static void count(String phrase, int weight, Map<String, Integer> counts) {
        List<String> w = words(phrase);
        for (int i = 0; i < w.size(); i++) { StringBuilder k = new StringBuilder(); for (int j = i; j < w.size() && j < i + 3; j++) { k.append(w.get(j)); counts.merge(k.toString(), weight, Integer::sum); } }
    }
    /**
     * Scores every character by the names in the mod: the name it was imported under and its folder and INI names count
     * three times, every INI section ([TextureOverrideCitlaliBody]) and resource file name (CitlaliHeadDiffuse.dds) once.
     * Characters in the bone library also match by the model name the game itself gave them.
     */
    public static Guess guess(File modRoot, String importedAs, List<ModLibrary.Character> library) throws Exception { return guess(modRoot, ModLibrary.ini(modRoot), importedAs, library); }
    public static Guess guess(File modRoot, File ini, String importedAs, List<ModLibrary.Character> library) throws Exception {
        Map<String, Integer> counts = new HashMap<>();
        count(importedAs == null ? "" : importedAs, 3, counts); count(modRoot.getName(), 3, counts); count(ini.getName().replaceFirst("(?i)\\.ini$", ""), 3, counts);
        for (String l : new String(Files.readAllBytes(ini.toPath()), StandardCharsets.UTF_8).split("\\r?\\n")) {
            String t = l.trim();
            if (t.startsWith("[") && t.endsWith("]")) count(t.substring(1, t.length() - 1), 1, counts);
            else if (t.toLowerCase(Locale.ROOT).startsWith("filename")) { int eq = t.indexOf('='); if (eq > 0) count(new File(t.substring(eq + 1).trim().replace('\\', '/').replace("\"", "")).getName().replaceFirst("\\.[A-Za-z0-9]+$", ""), 1, counts); }
        }
        Map<Integer, Set<String>> recorded = new HashMap<>();
        if (library != null) for (ModLibrary.Character c : library) for (String seg : c.root.split("/")) if (seg.toLowerCase(Locale.ROOT).startsWith("avatar")) recorded.computeIfAbsent(c.id, k -> new LinkedHashSet<>()).addAll(ModLibrary.tokens(seg));
        Guess g = new Guess(); int best = 0; StringBuilder why = new StringBuilder();
        for (Avatar a : ALL) {
            Set<String> names = new LinkedHashSet<>(a.aliases); Set<String> own = recorded.get(a.id); if (own != null) names.addAll(own);
            int score = 0; String hit = null; for (String n : names) { Integer c = counts.get(n); if (c != null) { score += c; if (hit == null) hit = n; } }
            if (score > 0) { g.scores.put(a.id, score); if (why.length() < 200) why.append(why.length() == 0 ? "" : ", ").append(hit).append('=').append(score); }
            best = Math.max(best, score);
        }
        if (best > 0) for (Avatar a : ALL) if (g.scores.getOrDefault(a.id, 0) == best) g.recommended.add(a);
        g.evidence = why.toString(); return g;
    }
    /** Adds what the mod's hashes say (DeviceHashes.characters: character -> its hashes), ten per hash: a hash this
     *  device or the PC database ties to one character outweighs any name. The recommendation is made again. */
    public static void hashEvidence(Guess g, Map<Integer, List<String>> hits) {
        if (hits.isEmpty()) return;
        StringBuilder why = new StringBuilder(g.evidence);
        for (Map.Entry<Integer, List<String>> e : hits.entrySet()) {
            int add = 10 * e.getValue().size(); g.scores.merge(e.getKey(), add, Integer::sum);
            if (why.length() < 300) why.append(why.length() == 0 ? "" : ", ").append("hash ").append(e.getValue().get(0)).append(e.getValue().size() > 1 ? " 等 " + e.getValue().size() + " 个" : "").append('=').append(add);
        }
        int best = 0; for (int v : g.scores.values()) best = Math.max(best, v);
        g.recommended.clear(); if (best > 0) for (Avatar a : ALL) if (g.scores.getOrDefault(a.id, 0) == best) g.recommended.add(a);
        if (best > 0 && g.scores.getOrDefault(SCENE, 0) == best) g.recommended.add(SCENE_AVATAR);   // hashes of an NPC's, a monster's or the glider's mesh
        g.evidence = why.toString();
    }
    /** Picker order: the recommended characters first, then everyone else in ID order. */
    public static List<Avatar> ordered(Guess g, String query) {
        List<Avatar> hits = search(query), out = new ArrayList<>();
        if (g != null) for (Avatar a : g.recommended) if (hits.contains(a)) out.add(a);
        for (Avatar a : hits) if (!out.contains(a)) out.add(a);
        return out;
    }
}
