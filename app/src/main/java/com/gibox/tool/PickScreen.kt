package com.gibox.tool

import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** A scheme's file name without .ini; the folder too when two schemes share a name. */
fun schemeLabel(rel: String, all: List<String>): String {
    val name = rel.substringAfterLast('/').replace(Regex("(?i)\\.ini$"), "")
    return if (all.count { it.substringAfterLast('/').equals(rel.substringAfterLast('/'), true) } > 1) rel.replace(Regex("(?i)\\.ini$"), "") else name
}

/**
 * Full page for choosing the mod's character: search by ID or name, the guessed character first marked （推荐）, one tap
 * selects, and only 确定 starts the import (or the re-conversion). Leaving the page imports nothing. A character with
 * skins lists its outfits under it (原皮 and each skin): the mod is converted for the one chosen, the one its INI hashes
 * name chosen first. Its mods stay in that character's list either way.
 */
@Composable
fun PickScreen(state: AppState, pick: Screen.Pick) {
    val guess = remember(pick) { state.guess(pick) }
    val recorded = remember(pick) { state.recorded() }
    var query by remember(pick) { mutableStateOf("") }
    var chosen by remember(pick) { mutableIntStateOf(0) }
    val list = remember(guess, query) { Characters.ordered(guess, query) }
    val selected = Characters.byId(chosen)
    val dir = pick.pending ?: pick.source
    val schemes = remember(pick) { state.schemes(dir) }
    var scheme by remember(pick) { mutableStateOf(state.scheme(dir).takeIf { it in schemes }) }
    val needScheme = schemes.size > 1 && scheme == null
    val outfits = remember(chosen) { state.outfits(chosen) }
    var outfit by remember(pick) { mutableStateOf("") }
    var outfitHint by remember(pick) { mutableStateOf("") }
    var outfitsRecorded by remember(pick) { mutableStateOf(emptySet<String>()) }
    LaunchedEffect(pick, chosen) {
        if (outfits.isEmpty()) { outfit = ""; outfitHint = ""; outfitsRecorded = emptySet(); return@LaunchedEffect }
        val (suggested, recordedNow) = withContext(Dispatchers.IO) { state.suggestedOutfit(pick, chosen) to state.recordedOutfits(chosen) }
        outfit = suggested; outfitsRecorded = recordedNow
        outfitHint = withContext(Dispatchers.IO) { state.outfitGuessText(pick, chosen) }
    }
    val chosenOutfit = outfits.firstOrNull { it.prefab == outfit }
    Scaffold(
        topBar = {
            TopAppBar(
                colors = pageBarColors(),
                title = { Text("选择角色") },
                subtitle = { Text(pick.modName) },
                navigationIcon = { BackButton { state.back() } },
            )
        },
        bottomBar = {
            Surface(color = MaterialTheme.colorScheme.surfaceContainer) {
                Row(
                    modifier = Modifier.fillMaxWidth().navigationBarsPadding().imePadding().padding(horizontal = 20.dp, vertical = 12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(Modifier.weight(1f)) {
                        Text(if (selected == null) "还没有选择" else "已选：${selected.name}" + (chosenOutfit?.let { " · ${it.name}" } ?: ""), style = MaterialTheme.typography.titleMedium)
                        Text(
                            if (needScheme) "先在上面选一套 INI 方案" else if (selected == null) "点一个角色，再点确定" else "${selected.id}" + if (recorded.contains(selected.id)) " · 已记录骨骼" else "",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    val height = ButtonDefaults.MediumContainerHeight
                    Button(
                        onClick = { selected?.let { state.confirm(pick, it, if (schemes.size > 1) scheme else null, chosenOutfit?.prefab) } },
                        enabled = selected != null && !needScheme && (outfits.isEmpty() || chosenOutfit != null),
                        shapes = ButtonDefaults.shapesFor(height),
                        modifier = Modifier.heightIn(min = height),
                        contentPadding = ButtonDefaults.contentPaddingFor(height),
                    ) {
                        Icon(Icons.Filled.Check, contentDescription = null, modifier = Modifier.size(ButtonDefaults.iconSizeFor(height)))
                        Spacer(Modifier.size(ButtonDefaults.iconSpacingFor(height)))
                        Text("确定", style = ButtonDefaults.textStyleFor(height))
                    }
                }
            }
        },
    ) { padding ->
        Column(Modifier.fillMaxSize().padding(top = padding.calculateTopPadding())) {
            if (schemes.size > 1) {
                Text(
                    "有 ${schemes.size} 套 INI 方案，选一套转换（之后可更换）",
                    modifier = Modifier.padding(start = 20.dp, end = 20.dp, top = 8.dp),
                    style = MaterialTheme.typography.bodyMedium,
                    color = if (scheme == null) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                )
                // Many schemes must not push the character list off the screen: the chips scroll in a bounded area.
                FlowRow(
                    modifier = Modifier.fillMaxWidth().heightIn(max = 168.dp).verticalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 4.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    for (s in schemes) FilterChip(selected = s == scheme, onClick = { scheme = s }, label = { Text(schemeLabel(s, schemes)) })
                }
            }
            OutlinedTextField(
                value = query,
                onValueChange = { query = it },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
                singleLine = true,
                leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
                trailingIcon = if (query.isEmpty()) null else { { IconButton(onClick = { query = "" }) { Icon(Icons.Filled.Clear, contentDescription = "清空") } } },
                placeholder = { Text("ID 或名称，如 10000107、茜特菈莉、citlali") },
                shape = MaterialTheme.shapes.extraLarge,
            )
            val names = guess.recommended.joinToString("、") { it.name }
            Text(
                if (guess.recommended.isEmpty()) "没认出角色，请按 ID 或名称查找" else "根据 MOD 名称和 hash 推荐：$names",
                modifier = Modifier.padding(horizontal = 20.dp, vertical = 4.dp),
                style = MaterialTheme.typography.bodyMedium,
                color = if (guess.recommended.isEmpty()) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.primary,
            )
            LazyColumn(
                modifier = Modifier.weight(1f),
                contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 8.dp, bottom = padding.calculateBottomPadding() + 16.dp),
                verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap),
            ) {
                if (list.isEmpty()) item { EmptyNote("没有匹配的角色") }
                itemsIndexed(list, key = { _, a -> a.id }) { index, a ->
                    val recommended = guess.recommends(a.id)
                    Column(verticalArrangement = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)) {
                    SegmentedListItem(
                        selected = a.id == chosen,
                        onClick = { chosen = a.id },
                        shapes = ListItemDefaults.segmentedShapes(index, list.size),
                        leadingContent = { RadioButton(selected = a.id == chosen, onClick = null) },
                        supportingContent = {
                            Text(
                                if (Characters.isScene(a.id)) "只支持纯贴图 MOD：按 hash 在场景里找 NPC、怪物的网格" + (if (a.id == pick.current) " · 当前" else "")
                                else "${a.id}" + (if (recorded.contains(a.id)) " · 已记录骨骼" else "") + (if (a.id == pick.current) " · 当前" else ""),
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        },
                        trailingContent = if (recommended) { { Icon(Icons.Filled.Star, contentDescription = "推荐", tint = MaterialTheme.colorScheme.primary) } } else null,
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(a.name, fontWeight = if (recommended) FontWeight.Bold else null)
                            if (recommended) {
                                Spacer(Modifier.width(4.dp))
                                Text("（推荐）", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                            }
                        }
                    }
                    // The outfits of the chosen character: the mod is converted for one of them (its bones are that outfit's).
                    if (a.id == chosen && outfits.isNotEmpty()) {
                        if (outfitHint.isNotEmpty()) Text(outfitHint, modifier = Modifier.padding(start = 24.dp, top = 4.dp), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.primary)
                        outfits.forEachIndexed { i, o ->
                            SegmentedListItem(
                                selected = o.prefab == outfit,
                                onClick = { outfit = o.prefab },
                                shapes = ListItemDefaults.segmentedShapes(i, outfits.size),
                                modifier = Modifier.padding(start = 24.dp),
                                leadingContent = { RadioButton(selected = o.prefab == outfit, onClick = null) },
                                supportingContent = {
                                    Text(
                                        (if (o.original()) "" else "${o.en} · ") + if (outfitsRecorded.contains(o.prefab)) "已记录骨骼" else "未记录骨骼：在大世界穿上，或在衣装页预览后自动采集",
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    )
                                },
                            ) { Text(o.name) }
                        }
                    }
                    }
                }
            }
        }
    }
}
