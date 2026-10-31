package com.gibox.tool

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

/** The mods of one character: a search field, then each mod to switch on or off here, or tap for its details. */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun CharacterScreen(state: AppState, id: Int) {
    val group = state.group(id)
    val all = group?.mods.orEmpty()
    var query by rememberSaveable(id) { mutableStateOf("") }
    val q = query.trim()
    val mods = if (q.isEmpty()) all else all.filter { state.label(it.name).contains(q, ignoreCase = true) || it.name.contains(q, ignoreCase = true) }
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
                subtitle = { Text("${all.size} 个 MOD" + if (id != 0) " · $id" else "") },
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
            if (all.isNotEmpty()) item(key = Long.MIN_VALUE) { SearchField(query, { query = it }, "搜索这个角色的 MOD", Modifier.padding(bottom = 8.dp)) }
            if (all.isEmpty()) item { EmptyNote("这个角色下没有 MOD 了") }
            else if (mods.isEmpty()) item { EmptyNote("没有找到“$q”") }
            itemsIndexed(mods, key = { _, m -> m.name }) { index, m ->
                val on = m.converted && state.selected(m.name)
                val label = state.label(m.name)
                SegmentedListItem(
                    onClick = { state.open(Screen.Detail(m.name)) },
                    shapes = ListItemDefaults.segmentedShapes(index, mods.size),
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
        }
    }
}
