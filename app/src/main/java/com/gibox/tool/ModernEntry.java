package com.gibox.tool;

import android.app.Activity;
import android.app.Application;
import android.app.Instrumentation;
import android.util.Log;
import io.github.libxposed.api.XposedModule;
import io.github.libxposed.api.XposedModuleInterface;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;

/**
 * The entry for frameworks with the modern Xposed API (102, listed in META-INF/xposed/java_init.list): the same work as
 * Entry (legacy API 82) in a game's main process, through Loader (no legacy classes: API 102 forbids them), the runtime started when the game's Application is created
 * (Instrumentation.callApplicationOnCreate, public API: no hidden-API reflection) and again after Activity.onResume.
 * A framework that takes this entry ignores assets/xposed_init, and it hands the app its service, through which the
 * app reads the real scope (LspService).
 *
 * Every step is also written to GIBox/modern-entry.txt in the game's media folder and to logcat (tag GIBox), so a
 * device where nothing happens shows how far the framework got.
 *
 * 0.7.66 / 0.7.67 called Entry.install here: loading Entry needs its legacy interfaces, which a module targeting API
 * 102 may not load, so the trace stopped after "Application created" (NoClassDefFoundError, caught by the framework).
 */
public final class ModernEntry extends XposedModule {
    private String process = "";
    private boolean game;

    @Override public void onModuleLoaded(XposedModuleInterface.ModuleLoadedParam param) {
        process = param.getProcessName();
        game = Control.supported(process);
        if (game) step("module loaded in " + process + " (" + getFrameworkName() + " " + getFrameworkVersion() + ", API " + getApiVersion() + ")", null);
    }

    @Override public void onPackageReady(XposedModuleInterface.PackageReadyParam param) {
        String pkg = param.getPackageName();
        if (!game || !param.isFirstPackage() || !Control.supported(pkg) || !pkg.equals(process)) return;
        step("package ready: " + pkg, null);
        final String apk = getModuleApplicationInfo().sourceDir;
        final Loader.Log log = this::step;
        try {
            Method create = Instrumentation.class.getDeclaredMethod("callApplicationOnCreate", Application.class);
            hook(create).intercept(chain -> {
                Object result = chain.proceed();
                step("Application created", null);
                Loader.install((Application) chain.getArg(0), apk, log);
                return result;
            });
            Method resume = Activity.class.getDeclaredMethod("onResume");
            hook(resume).intercept(chain -> {
                Object result = chain.proceed();
                Activity activity = (Activity) chain.getThisObject();
                // No in-game panel for now (an ImGui overlay will replace it); everything is controlled from the app.
                if (Control.supported(activity.getPackageName())) Loader.install(activity, apk, log);
                return result;
            });
            step("hooks installed (Instrumentation.callApplicationOnCreate, Activity.onResume); module APK " + apk, null);
        } catch (Throwable e) { step("hooks not installed", e); }
    }

    /** One step: the framework's log, logcat and GIBox/modern-entry.txt (appended, best effort). */
    private void step(String message, Throwable error) {
        try { if (error != null) log(Log.ERROR, "GIBox", message, error); else log(Log.INFO, "GIBox", message); } catch (Throwable ignored) {}
        try { if (error != null) Log.e("GIBox", message, error); else Log.i("GIBox", message); } catch (Throwable ignored) {}
        try {
            File file = new File(Control.shared(process), "modern-entry.txt");
            File dir = file.getParentFile();
            if (dir != null && (dir.isDirectory() || dir.mkdirs())) {
                try (FileOutputStream out = new FileOutputStream(file, true)) {
                    out.write((System.currentTimeMillis() + " " + message + (error != null ? ": " + error : "") + "\n").getBytes(StandardCharsets.UTF_8));
                }
            }
        } catch (Throwable ignored) {}
    }
}
