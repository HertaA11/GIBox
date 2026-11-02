package com.gibox.tool;

import android.content.Context;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Starting the runtime in a game process, shared by both entries: Entry (legacy API 82) and ModernEntry (API 102).
 * It must not refer to any de.robv.android.xposed class, not even through a superclass or interface: a module that
 * targets API 102 cannot load those classes ("Libxposed modules can not call legacy de.robv.android.xposed APIs"), and
 * resolving one fails the whole class (0.7.66 / 0.7.67: NoClassDefFoundError for Entry, so install never ran).
 */
final class Loader {
    private Loader() {}
    private static final AtomicBoolean loading = new AtomicBoolean(false);

    /** Where install() reports to: XposedBridge (Entry) or the modern API's log (ModernEntry). */
    interface Log { void log(String message, Throwable error); }

    private static void extract(ZipFile zip, String entry, File out) throws Exception {
        ZipEntry e = zip.getEntry(entry); if (e == null) throw new IllegalStateException("APK entry missing: " + entry);
        File temp = new File(out.getPath() + ".tmp");
        if (temp.exists() && !temp.delete()) throw new IllegalStateException("Cannot remove old temporary file");
        try (InputStream in = zip.getInputStream(e); FileOutputStream stream = new FileOutputStream(temp)) {
            byte[] b = new byte[65536]; int n; while ((n = in.read(b)) != -1) stream.write(b, 0, n);
            stream.getFD().sync();
        }
        if (out.exists() && !out.delete()) throw new IllegalStateException("Cannot replace " + out);
        if (!temp.renameTo(out)) throw new IllegalStateException("Cannot install " + out);
    }

    /** In a game process, once: the mark for the app, then libgibox.so out of the module APK, loaded and started. */
    static void install(final Context context, final String apk, final Log log) {
        if (!loading.compareAndSet(false, true)) return;
        new Thread(new Runnable() {
            @Override public void run() {
                File privateDir = new File(context.getFilesDir(), "gibox");
                // The app picks the game it works for by this mark: LSPosed injected GIBox here, and when.
                try {
                    File mark = new File(Control.shared(context.getPackageName()), "injected.txt");
                    File dir = mark.getParentFile();
                    if (dir != null && (dir.isDirectory() || dir.mkdirs())) Files.write(mark.toPath(), String.valueOf(System.currentTimeMillis()).getBytes(StandardCharsets.UTF_8));
                } catch (Throwable ignored) {}
                try {
                    if (apk == null) throw new IllegalStateException("Xposed module APK path unavailable");
                    File runtime = new File(privateDir, "runtime");
                    if (!runtime.isDirectory() && !runtime.mkdirs()) throw new IllegalStateException("Cannot create runtime directory");
                    File library = new File(runtime, "libgibox.so");
                    try (ZipFile zip = new ZipFile(apk)) {
                        extract(zip, "lib/arm64-v8a/libgibox.so", library);
                    }
                    library.setReadable(true, true); library.setExecutable(true, true);
                    log.log("GIBox: libgibox.so extracted (" + library.length() + " bytes), loading", null);
                    NativeBridge.loadModule(library.getAbsolutePath());
                    log.log("GIBox: libgibox.so loaded, starting the runtime", null);
                    // Mods are imported and converted in the app; the game only loads finished models (no scanning here).
                    NativeBridge.start(context.getPackageName(),privateDir.getAbsolutePath());
                    log.log("GIBox: runtime loaded for " + context.getPackageName(), null);
                } catch (Throwable e) {
                    loading.set(false);
                    try { log.log("GIBox: runtime not loaded", e); } catch (Throwable ignored) {}
                    try { privateDir.mkdirs(); Files.write(new File(privateDir, "bootstrap-error.txt").toPath(), e.toString().getBytes(StandardCharsets.UTF_8)); } catch (Exception ignored) {}
                }
            }
        }, "gibox-loader").start();
    }
}
