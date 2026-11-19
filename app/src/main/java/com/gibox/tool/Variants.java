package com.gibox.tool;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import org.json.JSONObject;

/** The switchable variables of a converted model (model.json "variants"), their values, and the values they start with. */
public final class Variants {
    public final Map<String, List<Integer>> values = new LinkedHashMap<>();
    public final Map<String, Integer> defaults = new LinkedHashMap<>();
    /** Version 3 "replaceWhen" (the mod's own on/off switch for its model, e.g. Shenhe's $vision): shown as 显示替换 开/关. */
    public final List<String> gateVars = new ArrayList<>();
    public final List<List<Integer>> gateAllow = new ArrayList<>();
    /** Empty when the model has no variants or its model.json cannot be read. */
    public static Variants read(File model) {
        try { return fromManifest(ModelManifest.read(model)); }
        catch (Exception e) { return new Variants(); }
    }
    static Variants fromManifest(ModelManifest manifest) {
        Variants variants = new Variants();
        variants.values.putAll(manifest.vars);
        variants.defaults.putAll(manifest.defaults);
        variants.gateVars.addAll(manifest.replaceVars);
        variants.gateAllow.addAll(manifest.replaceAllow);
        return variants;
    }
    /** The value in effect: the one chosen in control.json, else the default combination's, else the first. */
    public int current(String pkg, String model, String var) {
        return Control.variable(pkg, model, var, fallback(var));
    }
    int fallback(String var) {
        List<Integer> v = values.get(var); return defaults.containsKey(var) ? defaults.get(var) : v == null || v.isEmpty() ? 0 : v.get(0);
    }
    public Map<String, Integer> current(String pkg, String model) {
        return choices(Control.load(pkg), model);
    }
    Map<String, Integer> choices(JSONObject control, String model) {
        JSONObject variables = control.optJSONObject("variables");
        JSONObject choices = variables == null ? null : variables.optJSONObject(model);
        Map<String, Integer> out = new LinkedHashMap<>();
        for (String v : values.keySet()) {
            int fallbackValue = fallback(v);
            out.put(v, choices == null ? fallbackValue : choices.optInt(v, fallbackValue));
        }
        return out;
    }

    // ---------------------------------------------------------------- 显示替换
    public boolean hasSwitch() { return !gateVars.isEmpty() && !gateAllow.isEmpty(); }
    /** Whether these values show the replacement model (no switch: always). */
    public boolean replacing(Map<String, Integer> now) {
        if (!hasSwitch()) return true;
        List<Integer> row = new ArrayList<>(); for (String v : gateVars) row.add(now.containsKey(v) ? now.get(v) : fallback(v));
        return gateAllow.contains(row);
    }
    /** A switch variable that is nothing but the switch (one variable, two values, one of them on) is shown only as 显示替换. */
    public boolean hidden(String var) {
        return hasSwitch() && gateVars.size() == 1 && gateVars.get(0).equals(var) && gateAllow.size() == 1 && values.getOrDefault(var, new ArrayList<>()).size() == 2;
    }
    /** The switch variables' values that turn the replacement on or off; empty if nothing needs to change. Off prefers the
     *  mod's own initial values, on the first allowed row; every other variable keeps its value. */
    public Map<String, Integer> switchTo(boolean on, Map<String, Integer> now) {
        Map<String, Integer> out = new LinkedHashMap<>(); if (!hasSwitch() || replacing(now) == on) return out;
        List<Integer> pick = null;
        if (on) pick = gateAllow.get(0);
        else {
            List<Integer> initial = new ArrayList<>(); for (String v : gateVars) initial.add(fallback(v));
            if (!gateAllow.contains(initial)) pick = initial;
            else pick = firstOff(0, new ArrayList<>());
        }
        if (pick == null) return out;
        for (int i = 0; i < gateVars.size(); i++) out.put(gateVars.get(i), pick.get(i));
        return out;
    }
    private List<Integer> firstOff(int i, List<Integer> row) {
        if (i == gateVars.size()) return gateAllow.contains(row) ? null : new ArrayList<>(row);
        for (int value : values.getOrDefault(gateVars.get(i), new ArrayList<>())) {
            row.add(value); List<Integer> r = firstOff(i + 1, row); row.remove(row.size() - 1); if (r != null) return r;
        }
        return null;
    }
    static Map<String, Integer> merged(Map<String, Integer> now, Map<String, Integer> change) { Map<String, Integer> m = new HashMap<>(now); m.putAll(change); return m; }
}
