package com.gibox.tool;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.util.Log;
import io.github.libxposed.api.XposedModule;
import io.github.libxposed.api.XposedModuleInterface;
import java.lang.reflect.Method;

/**
 * The entry for frameworks with the modern Xposed API (101+, listed in META-INF/xposed/java_init.list): the same work as
 * Entry (legacy API 82) in a game's main process, the runtime started after Application.attach and again after
 * Activity.onResume, hooked through the modern API. A framework that takes this entry ignores assets/xposed_init, and it
 * hands the app its service, through which the app reads the real scope (LspService).
 */
public final class ModernEntry extends XposedModule {
    private String process = "";

    @Override public void onModuleLoaded(XposedModuleInterface.ModuleLoadedParam param) { process = param.getProcessName(); }

    @Override public void onPackageReady(XposedModuleInterface.PackageReadyParam param) {
        String pkg = param.getPackageName();
        if (!param.isFirstPackage() || !Control.supported(pkg) || !pkg.equals(process)) return;
        final String apk = getModuleApplicationInfo().sourceDir;
        final Entry.Log log = (message, error) -> { if (error != null) log(Log.ERROR, "GIBox", message, error); else log(Log.INFO, "GIBox", message); };
        try {
            Method attach = Application.class.getDeclaredMethod("attach", Context.class);
            hook(attach).intercept(chain -> {
                Object result = chain.proceed();
                Entry.install((Context) chain.getArg(0), apk, log);
                return result;
            });
            Method resume = Activity.class.getDeclaredMethod("onResume");
            hook(resume).intercept(chain -> {
                Object result = chain.proceed();
                Activity activity = (Activity) chain.getThisObject();
                // No in-game panel for now (an ImGui overlay will replace it); everything is controlled from the app.
                if (Control.supported(activity.getPackageName())) Entry.install(activity, apk, log);
                return result;
            });
        } catch (Throwable e) { log(Log.ERROR, "GIBox", "hooks not installed", e); }
    }
}
