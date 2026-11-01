package com.gibox.tool

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** The shared log (XXMI line format), newest at the bottom. */
@Composable
fun LogScreen(state: AppState) {
    var reload by remember { mutableIntStateOf(0) }
    var levelsOpen by remember { mutableStateOf(false) }
    val lines = remember(reload) { state.readLog().trimEnd().split('\n').filter { it.isNotEmpty() } }
    val list = rememberLazyListState()
    LaunchedEffect(lines) { if (lines.isNotEmpty()) list.scrollToItem(lines.size - 1) }
    Scaffold(
        topBar = {
            TopAppBar(
                colors = pageBarColors(),
                title = { Text("运行日志") },
                subtitle = { Text("${lines.size} 行") },
                navigationIcon = { BackButton { state.back() } },
                actions = {
                    IconButton(onClick = { reload++ }) { Icon(Icons.Filled.Refresh, contentDescription = "刷新") }
                    IconButton(onClick = {
                        state.prompt = Prompt("清空日志", "删除 ${AppLog.FILE}（游戏和 GIBox 共用）？", confirm = "清空", dismiss = "取消", destructive = true, onConfirm = { state.clearLog(); reload++ })
                    }) { Icon(Icons.Filled.Delete, contentDescription = "清空") }
                    IconButton(onClick = { state.shareLog() }) { Icon(Icons.Filled.Share, contentDescription = "分享日志") }
                    Box {
                        IconButton(onClick = { levelsOpen = true }) { Icon(Icons.Filled.MoreVert, contentDescription = "日志等级") }
                        DropdownMenu(expanded = levelsOpen, onDismissRequest = { levelsOpen = false }) {
                            DropdownMenuItem(text = { Text("日志等级") }, onClick = {}, enabled = false)
                            Control.LOG_LEVELS.forEachIndexed { index, level ->
                                DropdownMenuItem(
                                    text = { Text(Control.LOG_LEVEL_LABELS[index]) },
                                    leadingIcon = { RadioButton(selected = state.logLevel == level, onClick = null) },
                                    onClick = { levelsOpen = false; state.changeLogLevel(level) },
                                )
                            }
                        }
                    }
                },
            )
        },
    ) { padding ->
        SelectionContainer {
            LazyColumn(
                state = list,
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(start = 12.dp, end = 12.dp, top = padding.calculateTopPadding() + 4.dp, bottom = padding.calculateBottomPadding() + 16.dp),
            ) {
                if (lines.isEmpty()) item { EmptyNote("日志是空的") }
                items(lines) { line ->
                    Text(
                        line,
                        style = MaterialTheme.typography.bodySmall,
                        fontFamily = FontFamily.Monospace,
                        fontSize = 11.sp,
                        lineHeight = 15.sp,
                        color = when {
                            " ERROR " in line -> MaterialTheme.colorScheme.error
                            " WARNING " in line -> MaterialTheme.colorScheme.tertiary
                            line.startsWith("    ") -> MaterialTheme.colorScheme.onSurfaceVariant
                            else -> MaterialTheme.colorScheme.onSurface
                        },
                    )
                }
            }
        }
    }
}
