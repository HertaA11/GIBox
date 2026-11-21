# Xposed loads these classes by name from assets/xposed_init and META-INF/xposed/java_init.list.
-keep class com.gibox.tool.Entry {
    public <init>();
    public void initZygote(de.robv.android.xposed.IXposedHookZygoteInit$StartupParam);
    public void handleLoadPackage(de.robv.android.xposed.callbacks.XC_LoadPackage$LoadPackageParam);
}

-keep class com.gibox.tool.ModernEntry {
    public <init>();
    public void onModuleLoaded(io.github.libxposed.api.XposedModuleInterface$ModuleLoadedParam);
    public void onPackageReady(io.github.libxposed.api.XposedModuleInterface$PackageReadyParam);
}

# The legacy entry writes these fields by name through the app's class loader.
-keep class com.gibox.tool.ModuleStatus {
    public static boolean active;
    public static int api;
}

# libgibox exports Java_com_gibox_tool_NativeBridge_* symbols.
-keepclasseswithmembers class com.gibox.tool.NativeBridge {
    native <methods>;
}
