package com.gibox.tool

import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import com.itos.coui.layout.Divider
import com.itos.coui.layout.Footer
import com.itos.coui.layout.Group
import com.itos.coui.scroll.rememberCouiOverscrollEffect
import com.itos.coui.switch.Switch
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext

@Composable
internal fun CharacterScreen(state: AppState, id: Int, active: Boolean) {
    var query by rememberSaveable(id) { mutableStateOf("") }
    val group = state.group(id)
    val title = group?.title ?: if (id == 0) "未选择角色" else Characters.nameOf(id)
    val filter = query.trim()
    val mods = remember(group, filter) {
        group?.mods.orEmpty().filter {
            filter.isEmpty() || state.label(it.name).contains(filter, true) || it.name.contains(filter, true)
        }
    }
    val pkg = state.game
    if (active) LaunchedEffect(pkg, id) {
        while (isActive) {
            state.acceptStatus(pkg, withContext(Dispatchers.IO) { Control.status(pkg) })
            delay(500)
        }
    }
    Page(title, state::back, topContent = {
        if (!group?.mods.isNullOrEmpty()) SearchControl(query, { query = it }, "搜索这个角色的 MOD")
    }) { padding ->
        LazyColumn(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding),
            overscrollEffect = rememberCouiOverscrollEffect()) {
            if (id in 10000000..10001999 && query.isBlank()) item {
                Group {
                    val pending = state.refreshingBones(id)
                    ActionRow(if (pending) "等待采集" else "重新采集骨骼", state.boneRefreshMessage(id), Glyph.Refresh,
                        enabled = !pending && state.busy == null) { state.confirmBoneRefresh(id) }
                    Divider()
                    ActionRow("导出骨骼（可补充内置表）", glyph = Glyph.Share, enabled = state.busy == null) { state.exportCharacterBones(id) }
                }
            }
            if (mods.isEmpty()) item {
                Footer(text = when {
                    !state.libraryReady -> "正在读取模组…"
                    filter.isEmpty() -> "这个角色下没有 MOD 了"
                    else -> "没有找到“$filter”"
                })
            }
            items(mods, key = { it.name }) { mod -> Group { ModRow(state, mod) } }
        }
    }
}

@Composable
internal fun ModRow(state: AppState, mod: Importer.Mod) {
    val on = mod.converted && state.selected(mod.name)
    val summary = buildList {
        if (mod.outfit.isNotEmpty()) add(state.outfitLabel(mod.outfit))
        add(if (!mod.converted) "未转换" else if (on) "已开启" else "未开启")
        if (mod.outdated) add("需重新转换")
    }.joinToString(" · ")
    ActionRow(state.label(mod.name), summary, trailing = {
        Switch(checked = on, onCheckedChange = { state.setSelected(mod, it) }, enabled = mod.converted)
    }) { state.open(Screen.Detail(mod.name)) }
}
