package com.gibox.tool;

/** All native entry points are implemented in app/src/main/cpp. */
public final class NativeBridge {
    private static boolean loaded;
    private NativeBridge() {}
    public static synchronized void loadModule(String absoluteLibrary) {
        if (!loaded) { System.load(absoluteLibrary); loaded=true; }
    }
    public static synchronized byte[] decodeTexture(byte[] image) {
        if (image.length >= 8 && image[0] == (byte)137 && image[1] == 80 && image[2] == 78 && image[3] == 71
            && image[4] == 13 && image[5] == 10 && image[6] == 26 && image[7] == 10) return decodePng(image);
        if (!loaded) { System.loadLibrary("gibox"); loaded=true; }
        return decodeDds(image);
    }
    private static byte[] decodePng(byte[] source) {
        android.graphics.BitmapFactory.Options bounds = new android.graphics.BitmapFactory.Options();
        bounds.inJustDecodeBounds = true; android.graphics.BitmapFactory.decodeByteArray(source, 0, source.length, bounds);
        int w = bounds.outWidth, h = bounds.outHeight;
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || 8 + 4L * w * h > Integer.MAX_VALUE)
            throw new IllegalArgumentException("PNG 纹理尺寸无效");
        android.graphics.BitmapFactory.Options options = new android.graphics.BitmapFactory.Options();
        options.inPreferredConfig = android.graphics.Bitmap.Config.ARGB_8888;
        options.inScaled = false;
        // Genshin albedo alpha stores material data and is often zero. Premultiplication would erase its RGB.
        options.inPremultiplied = false;
        android.graphics.Bitmap bitmap = android.graphics.BitmapFactory.decodeByteArray(source, 0, source.length, options);
        if (bitmap == null) throw new IllegalArgumentException("PNG 纹理解码失败");
        try {
            if (bitmap.getWidth() != w || bitmap.getHeight() != h) throw new IllegalArgumentException("PNG 纹理尺寸不一致");
            java.nio.ByteBuffer out = java.nio.ByteBuffer.allocate(8 + 4 * w * h).order(java.nio.ByteOrder.LITTLE_ENDIAN);
            out.putInt(w).putInt(h); int[] row = new int[w];
            for (int y = 0; y < h; y++) {
                bitmap.getPixels(row, 0, w, 0, y, w, 1);
                for (int argb : row) out.put((byte)(argb >>> 16)).put((byte)(argb >>> 8)).put((byte)argb).put((byte)(argb >>> 24));
            }
            return out.array();
        } finally { bitmap.recycle(); }
    }
    /** The logic core's rehearsal of a Present program (app/src/main/cpp/logic_import.hpp): a JSON request, a JSON reply. */
    public static synchronized String rehearse(String request) {
        if (!loaded) { System.loadLibrary("gibox"); loaded=true; }
        return new String(rehearseUtf8(request.getBytes(java.nio.charset.StandardCharsets.UTF_8)), java.nio.charset.StandardCharsets.UTF_8);
    }
    private static native byte[] rehearseUtf8(byte[] request);
    /** The logic core as the converter's 3DMigoto backend: what the INI draws for one assignment (logic_import.hpp drawsJson). */
    public static synchronized String draws(String request) {
        if (!loaded) { System.loadLibrary("gibox"); loaded=true; }
        return new String(drawsUtf8(request.getBytes(java.nio.charset.StandardCharsets.UTF_8)), java.nio.charset.StandardCharsets.UTF_8);
    }
    private static native byte[] drawsUtf8(byte[] request);
    public static native void start(String packageName,String privateDir);
    private static native byte[] decodeDds(byte[] dds);
}
