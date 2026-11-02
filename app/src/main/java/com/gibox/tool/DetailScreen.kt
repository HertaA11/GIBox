package com.gibox.tool

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Build
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.AssistChip
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.unit.dp
import com.alorma.compose.settings.ui.expressive.SettingsGroup
import com.alorma.compose.settings.ui.expressive.SettingsMenuLink
import com.alorma.compose.settings.ui.expressive.SettingsSwitch
import java.io.File

/** One mod: enable, name, character, variants (款式), conversion record, delete. */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun DetailScreen(state: AppState, name: String) {
    val mod = state.mod(name)
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        topBar = {
            LargeFlexibleTopAppBar(
                colors = pageBarColors(),
                title = { Text(state.label(name)) },
                subtitle = { Text(if (mod == null) "已删除" else who(mod)) },
                navigationIcon = { BackButton { state.back() } },
                scrollBehavior = scroll,
            )
        },
    ) { padding ->
        if (mod == null) {
            Column(Modifier.padding(padding)) { EmptyNote("这个 MOD 已不存在") }
            return@Scaffold
        }
        val variants = remember(mod.model, state.revision) { mod.model?.let { Variants.read(it) } }
        val manifest = remember(mod.model, state.revision) { mod.model?.let { runCatching { ModelManifest.read(it) }.getOrNull() } }
        val keys = manifest?.keys?.toList().orEmpty()
        val driven = manifest?.driven == true
        val logicCore = remember(state.revision) { state.experiment("logicCore") }
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = padding.calculateTopPadding(), bottom = padding.calculateBottomPadding() + 24.dp),
        ) {
            item {
                SettingsGroup(title = { SectionTitle("状态") }, verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)) {
                    SettingsSwitch(
                        state = mod.converted && state.selected(mod.name),
                        title = { Text("启用") },
                        subtitle = { Text(if (mod.converted) "进游戏后替换这个角色" else "还没转换：先选择角色") },
                        enabled = mod.converted,
                        shapes = ListItemDefaults.segmentedShapes(0, 1),
                        onCheckedChange = { state.setSelected(mod, it) },
                    )
                }
            }
            item {
                val label = state.label(mod.name)
                SettingsGroup(title = { SectionTitle("名称") }) {
                    SettingsMenuLink(
                        title = { Text("重命名") },
                        subtitle = { Text(label) },
                        icon = { Icon(Icons.Filled.Edit, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(0, 1),
                        onClick = { state.renaming = mod.name },
                    )
                }
            }
            if (mod.source != null) item {
                val actions = if (mod.converted && mod.id != 0) 2 else 1
                SettingsGroup(title = { SectionTitle("角色") }, verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)) {
                    SettingsMenuLink(
                        title = { Text(if (mod.converted) "更换角色并重新转换" else "选择角色并转换") },
                        subtitle = { Text(who(mod) + if (!mod.outdated) "" else if (mod.reason.isNotEmpty()) "（${mod.reason}，需要重新转换）" else "（已更改，还没重新转换）") },
                        icon = { Icon(Icons.Filled.Person, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(0, actions),
                        onClick = { state.open(Screen.Pick(mod.name, mod.sourceName, null, mod.source, mod.id)) },
                    )
                    if (actions == 2) SettingsMenuLink(
                        title = { Text("重新转换") },
                        subtitle = { Text("改了 Mods 里的文件后用；角色不变") },
                        icon = { Icon(Icons.Filled.Build, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(1, 2),
                        onClick = { state.convert(mod.source, mod.id) },
                    )
                }
            }
            val schemes = mod.source?.let { state.schemes(it) }.orEmpty()
            if (mod.source != null && state.logLevel == "debug") item {
                SettingsGroup(title = { SectionTitle("动画转换实验（调试）") }) {
                    SettingsSwitch(
                        state = state.animationBoneFallback(mod.source),
                        title = { Text("忽略越界骨骼关联") },
                        subtitle = { Text("只作用于此 MOD 的逐帧动画：保留顶点的有效权重并重新归一化；需完整骨骼库，可能改变新增部件的运动，保存后重新转换") },
                        enabled = state.busy == null,
                        shapes = ListItemDefaults.segmentedShapes(0, 1),
                        onCheckedChange = { state.setAnimationBoneFallback(mod, it) },
                    )
                }
            }
            if (schemes.size > 1) item {
                val current = state.scheme(mod.source)
                SettingsGroup(title = { SectionTitle("INI 方案") }, verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)) {
                    SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(0, 1)) {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text(if (mod.converted) "换一套会重新转换" else "选一套，再选择角色转换", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                for (s in schemes) FilterChip(selected = s == current, onClick = { state.chooseScheme(mod, s) }, label = { Text(schemeLabel(s, schemes)) })
                            }
                        }
                    }
                }
            }
            if (variants != null && (variants.values.keys.any { !variants.hidden(it) } || variants.hasSwitch())) item {
                SettingsGroup(title = { SectionTitle("款式") }, verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)) {
                    val names = variants.values.keys.filter { !variants.hidden(it) }
                    val switch = variants.hasSwitch()
                    val rows = names.size + (if (switch) 1 else 0)
                    if (switch) {
                        val on = variants.replacing(variants.current(state.game, mod.name))
                        SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(0, rows)) {
                            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                Text("显示替换", style = MaterialTheme.typography.titleSmall)
                                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    FilterChip(selected = on, onClick = { state.setReplacing(mod, variants, true) }, label = { Text("开") })
                                    FilterChip(selected = !on, onClick = { state.setReplacing(mod, variants, false) }, label = { Text("关") })
                                }
                            }
                        }
                    }
                    names.forEachIndexed { i, v ->
                        val index = i + (if (switch) 1 else 0)
                        val current = variants.current(state.game, mod.name, v)
                        SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(index, rows)) {
                            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                Text(v.removePrefix("$"), style = MaterialTheme.typography.titleSmall)
                                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    for (value in variants.values[v].orEmpty()) {
                                        FilterChip(
                                            selected = value == current,
                                            onClick = { state.setVariable(mod, v, value) },
                                            label = { Text(value.toString()) },
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (logicCore && keys.isNotEmpty()) item {
                // The mod's own keys, as on PC: the logic core in the game runs what the key does (its run lists, conditions,
                // assignments); 款式 above stays the App's own table and its fallback.
                SettingsGroup(title = { SectionTitle("按键（逻辑核心）") }, verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)) {
                    SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(0, 1)) {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text(
                                "游戏里按一次（需模型显示中且由逻辑核心驱动）" +
                                    if (driven) "。这套 MOD 自己选款式，上面的款式表不起作用" else "",
                                style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                for (k in keys) {
                                    val name = k.section.substring(3)
                                    AssistChip(
                                        onClick = { state.pressKey(mod, k) },
                                        label = { Text(name + if (k.type == "toggle") "（开关）" else if (k.back) "（下一个）" else "（切换）") },
                                    )
                                    if (k.back) AssistChip(onClick = { state.pressKey(mod, k, true) }, label = { Text("$name（上一个）") })
                                }
                            }
                        }
                    }
                }
            }
            item {
                val record = mod.model?.let { File(it, "conversion.log") }?.takeIf { it.isFile }
                val count = if (record != null) 2 else 1
                SettingsGroup(title = { SectionTitle("其他") }, verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)) {
                    if (record != null) SettingsMenuLink(
                        title = { Text("转换记录") },
                        subtitle = { Text("部件、骨骼、款式变量") },
                        icon = { Icon(Icons.Filled.Info, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(0, count),
                        onClick = { state.prompt = Prompt("转换记录", record.readText()) },
                    )
                    SettingsMenuLink(
                        title = { Text("删除", color = MaterialTheme.colorScheme.error) },
                        subtitle = { Text(if (mod.source != null) "删除 Mods 里的副本和转换好的模型" else "删除转换好的模型") },
                        icon = { Icon(Icons.Filled.Delete, contentDescription = null, tint = MaterialTheme.colorScheme.error) },
                        shapes = ListItemDefaults.segmentedShapes(count - 1, count),
                        onClick = { state.delete(mod) },
                    )
                }
            }
            if (mod.source == null) item {
                SettingsGroup(title = { SectionTitle("说明") }) {
                    SettingsMenuLink(
                        title = { Text("没有源文件") },
                        subtitle = { Text("手动转换的模型（Mods 里没有原件），只能启用、停用和删除。要换角色请重新导入 MOD。") },
                        icon = { Icon(Icons.Filled.Info, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(0, 1),
                        onClick = {},
                    )
                }
            }
        }
    }
}
