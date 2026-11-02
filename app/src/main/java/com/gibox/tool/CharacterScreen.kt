package com.gibox.tool

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.SharedTransitionLayout
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext

/**
 * The mods of one character: a search box (it opens the search page), then each mod to switch on or off here, or tap
 * for its details.
 */
@Composable
fun CharacterScreen(state: AppState, id: Int) {
    var searching by rememberSaveable(id) { mutableStateOf(false) }
    var query by rememberSaveable(id) { mutableStateOf("") }
    SharedTransitionLayout { AnimatedContent(targetState = searching, transitionSpec = { searchFade() }, label = "search") { open ->
        val field = searchBounds(this)
        if (!open) CharacterMain(state, id, field) { searching = true }
        else {
            val q = query.trim()
            val found = if (q.isEmpty()) emptyList() else state.group(id)?.mods.orEmpty().filter { state.label(it.name).contains(q, ignoreCase = true) || it.name.contains(q, ignoreCase = true) }
            SearchPage(query, { query = it }, "搜索这个角色的 MOD", onClose = { searching = false; query = "" }, field = field) {
                if (q.isNotEmpty() && found.isEmpty()) item { EmptyNote("没有找到“$q”") }
                itemsIndexed(found, key = { _, m -> m.name }) { index, m -> ModRow(state, m, index, found.size) }
            }
        }
    } }
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun CharacterMain(state: AppState, id: Int, searchField: Modifier, onSearch: () -> Unit) {
    val group = state.group(id)
    val mods = group?.mods.orEmpty()
    val title = group?.title ?: if (id == 0) "未选择角色" else Characters.nameOf(id)
    val pending = state.refreshingBones(id)
    val pkg = state.game
    LaunchedEffect(pkg, id) {
        while (isActive) {
            val snapshot = withContext(Dispatchers.IO) { Control.status(pkg) }
            state.acceptStatus(pkg, snapshot)
            delay(500)
        }
    }
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        topBar = {
            LargeFlexibleTopAppBar(
                colors = pageBarColors(),
                title = {
                    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        Text(title, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                        if (id in 10000000..10001999) Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            FilledTonalIconButton(
                                onClick = { state.confirmBoneRefresh(id) },
                                shapes = IconButtonDefaults.shapes(),
                                enabled = !pending && state.busy == null,
                                modifier = Modifier.size(56.dp),
                            ) {
                                if (pending) CircularProgressIndicator(modifier = Modifier.size(24.dp), strokeWidth = 2.dp)
                                else Icon(Icons.Filled.Refresh, contentDescription = "重新采集$title 的骨骼")
                            }
                            Text(if (pending) "等待采集" else "重采骨骼", style = MaterialTheme.typography.labelSmall)
                        }
                    }
                },
                subtitle = { Text("${mods.size} 个 MOD" + if (id != 0) " · $id" else "") },
                navigationIcon = { BackButton { state.back() } },
                scrollBehavior = scroll,
            )
        },
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = padding.calculateTopPadding() + 8.dp, bottom = padding.calculateBottomPadding() + 24.dp),
            verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap),
        ) {
            state.boneRefreshMessage(id)?.let { message ->
                item(key = -id.toLong()) { Text(message, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant) }
            }
            if (mods.isNotEmpty()) item(key = Long.MIN_VALUE) { SearchBox("搜索这个角色的 MOD", onSearch, Modifier.padding(bottom = 8.dp).then(searchField)) }
            if (mods.isEmpty()) item { EmptyNote("这个角色下没有 MOD 了") }
            itemsIndexed(mods, key = { _, m -> m.name }) { index, m -> ModRow(state, m, index, mods.size) }
        }
    }
}

/** One mod: its name, its state, the switch; tap for its details. */
@Composable
private fun ModRow(state: AppState, m: Importer.Mod, index: Int, count: Int) {
    val on = m.converted && state.selected(m.name)
    val label = state.label(m.name)
    SegmentedListItem(
        onClick = { state.open(Screen.Detail(m.name)) },
        shapes = ListItemDefaults.segmentedShapes(index, count),
        supportingContent = {
            val parts = mutableListOf(if (!m.converted) "未转换" else if (on) "已开启" else "未开启")
            if (m.outdated) parts.add("需重新转换")
            Text(
                parts.joinToString(" · "),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                color = if (m.outdated) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
            )
        },
        trailingContent = {
            Switch(checked = on, onCheckedChange = { state.setSelected(m, it) }, enabled = m.converted)
        },
    ) {
        Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}
