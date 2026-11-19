package com.gibox.tool

import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.structuralEqualityPolicy
import androidx.compose.runtime.remember
import com.itos.coui.layout.Divider
import com.itos.coui.layout.Group

/** A row of the key panel: one key, or activate keys that set the same variables (a MOD's own "1 2 3" keys). */
private sealed interface KeyRow
private class Single(val index: Int, val key: ModelManifest.Key) : KeyRow
private class Choice(val members: List<Pair<Int, ModelManifest.Key>>) : KeyRow

/** Activate keys that only assign plain values to the same variables become one row of choices; everything else one row each. */
private fun keyRows(keys: List<ModelManifest.Key>): List<KeyRow> {
    fun plain(k: ModelManifest.Key) = k.type == "activate" && k.presets.size == 1 && !k.presets[0].special && k.presets[0].values.isNotEmpty()
    val groups = keys.withIndex().filter { plain(it.value) }.groupBy { it.value.presets[0].values.keys.sorted().joinToString(",") }
    val out = ArrayList<KeyRow>(); val done = HashSet<String>()
    keys.forEachIndexed { i, k ->
        val group = if (plain(k)) k.presets[0].values.keys.sorted().joinToString(",") else null
        val members = group?.let { groups[it] }
        if (group != null && members != null && members.size > 1) { if (done.add(group)) out.add(Choice(members.map { it.index to it.value })) }
        else out.add(Single(i, k))
    }
    return out
}

/** The section without its "Key" prefix: KeySwapHair → SwapHair. */
private fun keyName(k: ModelManifest.Key) = k.section.drop(3).trim('_', ' ', '.').ifEmpty { k.section }
/** key = values as a label: ctrl alt 1 → CTRL+ALT+1, VK_UP → UP; no_modifiers and the like are left out. */
private fun bindLabel(k: ModelManifest.Key): String = k.binds.firstOrNull()?.split(' ', '\t')
    ?.filter { it.isNotBlank() && !it.lowercase().startsWith("no_") }
    ?.joinToString("+") { it.uppercase().removePrefix("VK_") }.orEmpty()
private fun keyLine(k: ModelManifest.Key, what: String) = listOfNotNull(bindLabel(k).takeIf { it.isNotEmpty() }?.let { "按键 $it" }, what.takeIf { it.isNotEmpty() }).joinToString(" · ")

/**
 * The MOD's own [Key] sections as a switch panel, the way 3DMigoto runs them on PC (the logic core in the game executes
 * each key: its conditions, run lists, transitions):
 * - a cycle: its presets as chips, chosen directly ("select", activated exactly as stepping to that preset would);
 *   上一个 / 下一个 as on PC only with the debug switch 逐个切换按钮 (steps); a cycle whose presets carry a condition or a
 *   run (choosing one directly could differ from reaching it on PC) only steps, so it always has them;
 * - activate keys that set the same variables (a MOD's own 1 2 3 keys): one row of choices; any other activate key: a press;
 * - toggle and hold: switches.
 * Which preset / choice / switch is on comes from the game (status.json "keys", 3DMigoto's smart cycle resync).
 */
@Composable
internal fun KeyPanel(state: AppState, mod: Importer.Mod, manifest: ModelManifest, steps: Boolean) {
    val keys = manifest.keys
    val states by remember(state, mod.name) {
        derivedStateOf(structuralEqualityPolicy()) { state.status.keys[mod.name] }
    }
    fun keyState(index: Int, key: ModelManifest.Key) = states?.getOrNull(index)?.takeIf { it.section == key.section }
    val rows = remember(keys) { keyRows(keys) }
    Group(title = "按键切换") {
        ActionRow(when {
            states == null -> "游戏里显示模型时生效"
            else -> "按 MOD 自带规则切换"
        } + if (manifest.driven) "；款式表不起作用" else "")
        rows.forEach { row ->
            Divider()
            when (row) {
                is Choice -> {
                    val title = row.members.first().second.presets[0].values.keys.sorted().joinToString("、") { it.removePrefix("$") }
                    val selected = row.members.firstOrNull { (i, key) -> keyState(i, key)?.matches == true }?.first
                    ChoiceRow(title, selected, row.members.map { (i, key) -> i to bindLabel(key).ifEmpty { keyName(key) } }) { i ->
                        state.pressKey(mod, row.members.first { it.first == i }.second, manifest.defaults)
                    }
                }
                is Single -> {
                    val key = row.key
                    val snapshot = keyState(row.index, key)
                    when (key.type) {
                        "toggle" -> ToggleRow(keyName(key), snapshot?.active == true, keyLine(key, "开关")) { state.pressKey(mod, key, manifest.defaults) }
                        "hold" -> ToggleRow(keyName(key), snapshot?.active == true, keyLine(key, "按住")) { state.holdKey(mod, key, it) }
                        "cycle" -> {
                            if (key.selectable()) NumberedChips(
                                keyName(key), key.presets.indices.toList(), snapshot?.current,
                                summary = keyLine(key, "").ifEmpty { null }, label = { "${it + 1}" },
                            ) { state.selectKey(mod, key, it) }
                            else ActionRow(keyName(key), keyLine(key, "只能逐个切" +
                                if (snapshot != null && snapshot.current >= 0) "，第 ${snapshot.current + 1} 个" else ""))
                            if (steps || !key.selectable()) {
                                if (key.back) ActionRow("上一个") { state.pressKey(mod, key, manifest.defaults, back = true) }
                                ActionRow("下一个") { state.pressKey(mod, key, manifest.defaults) }
                            }
                        }
                        else -> ActionRow(keyName(key), keyLine(key, if (snapshot?.matches == true) "已生效" else "").ifEmpty { null }) {
                            state.pressKey(mod, key, manifest.defaults)
                        }
                    }
                }
            }
        }
    }
}
