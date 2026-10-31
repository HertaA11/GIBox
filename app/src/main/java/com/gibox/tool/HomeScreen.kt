package com.gibox.tool

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

/**
 * Home: top bar (日志 / 设置), module card, the replacement switch, import, and the mods by character (a row per
 * character with its number of mods; the mods themselves are on the character's page). 启动 is the floating button.
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun HomeScreen(state: AppState, pickZip: () -> Unit) {
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text("GIBox") },
                subtitle = { Text("原神模型导入器") },
                actions = {
                    IconButton(onClick = { state.open(Screen.Log) }) { Icon(Icons.AutoMirrored.Filled.List, contentDescription = "日志") }
                    IconButton(onClick = { state.open(Screen.Settings) }) { Icon(Icons.Filled.Settings, contentDescription = "设置") }
                },
                scrollBehavior = scroll,
            )
        },
        // 启动: the floating action button, bottom right.
        floatingActionButton = {
            FloatingActionButton(onClick = { state.launch() }) { Icon(Icons.Filled.PlayArrow, contentDescription = "启动") }
        },
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = padding.calculateTopPadding() + 8.dp, bottom = padding.calculateBottomPadding() + 96.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item { ModuleCard(state) }
            item { ReplacementSwitch(state) }
            item { ToolRow(state, pickZip) }
            item {
                Text("Mods（${state.mods.size}）", modifier = Modifier.padding(start = 4.dp, top = 8.dp), style = MaterialTheme.typography.titleMedium)
            }
            item { CharacterList(state) }
        }
    }
}

/** Whether LSPosed loaded the module and its version; without file access, the button to grant it. */
@Composable
private fun ModuleCard(state: AppState) {
    val active = state.moduleActive
    val warn = !active || !state.storage
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.extraLarge,
        colors = CardDefaults.cardColors(
            containerColor = if (warn) MaterialTheme.colorScheme.errorContainer else MaterialTheme.colorScheme.primaryContainer,
            contentColor = if (warn) MaterialTheme.colorScheme.onErrorContainer else MaterialTheme.colorScheme.onPrimaryContainer,
        ),
    ) {
        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(if (active) Icons.Filled.CheckCircle else Icons.Filled.Warning, contentDescription = null)
                Spacer(Modifier.width(12.dp))
                Text(if (active) "模块已激活" else "模块未激活", style = MaterialTheme.typography.titleLarge)
            }
            Text("模块版本 " + state.appVersion, style = MaterialTheme.typography.bodyMedium)
            if (!active) Text("在 LSPosed 里启用 GIBox，作用域勾选原神和 GIBox 自己，然后重新打开 GIBox、彻底重启游戏。", style = MaterialTheme.typography.bodyMedium)
            if (!state.storage) {
                Text("还没有文件访问权限：模型放在原神的目录里，需要“所有文件访问权限”。", style = MaterialTheme.typography.bodyMedium)
                FilledTonalButton(onClick = { state.grantStorage() }) { Text("授予文件访问权限") }
            }
        }
    }
}

/** The replacement master switch, right below the module card: a plain white row (the lowest surface tone). */
@Composable
private fun ReplacementSwitch(state: AppState) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.extraLarge,
        color = MaterialTheme.colorScheme.surfaceContainerLowest,
    ) {
        Row(Modifier.padding(horizontal = 20.dp, vertical = 14.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("模型替换", style = MaterialTheme.typography.titleMedium)
                Text(if (state.enabled) "已开启：进游戏后替换已启用的 MOD" else "已关闭：游戏里显示原模型", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            Switch(checked = state.enabled, onCheckedChange = { state.setReplacement(it) })
        }
    }
}

@Composable
private fun ToolRow(state: AppState, pickZip: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Button(onClick = { if (state.storage) pickZip() else state.needStorage() }) {
            Icon(Icons.Filled.Add, contentDescription = null, modifier = Modifier.size(ButtonDefaults.IconSize))
            Spacer(Modifier.size(ButtonDefaults.IconSpacing))
            Text("导入 MOD（.zip）")
        }
        Spacer(Modifier.weight(1f))
        OutlinedButton(onClick = { state.refresh() }) {
            Icon(Icons.Filled.Refresh, contentDescription = null, modifier = Modifier.size(ButtonDefaults.IconSize))
            Spacer(Modifier.size(ButtonDefaults.IconSpacing))
            Text("刷新")
        }
    }
}

/** One row per character: its name, and how many mods it has. */
@Composable
private fun CharacterList(state: AppState) {
    val groups = state.groups()
    if (groups.isEmpty()) {
        EmptyNote("还没有 MOD。点「导入 MOD」，在系统文件管理器里选 MOD 的 .zip 压缩包。")
        return
    }
    Column(verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)) {
        groups.forEachIndexed { index, g ->
            SegmentedListItem(
                onClick = { state.open(Screen.Character(g.id)) },
                shapes = ListItemDefaults.segmentedShapes(index, groups.size),
                trailingContent = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("${g.mods.size} 个", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                },
            ) {
                Text(g.title, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
    }
}
