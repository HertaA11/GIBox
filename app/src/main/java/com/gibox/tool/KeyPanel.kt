package com.gibox.tool

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.material3.AssistChip
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.FilterChip
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.unit.dp
import com.alorma.compose.settings.ui.expressive.SettingsGroup
import com.alorma.compose.settings.ui.expressive.SettingsMenuLink
import com.alorma.compose.settings.ui.expressive.SettingsSwitch

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
private fun keyLine(k: ModelManifest.Key, what: String) = (bindLabel(k).takeIf { it.isNotEmpty() }?.let { "PC 按键 $it · " } ?: "") + what

/**
 * The MOD's own [Key] sections as a switch panel, the way 3DMigoto runs them on PC (the logic core in the game executes
 * each key: its conditions, run lists, transitions):
 * - a cycle: its presets as chips, chosen directly ("select", activated exactly as stepping to that preset would), plus
 *   下一个 / 上一个; a cycle whose presets carry a condition or a run (choosing one directly could differ from reaching
 *   it on PC) only steps, as on PC;
 * - activate keys that set the same variables (a MOD's own 1 2 3 keys): one row of choices; any other activate key: a press;
 * - toggle and hold: switches.
 * Which preset / choice / switch is on comes from the game (status.json "keys", 3DMigoto's smart cycle resync).
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun KeyPanel(state: AppState, mod: Importer.Mod, keys: List<ModelManifest.Key>, driven: Boolean, logicCore: Boolean) {
    val states = state.status.keys[mod.name]
    fun stateOf(i: Int, k: ModelManifest.Key) = states?.getOrNull(i)?.takeIf { it.section == k.section }
    val rows = if (logicCore) keyRows(keys) else emptyList()
    val count = rows.size + 1
    SettingsGroup(title = { SectionTitle("按键切换") }, verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)) {
        SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(0, count)) {
            Text(
                when {
                    !logicCore -> "需在 设置 → 调试 打开“使用 3DMigoto 进行转换”"
                    states == null -> "游戏里显示这个模型时生效（现在没有运行）"
                    else -> "和 PC 上按键一样，按 MOD 自己的规则切换"
                } + if (driven && logicCore) "。款式由 MOD 自己决定，上面的款式表不起作用" else "",
                style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        rows.forEachIndexed { r, row ->
            val shapes = ListItemDefaults.segmentedShapes(r + 1, count)
            when (row) {
                is Choice -> SegmentedListItem(shapes = shapes) {
                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text(row.members.first().second.presets[0].values.keys.sorted().joinToString("、") { it.removePrefix("$") }, style = MaterialTheme.typography.titleSmall)
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            for ((i, k) in row.members) FilterChip(
                                selected = stateOf(i, k)?.matches == true,
                                onClick = { state.pressKey(mod, k) },
                                label = { Text(bindLabel(k).ifEmpty { keyName(k) }) },
                            )
                        }
                    }
                }
                is Single -> {
                    val k = row.key; val st = stateOf(row.index, k)
                    when (k.type) {
                        "toggle" -> SettingsSwitch(
                            state = st?.active == true, title = { Text(keyName(k)) }, subtitle = { Text(keyLine(k, "开关")) },
                            shapes = shapes, onCheckedChange = { state.pressKey(mod, k) },
                        )
                        "hold" -> SettingsSwitch(
                            state = st?.active == true, title = { Text(keyName(k)) }, subtitle = { Text(keyLine(k, "打开即按住，关掉即松开")) },
                            shapes = shapes, onCheckedChange = { state.holdKey(mod, k, it) },
                        )
                        "cycle" -> SegmentedListItem(shapes = shapes) {
                            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                Text(keyName(k), style = MaterialTheme.typography.titleSmall)
                                val special = k.presets.any { it.special }
                                Text(
                                    keyLine(k, if (k.selectable()) "共 ${k.presets.size} 个，点哪个切到哪个" else (if (special) "带条件或附加命令，只能按顺序切换" else "按顺序切换") +
                                        if (st != null && st.current >= 0) "，现在第 ${st.current + 1} 个" else ""),
                                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                                if (k.selectable()) FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    k.presets.indices.forEach { i ->
                                        FilterChip(selected = st?.current == i, onClick = { state.selectKey(mod, k, i) }, label = { Text("${i + 1}") })
                                    }
                                }
                                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    if (k.back) AssistChip(onClick = { state.pressKey(mod, k, true) }, label = { Text("上一个") })
                                    AssistChip(onClick = { state.pressKey(mod, k) }, label = { Text("下一个") })
                                }
                            }
                        }
                        else -> SettingsMenuLink(
                            title = { Text(keyName(k)) },
                            subtitle = { Text(keyLine(k, if (st?.matches == true) "按一下 · 已生效" else "按一下")) },
                            shapes = shapes, onClick = { state.pressKey(mod, k) },
                        )
                    }
                }
            }
        }
    }
}
