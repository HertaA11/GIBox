package com.gibox.tool

import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.selection.selectable
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import com.itos.coui.effect.couiButtonFeedback
import com.itos.coui.layout.Divider
import com.itos.coui.layout.Footer
import com.itos.coui.layout.Group
import com.itos.coui.radiobutton.RadioButton
import com.itos.coui.scroll.rememberCouiOverscrollEffect
import com.itos.coui.theme.CouiTheme
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
    val guess = remember(pick) {
        Characters.Guess().apply { recommended.addAll(pick.recommendedIds.mapNotNull(Characters::byId)) }
    }
    // Off the main thread: the first call reads the skeleton table made from the game's files (17 MB).
    val recorded by produceState(initialValue = emptySet<Int>(), pick, state.game) { value = withContext(Dispatchers.IO) { state.recorded() } }
    var query by rememberSaveable(pick) { mutableStateOf("") }
    var chosen by rememberSaveable(pick) { mutableIntStateOf(0) }
    val list = remember(guess, query) { Characters.ordered(guess, query) }
    val selected = Characters.byId(chosen)
    val dir = pick.pending ?: pick.source
    val ini by produceState<Pair<List<Pair<String, String>>, String?>?>(null, dir) {
        value = withContext(Dispatchers.IO) {
            val choices = state.schemes(dir)
            choices.map { it to schemeLabel(it, choices) } to state.scheme(dir).takeIf { it in choices }
        }
    }
    val schemes = ini?.first.orEmpty()
    var scheme by remember(pick, ini) { mutableStateOf(ini?.second) }
    val needScheme = schemes.size > 1 && scheme == null
    val outfits = remember(chosen) { state.outfits(chosen) }
    var outfit by remember(pick) { mutableStateOf("") }
    var outfitHint by remember(pick) { mutableStateOf("") }
    var outfitsRecorded by remember(pick) { mutableStateOf(emptySet<String>()) }
    LaunchedEffect(pick, chosen, state.game) {
        if (outfits.isEmpty()) { outfit = ""; outfitHint = ""; outfitsRecorded = emptySet(); return@LaunchedEffect }
        val (suggested, recordedNow) = withContext(Dispatchers.IO) { state.suggestedOutfit(pick, chosen) to state.recordedOutfits(chosen) }
        outfit = suggested; outfitsRecorded = recordedNow
        outfitHint = withContext(Dispatchers.IO) { state.outfitGuessText(pick, chosen) }
    }
    val chosenOutfit = outfits.firstOrNull { it.prefab == outfit }
    val boneHint by produceState(initialValue = "", chosen, outfit, state.game) {
        value = withContext(Dispatchers.IO) { state.boneImportHint(chosen, outfit) }
    }

    Page("选择分类或角色", state::back, topContent = {
        SearchControl(query, { query = it }, "ID 或名称，如 10000107、茜特菈莉、citlali")
    }, bottom = {
        BottomAction(if (selected == null) "确定" else "确定 · ${selected.name}" +
            (chosenOutfit?.let { " · ${it.name}" } ?: ""),
            enabled = state.libraryReady && selected != null && ini != null && !needScheme && (outfits.isEmpty() || chosenOutfit != null)) {
            selected?.let { state.confirm(pick, it, if (schemes.size > 1) scheme else null, chosenOutfit?.prefab) }
        }
    }) { padding ->
        LazyColumn(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding),
            overscrollEffect = rememberCouiOverscrollEffect()) {
            item {
                Group {
                    ActionRow(pick.modName, when {
                        ini == null -> "正在读取 INI 方案…"
                        needScheme -> "先选择一套 INI 方案"
                        selected == null -> "点一个角色，再点确定"
                        else -> "${selected.id} · $boneHint"
                    }, Glyph.Parcel)
                    if (schemes.size > 1) {
                        Divider()
                        ChoiceRow("INI 方案", scheme, schemes,
                            "有 ${schemes.size} 套方案，之后也可更换", Glyph.Wrench) { scheme = it }
                    }
                }
            }
            item {
                val names = guess.recommended.joinToString("、") { it.name }
                Footer(text = if (names.isEmpty()) "没认出角色，请按 ID 或名称查找" else "根据 MOD 名称和 hash 推荐：$names")
            }
            if (list.isEmpty()) item { Footer(text = "没有匹配的角色") }
            items(list, key = { it.id }) { avatar ->
                val recommended = guess.recommends(avatar.id)
                val checked = chosen == avatar.id
                val interaction = remember { MutableInteractionSource() }
                Group {
                    ActionRow(avatar.name + if (recommended) "（推荐）" else "", when {
                        Characters.isLoading(avatar.id) -> "加载背景图片，无需骨骼"
                        Characters.isScene(avatar.id) -> "NPC、怪物、武器、风之翼" + if (avatar.id == pick.current) " · 当前" else ""
                        else -> "${avatar.id}" + (if (recorded.contains(avatar.id)) " · 已记录骨骼" else "") +
                            (if (avatar.id == pick.current) " · 当前" else "")
                    }, modifier = Modifier.couiButtonFeedback(CouiTheme.isDark, interaction, true, list = true)
                        .selectable(checked, interaction, null, role = Role.RadioButton) { chosen = avatar.id },
                        trailing = { RadioButton(selected = checked) })
                    if (avatar.id == chosen && outfits.isNotEmpty()) {
                        Divider()
                        ChoiceRow("衣装", outfit, outfits.map { row ->
                            row.prefab to (row.name + if (outfitsRecorded.contains(row.prefab)) " · 已记录骨骼" else " · 未记录骨骼")
                        }, outfitHint.ifBlank { "未记录的衣装可在游戏衣装页预览后自动采集骨骼" }) { outfit = it }
                    }
                }
            }
        }
    }
}
