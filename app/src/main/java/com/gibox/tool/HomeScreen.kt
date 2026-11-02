package com.gibox.tool

import androidx.compose.foundation.clickable
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.SegmentedListItem
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

/**
 * Home: top bar (日志 / 设置), the module card (state and version), a card with the replacement switch, import and
 * refresh, and a card of the mods by character (searchable by name or ID).
 */
@Composable
fun HomeScreen(state: AppState, pickZip: () -> Unit) {
    var searching by rememberSaveable { mutableStateOf(false) }
    var query by rememberSaveable { mutableStateOf("") }
    SearchSwitch(searching) { open, field, entered ->
        if (!open) HomeMain(state, pickZip, field) { searching = true }
        else {
            val q = query.trim()
            val found = if (q.isEmpty()) emptyList() else state.groups().filter { it.title.contains(q, ignoreCase = true) || it.id.toString().contains(q) }
            SearchPage(query, { query = it }, "搜索角色名或 ID", onClose = { searching = false; query = "" }, field = field, entered = entered) {
                if (q.isNotEmpty() && found.isEmpty()) item { EmptyNote("没有找到“$q”") }
                itemsIndexed(found, key = { _, g -> g.id }) { index, g ->
                    SegmentedListItem(
                        onClick = { state.open(Screen.Character(g.id)) },
                        shapes = ListItemDefaults.segmentedShapes(index, found.size),
                        trailingContent = {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text("${g.mods.size} 个", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
                                Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        },
                    ) { Text(g.title, maxLines = 1, overflow = TextOverflow.Ellipsis) }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun HomeMain(state: AppState, pickZip: () -> Unit, searchField: Modifier, onSearch: () -> Unit) {
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        topBar = {
            LargeFlexibleTopAppBar(
                colors = pageBarColors(),
                title = { Text("GIBox") },
                subtitle = { Text("原神模型导入器") },
                actions = {
                    IconButton(onClick = { state.open(Screen.Log) }) { Icon(Icons.AutoMirrored.Filled.List, contentDescription = "日志") }
                    IconButton(onClick = { state.open(Screen.Settings) }) { Icon(Icons.Filled.Settings, contentDescription = "设置") }
                },
                scrollBehavior = scroll,
            )
        },
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = padding.calculateTopPadding() + 8.dp, bottom = padding.calculateBottomPadding() + 24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item { ModuleCard(state) }
            item { ManageCard(state, pickZip) }
            item { ModsCard(state, searchField, onSearch) }
        }
    }
}

/** Whether LSPosed loaded the module, and its version. */
@Composable
private fun ModuleCard(state: AppState) {
    val active = state.moduleActive
    val warn = !active
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.extraLarge,
        colors = CardDefaults.cardColors(
            containerColor = if (warn) MaterialTheme.colorScheme.errorContainer else MaterialTheme.colorScheme.primaryContainer,
            contentColor = if (warn) MaterialTheme.colorScheme.onErrorContainer else MaterialTheme.colorScheme.onPrimaryContainer,
        ),
    ) {
        Column(Modifier.padding(horizontal = 24.dp, vertical = 22.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(if (active) Icons.Filled.CheckCircle else Icons.Filled.Warning, contentDescription = null, modifier = Modifier.size(32.dp))
                Spacer(Modifier.width(16.dp))
                Column {
                    Text(if (active) "模块已激活" else "模块未激活", style = MaterialTheme.typography.headlineSmall)
                    Text("模块版本 " + state.appVersion, style = MaterialTheme.typography.bodyMedium)
                }
            }
            if (!active) Text("在 LSPosed 里启用 GIBox，作用域勾选原神（官服或 B 服），然后重新打开 GIBox、彻底重启游戏。", style = MaterialTheme.typography.bodyMedium)
            // Both games ticked in LSPosed: which one the app manages.
            if (state.games.size > 1) Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (pkg in state.games) FilterChip(selected = state.game == pkg, onClick = { state.chooseGame(pkg) }, label = { Text(serverName(pkg)) })
            }
        }
    }
}

/** A white card on the page background, as the home screen's groups. */
@Composable
private fun HomeCard(content: @Composable () -> Unit) {
    Surface(modifier = Modifier.fillMaxWidth(), shape = MaterialTheme.shapes.extraLarge, color = MaterialTheme.colorScheme.surfaceContainerLowest) { content() }
}

/** The replacement master switch (on a tinted row of its own), import and refresh. */
@Composable
private fun ManageCard(state: AppState, pickZip: () -> Unit) {
    HomeCard {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Surface(modifier = Modifier.fillMaxWidth(), shape = MaterialTheme.shapes.large, color = MaterialTheme.colorScheme.surfaceContainerLow) {
                Row(Modifier.padding(horizontal = 20.dp, vertical = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text("全局模型替换", style = MaterialTheme.typography.titleMedium)
                        Text(if (state.enabled) "已开启 · 游戏里替换已启用的 MOD" else "已关闭 · 游戏里显示原模型", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    Switch(checked = state.enabled, onCheckedChange = { state.setReplacement(it) })
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Button(onClick = { if (state.storage) pickZip() else state.needStorage() }, modifier = Modifier.weight(1.6f).heightIn(min = 52.dp)) {
                    Icon(Icons.Filled.Add, contentDescription = null, modifier = Modifier.size(ButtonDefaults.IconSize))
                    Spacer(Modifier.size(ButtonDefaults.IconSpacing))
                    Text("导入 MOD", maxLines = 1, softWrap = false)
                }
                OutlinedButton(onClick = { state.refresh() }, modifier = Modifier.weight(1f).heightIn(min = 52.dp)) {
                    Icon(Icons.Filled.Refresh, contentDescription = null, modifier = Modifier.size(ButtonDefaults.IconSize))
                    Spacer(Modifier.size(ButtonDefaults.IconSpacing))
                    Text("刷新", maxLines = 1, softWrap = false)
                }
            }
        }
    }
}

/**
 * 角色 MOD: one row per character with its number of mods; the mods themselves are on the character's page. The search
 * button opens the search page (characters by name or ID).
 */
@Composable
private fun ModsCard(state: AppState, searchField: Modifier, onSearch: () -> Unit) {
    val groups = state.groups()
    HomeCard {
        Column(Modifier.padding(vertical = 8.dp)) {
            Row(Modifier.padding(start = 24.dp, end = 12.dp, top = 4.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("角色 MOD", style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                Text("${groups.size} 个角色", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                IconButton(onClick = onSearch, modifier = searchField) { Icon(Icons.Filled.Search, contentDescription = "搜索角色") }
            }
            HorizontalDivider(Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.surfaceContainerHigh)
            if (groups.isEmpty()) EmptyNote("还没有 MOD。点「导入 MOD」，在系统文件管理器里选 MOD 的 .zip 压缩包。")
            groups.forEach { g ->
                Row(
                    Modifier.fillMaxWidth().clickable { state.open(Screen.Character(g.id)) }.padding(start = 24.dp, end = 16.dp, top = 16.dp, bottom = 16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(g.title, style = MaterialTheme.typography.titleMedium, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
                    Text("${g.mods.size} 个", style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null)
                }
            }
        }
    }
}
