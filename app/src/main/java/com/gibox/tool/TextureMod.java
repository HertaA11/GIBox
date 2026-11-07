package com.gibox.tool;

import java.io.File;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

/**
 * A texture-only mod as a GIBox model folder (0.7.52): models/<name>/texture.json and textures/<n>.rgba, no mesh and
 * no skeleton. The game side (runtime.cpp, texture_diag.hpp) finds the slots with the rules in texture.json "diag" (the
 * same document TextureDiag writes) and puts the textures there with the strategy chosen in the App (control.json
 * "textureMods"): only diagnose, write the game's material, or a copy of it. A rule it cannot place waits for a mapping;
 * the import itself does not fail for that.
 *
 * Textures: decoded (DDS / PNG) to RGBA8, halved with a box filter to at most 1024 on a side (the game's own character
 * textures are 1024, 0.7.50 device log), rows top to bottom as the PC file has them (the order the game's own meshes
 * read, as the face texture swap found in 0.7.44). No mipmaps.
 */
final class TextureMod {
    private TextureMod() {}
    static final String FILE = "texture.json";
    static final int MAX_SIDE = 1024;
    private static void check(boolean ok, String message) { if (!ok) throw new IllegalArgumentException(message); }
    private static long generation = 0;

    static boolean is(File dir) { return new File(dir, FILE).isFile() && !new File(dir, "model.json").isFile(); }

    static File install(File src, ModelConverter.Ini ini, int id, File dest, ModelConverter.Options o, ModelConverter.TextureDecoder decoder, StringBuilder log) throws Exception {
        check(id >= 10000000 && id <= 10001999, "纯贴图 MOD 需要选择角色");
        log.append("纯贴图 MOD：").append(ini.file.getName()).append("（只换游戏模型上的贴图，不需要网格和骨骼）\n");
        Map<String, Object> doc = TextureDiag.analyze(src, ini.file, id, dest.getName(), log);
        File base = ini.file.getAbsoluteFile().getParentFile();
        dest = dest.getCanonicalFile(); File parent = dest.getParentFile(); check(parent.isDirectory() || parent.mkdirs(), "无法创建输出根目录");
        check(o.replace || !dest.exists(), "输出目录已存在：" + dest);
        File tmp = Files.createTempDirectory(parent.toPath(), ".model-convert-").toFile();
        try {
            File dir = new File(tmp, "textures"); check(dir.mkdirs(), "无法创建贴图目录");
            List<Object> textures = new ArrayList<>(); Set<String> decoded = new HashSet<>(); long bytes = 0; int n = 0;
            for (Object raw : (List<?>) doc.get("resources")) {
                @SuppressWarnings("unchecked") Map<String, Object> r = (Map<String, Object>) raw; String name = (String) r.get("name");
                if (!Boolean.TRUE.equals(r.get("exists"))) { log.append("贴图 ").append(name).append(" 没有文件").append(r.get("error") == null ? "" : "：" + r.get("error")).append("，跳过引用它的规则\n"); continue; }
                try {
                    File f = TextureDiag.resolve(base, (String) r.get("file")); String ext = f.getName().toLowerCase(Locale.ROOT);
                    check(ext.endsWith(".dds") || ext.endsWith(".png"), "只支持 DDS / PNG：" + f.getName()); check(f.length() <= 256L << 20, "贴图过大：" + f.getName());
                    byte[] out = decoder.decode(Files.readAllBytes(f.toPath())); check(out.length >= 8, "贴图解码返回值无效");
                    ByteBuffer h = ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN); int w = h.getInt(0), y = h.getInt(4);
                    check(w > 0 && y > 0 && w <= 8192 && y <= 8192 && out.length == 8 + 4L * w * y, "贴图解码尺寸无效");
                    byte[] pixels = Arrays.copyOfRange(out, 8, out.length); int sw = w, sh = y;
                    while (w > MAX_SIDE || y > MAX_SIDE) { int nw = Math.max(1, w / 2), nh = Math.max(1, y / 2); byte[] s = new byte[nw * nh * 4];
                        for (int j = 0; j < nh; j++) for (int i = 0; i < nw; i++) for (int c = 0; c < 4; c++) { int sum = 0, count = 0;
                            for (int dy = 0; dy < 2 && j * 2 + dy < y; dy++) for (int dx = 0; dx < 2 && i * 2 + dx < w; dx++) { sum += pixels[((j * 2 + dy) * w + i * 2 + dx) * 4 + c] & 255; count++; }
                            s[(j * nw + i) * 4 + c] = (byte) (sum / count); }
                        pixels = s; w = nw; y = nh; }
                    String file = "textures/" + (n++) + ".rgba"; Files.write(new File(tmp, file).toPath(), pixels); bytes += pixels.length; decoded.add(name);
                    textures.add(Json.of("resource", name, "file", file, "width", w, "height", y, "bytes", (long) pixels.length,
                        "source", Json.of("file", r.get("file"), "width", sw, "height", sh, "format", r.get("format") == null ? "" : r.get("format"))));
                    log.append("贴图 ").append(name).append("：").append(sw).append("x").append(sh).append(sw != w ? " 缩到 " + w + "x" + y : "").append("（RGBA，").append(pixels.length >> 10).append(" KiB）\n");
                } catch (Exception e) { log.append("贴图 ").append(name).append(" 无法读取：").append(e.getMessage()).append("，跳过引用它的规则\n"); }
            }
            // The game side writes diffuse (_MainTex) and lightmap (_LightMapTex) only; anything else is diagnosed, not written.
            int usable = 0, rules = 0;
            for (Object raw : (List<?>) doc.get("rules")) { Map<?, ?> r = (Map<?, ?>) raw; if ("control".equals(r.get("kind"))) continue; rules++; boolean any = false;
                for (Object t : (List<?>) r.get("textures")) { Map<?, ?> b = (Map<?, ?>) t; any = any || (("diffuse".equals(b.get("role")) || "lightmap".equals(b.get("role"))) && decoded.contains(b.get("resource"))); }
                if (any) usable++; }
            check(usable > 0, "纯贴图 MOD 里没有能写入的贴图：" + rules + " 条规则都不是能确认的 diffuse / lightmap，或其贴图无法读取");
            List<Object> vars = new ArrayList<>(); Map<String, Object> defaults = new LinkedHashMap<>();
            for (Object raw : (List<?>) doc.get("variables")) { Map<?, ?> v = (Map<?, ?>) raw; List<?> values = (List<?>) v.get("values"); if (values == null || values.isEmpty() || values.size() > 64 || vars.size() >= 16) continue;
                String name = (String) v.get("name"); Object initial = v.get("initial"); vars.add(Json.of("name", name, "values", new ArrayList<Object>(values)));
                defaults.put(name, initial != null && values.contains(initial) ? initial : values.get(0)); }
            Map<String, Object> manifest = new LinkedHashMap<>();
            manifest.put("format", "gibox-texture-mod"); manifest.put("version", 1); manifest.put("name", dest.getName());
            manifest.put("generation", "t" + System.currentTimeMillis() + "-" + (++generation)); manifest.put("character", Json.of("id", id));
            if (o.sourcePath != null) manifest.put("source", Json.of("path", o.sourcePath, "signature", o.sourceSignature == null ? "" : o.sourceSignature));
            manifest.put("variants", Json.of("vars", vars, "default", defaults)); manifest.put("textures", textures); manifest.put("diag", doc);
            log.append("纯贴图 MOD：").append(usable).append(" / ").append(rules).append(" 条规则有可写入的贴图，").append(textures.size()).append(" 张贴图共 ").append(bytes >> 20).append(" MiB；进游戏后自动替换确认过的部件，匹配不上的等待映射\n");
            Files.write(new File(tmp, "conversion.log").toPath(), log.toString().getBytes(StandardCharsets.UTF_8));
            Files.write(new File(tmp, FILE).toPath(), Json.write(manifest).getBytes(StandardCharsets.UTF_8));   // last: the game reloads on it
            ModelConverter.commit(tmp, dest, o.replace); log.append("完成：").append(dest).append('\n'); return dest;
        } finally { if (tmp.exists()) ModelConverter.remove(tmp); }
    }
}
