package com.gibox.tool

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.rememberVectorPainter
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import com.itos.coui.dialog.CouiDialogDefaults
import com.itos.coui.icons.CouiIcons
import com.itos.coui.layout.Footer
import com.itos.coui.menu.couiMenuAnchor
import com.itos.coui.menu.rememberCouiMenuAnchor
import com.itos.coui.scroll.rememberCouiOverscrollEffect
import com.itos.coui.text.CouiTextKind
import com.itos.coui.text.Text
import com.itos.coui.theme.CouiTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private val logLevels = Control.LOG_LEVELS.zip(Control.LOG_LEVEL_LABELS)

@Composable
internal fun LogScreen(state: AppState) {
    val file = state.logFile
    var reload by remember { mutableIntStateOf(0) }
    var levelsExpanded by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    val lines by produceState<List<String>?>(null, file, reload) {
        value = withContext(Dispatchers.IO) {
            AppLog.tail(file, 128 * 1024).lineSequence().filter(String::isNotBlank).toList()
        }
    }
    val list = rememberLazyListState()
    LaunchedEffect(lines) { lines?.lastIndex?.takeIf { it >= 0 }?.let { list.scrollToItem(it) } }

    Page("运行日志", state::back, actions = {
        LogToolbarAction(painterResource(R.drawable.gibox_log_refresh), "刷新") { reload++ }
        LogToolbarAction(painterResource(R.drawable.gibox_log_delete), "删除", color = CouiDialogDefaults.themeColors().destructive) {
            state.prompt = Prompt("清空日志", "删除 ${AppLog.FILE}（游戏和 GIBox 共用）？",
                confirm = "清空", dismiss = "取消", destructive = true, onConfirm = {
                    scope.launch { if (state.clearLog(file)) reload++ }
                })
        }
        LogToolbarAction(painterResource(R.drawable.gibox_log_share), "分享", click = state::shareLog)
        val anchor = rememberCouiMenuAnchor()
        Box(Modifier.couiMenuAnchor(anchor)) {
            LogToolbarAction(rememberVectorPainter(CouiIcons.Outlined.SliderCustom), "级别") {
                anchor.open { levelsExpanded = true }
            }
            ChoiceMenu(levelsExpanded, { levelsExpanded = false }, state.logLevel, logLevels, anchor) { level ->
                levelsExpanded = false
                if (level != state.logLevel) state.changeLogLevel(level)
            }
        }
    }, topContent = { Footer(text = "游戏版本：${serverName(state.game)}") }) { padding ->
        SelectionContainer {
            LazyColumn(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding), state = list,
                overscrollEffect = rememberCouiOverscrollEffect()) {
                if (lines?.isEmpty() == true) item { Footer(text = "日志是空的") }
                items(lines.orEmpty()) { line ->
                    Text(text = line, kind = CouiTextKind.ItemSummary, modifier = Modifier.padding(horizontal = 16.dp, vertical = 2.dp),
                        color = if (" WARNING " in line || " ERROR " in line) CouiTheme.colorScheme.accentOnSurface
                            else CouiTheme.colorScheme.onSurface)
                }
            }
        }
    }
}

@Composable
private fun LogToolbarAction(
    icon: Painter, description: String, color: Color = CouiTheme.colorScheme.onSurface, click: () -> Unit,
) {
    ToolbarButton(click) {
        Image(icon, contentDescription = description, modifier = Modifier.size(24.dp), colorFilter = ColorFilter.tint(color))
    }
}
