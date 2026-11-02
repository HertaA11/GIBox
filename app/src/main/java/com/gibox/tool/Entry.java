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

/** The legacy entry (assets/xposed_init, Xposed API 82); the runtime itself is started by Loader. */
public final class Entry implements IXposedHookLoadPackage, IXposedHookZygoteInit {
    private static final String SELF = "com.gibox.tool";
    private static String moduleApk;

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

    /** Loader.install with the legacy API's log. */
    private static void install(final Context context) {
        Loader.install(context, moduleApk, new Loader.Log() {
            @Override public void log(String message, Throwable error) { if (error != null) XposedBridge.log(error); else XposedBridge.log(message); }
        });
    }
}
