package com.gibox.tool;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.IXposedHookZygoteInit;
import de.robv.android.xposed.XC_MethodHook;
import de.robv.android.xposed.XposedBridge;
import de.robv.android.xposed.XposedHelpers;
import de.robv.android.xposed.callbacks.XC_LoadPackage;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

public final class Entry implements IXposedHookLoadPackage, IXposedHookZygoteInit {
    private static final String SELF = "com.gibox.tool";
    private static String moduleApk;
    private static final AtomicBoolean loading = new AtomicBoolean(false);

    @Override public void initZygote(StartupParam param) { moduleApk = param.modulePath; }

    @Override public void handleLoadPackage(final XC_LoadPackage.LoadPackageParam param) {
        // GIBox itself in the scope: tell its UI that the module is active (and which framework API loaded it).
        if (SELF.equals(param.packageName)) {
            try {
                Class<?> status = XposedHelpers.findClass(SELF + ".ModuleStatus", param.classLoader);
                XposedHelpers.setStaticBooleanField(status, "active", true);
                XposedHelpers.setStaticIntField(status, "api", XposedBridge.getXposedVersion());
            } catch (Throwable e) { XposedBridge.log(e); }
            return;
        }
        if (!Control.supported(param.packageName) || !param.packageName.equals(param.processName)) return;
        XposedHelpers.findAndHookMethod(Application.class, "attach", Context.class, new XC_MethodHook() {
            @Override protected void afterHookedMethod(MethodHookParam hook) {
                install((Context) hook.args[0]);
            }
        });
        XposedHelpers.findAndHookMethod(Activity.class, "onResume", new XC_MethodHook() {
            @Override protected void afterHookedMethod(MethodHookParam hook) {
                final Activity activity = (Activity) hook.thisObject;
                if (!Control.supported(activity.getPackageName())) return;
                // No in-game panel for now (an ImGui overlay will replace it); everything is controlled from the app.
                install(activity);
            }
        });
    }

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

    /** Where install() reports to: XposedBridge (legacy API) or the modern API's log (ModernEntry). */
    interface Log { void log(String message, Throwable error); }

    private static void install(final Context context) {
        install(context, moduleApk, new Log() {
            @Override public void log(String message, Throwable error) { if (error != null) XposedBridge.log(error); else XposedBridge.log(message); }
        });
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
