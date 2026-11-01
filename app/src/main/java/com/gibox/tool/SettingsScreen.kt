package com.gibox.tool

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.filled.Build
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.Icon
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.unit.dp
import com.alorma.compose.settings.ui.expressive.SettingsGroup
import com.alorma.compose.settings.ui.expressive.SettingsMenuLink


/** Settings: file directories; the debug page at the bottom. (The game is picked by LSPosed injection; file access is asked for when needed.) */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun SettingsScreen(state: AppState) {
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    val gap = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)
    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        topBar = {
            LargeFlexibleTopAppBar(
                colors = pageBarColors(),
                title = { Text("设置") },
                navigationIcon = { BackButton { state.back() } },
                scrollBehavior = scroll,
            )
        },
    ) { padding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = padding.calculateTopPadding(), bottom = padding.calculateBottomPadding() + 24.dp),
        ) {
            item {
                val paths = listOf(
                    "点击打开 Mods 文件夹" to state.modsRoot,
                    "点击打开模型文件夹" to state.modelsRoot,
                    "点击打开骨骼库文件夹" to state.boneRoot,
                    "分享日志文件" to state.logFile,
                )
                SettingsGroup(title = { SectionTitle("文件目录") }, verticalArrangement = gap) {
                    paths.forEachIndexed { index, (label, value) ->
                        SettingsMenuLink(
                            title = { Text(label) },
                            subtitle = { Text(value.path) },
                            icon = { Icon(if (index == paths.size - 1) Icons.Filled.Share else Icons.AutoMirrored.Filled.List, contentDescription = null) },
                            shapes = ListItemDefaults.segmentedShapes(index, paths.size),
                            onClick = { if (index == paths.size - 1) state.shareLog() else state.openFolder(value) },
                        )
                    }
                }
            }
            item {
                SettingsGroup(title = { SectionTitle("调试") }, verticalArrangement = gap) {
                    SettingsMenuLink(
                        title = { Text("调试") },
                        subtitle = { Text("纯贴图 MOD、3DMigoto 转换、实验功能、骨骼导出、旧流程") },
                        icon = { Icon(Icons.Filled.Build, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(0, 1),
                        onClick = { state.open(Screen.Debug) },
                    )
                }
            }
        }
    }
}
