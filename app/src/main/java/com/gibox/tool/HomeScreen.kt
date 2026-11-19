package com.gibox.tool

import android.os.Build
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.itos.coui.layout.Divider
import com.itos.coui.layout.Footer
import com.itos.coui.layout.Group
import com.itos.coui.scroll.rememberCouiOverscrollEffect
import com.itos.coui.segmentbutton.SegmentedControl
import com.itos.coui.text.CouiTextKind
import com.itos.coui.text.Text
import com.itos.coui.theme.CouiTheme

private val categories = listOf("角色 MOD", "场景 MOD", "加载图片")

@Composable
internal fun ModManagementScreen(state: AppState, pickZip: () -> Unit) {
    var category by rememberSaveable { mutableIntStateOf(0) }
    var searching by rememberSaveable { mutableStateOf(false) }
    var query by rememberSaveable { mutableStateOf("") }
    val listState = rememberLazyListState()
    val searchList = rememberLazyListState()
    val selectCategory: (Int) -> Unit = {
        if (category != it) {
            listState.requestScrollToItem(0)
            category = it
        }
    }
    val groups = state.groups()
    val filter = query.trim()
    val results = remember(groups, searching, filter, category) {
        groups.filter { group ->
            if (searching) filter.isNotEmpty() && (group.title.contains(filter, true) || group.id.toString().contains(filter))
            else category == when {
                Characters.isLoading(group.id) -> 2
                Characters.isScene(group.id) -> 1
                else -> 0
            }
        }
    }
    val flatMods = remember(results) { results.flatMap { it.mods } }
    Page("模组管理", toolbar = {
        SearchToolbar("模组管理", "搜索分类、角色名或 ID", query, { query = it }, searching, {
            searching = it
            if (!it) query = ""
        }, back = state::back)
    }) { padding ->
        LazyColumn(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding).padding(bottom = 8.dp),
            state = if (searching) searchList else listState,
            overscrollEffect = rememberCouiOverscrollEffect(),
        ) {
            if (!searching) {
                item {
                    Group {
                        ToggleRow("全局模型替换", state.enabled,
                            if (state.enabled) "在游戏里替换已启用的 MOD" else "在游戏里显示原模型",
                            enabled = state.libraryReady, change = state::setReplacement)
                        Divider()
                        ActionRow("导入 MOD", enabled = state.libraryReady) { if (state.storage) pickZip() else state.needStorage() }
                        Divider()
                        ActionRow("刷新", click = state::refreshInBackground)
                    }
                }
                item {
                    if (Build.VERSION.SDK_INT >= 34) {
                        SegmentedControl(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
                            labels = categories, selectedIndex = category, onSelected = selectCategory)
                    } else Group {
                        ChoiceRow("MOD 分类", category, categories.mapIndexed { i, label -> i to label }, change = selectCategory)
                    }
                }
            }
            if (!state.libraryReady) item { Footer(text = "正在读取模组…") }
            if (state.libraryReady && results.isEmpty() && (!searching || query.isNotBlank())) item {
                Footer(text = when {
                    searching && query.isNotBlank() -> "没有找到“${query.trim()}”"
                    category == 2 -> "还没有加载图片。导入加载背景 MOD 时，选择「加载图片」。"
                    category == 1 -> "还没有场景 MOD。导入 NPC、怪物、武器或风之翼 MOD 时，选择「场景 MOD」。"
                    else -> "还没有角色 MOD。点「导入 MOD」，选择 MOD 的 .zip 压缩包。"
                })
            }
            if (!searching && category != 0) {
                items(flatMods, key = { it.name }) { mod -> Group { ModRow(state, mod) } }
            } else items(results, key = { it.id }) { group ->
                Group {
                    ActionRow(group.title, showNext = true, trailing = {
                        Text(text = "${group.mods.size} 个", kind = CouiTextKind.ItemValue, color = CouiTheme.colorScheme.onSurfaceVariant)
                    }) { state.open(Screen.Character(group.id)) }
                }
            }
        }
    }
}
