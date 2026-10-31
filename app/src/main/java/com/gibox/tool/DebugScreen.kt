package com.gibox.tool

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Build
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedListItem
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.unit.dp
import com.alorma.compose.settings.ui.expressive.SettingsGroup
import com.alorma.compose.settings.ui.expressive.SettingsMenuLink
import com.alorma.compose.settings.ui.expressive.SettingsSwitch

/**
 * Debug and test tools, out of the way of the settings: texture-only mods (strategy, LOD, hash matching, test rounds), the
 * 3DMigoto logic core, the material and animation experiments, bone export and the old manual conversion.
 */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun DebugScreen(state: AppState) {
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    val gap = Arrangement.spacedBy(ListItemDefaults.SegmentedGap)
    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text("调试") },
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
                // Texture-only mods: strategy and LOD take effect within a second; hash matching at the next game start.
                val rev = state.revision
                val strategy = remember(rev) { state.textureMods("strategy", "auto") }
                val lod = remember(rev) { state.textureMods("lod", "full") }
                val ibHash = remember(rev) { state.textureModsFlag("ibHash") }
                SettingsGroup(title = { SectionTitle("纯贴图 MOD") }, verticalArrangement = gap) {
                    SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(0, 4)) {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("替换方式", style = MaterialTheme.typography.titleSmall)
                            Text("默认用自动。其他方式用来对比和排查问题，切换时会先还原。", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                for ((value, label) in listOf("auto" to "自动（默认）", "diagnose" to "只检查，不替换", "direct" to "直接改游戏材质", "clone" to "复制材质再改")) {
                                    FilterChip(selected = strategy == value, onClick = { state.setTextureMods("strategy", value) }, label = { Text(label) })
                                }
                            }
                        }
                    }
                    SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(1, 4)) {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("远处的简化模型", style = MaterialTheme.typography.titleSmall)
                            Text("大世界里角色常用简化模型。默认只换完整模型；也可以换到核对一致的简化模型上。", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                for ((value, label) in listOf("full" to "只换完整模型", "migrate" to "简化模型也换")) {
                                    FilterChip(selected = lod == value, onClick = { state.setTextureMods("lod", value) }, label = { Text(label) })
                                }
                            }
                        }
                    }
                    SettingsSwitch(
                        state = ibHash,
                        title = { Text("用 hash 找部件") },
                        subtitle = { Text("重启游戏后生效。像 PC 一样按 hash 找到刘海、眼睛等部件；找到后会记住，之后可以关掉") },
                        shapes = ListItemDefaults.segmentedShapes(2, 4),
                        onCheckedChange = { state.setTextureModsFlag("ibHash", it) },
                    )
                    SettingsMenuLink(
                        title = { Text("轮流测试替换方式") },
                        subtitle = { Text("角色在场时依次试自动、直接改、复制，每轮换上、观察 4 秒、还原并核对；结果看日志 texture-test") },
                        icon = { Icon(Icons.Filled.Build, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(3, 4),
                        onClick = { state.startTextureTest() },
                    )
                }
            }
            item {
                // The trace takes effect at once (with the log level 调试); game materials with the next switch-in.
                val rev = state.revision
                val games = remember(rev) { state.experiment("gameMaterials") }
                val trace = remember(rev) { state.experiment("glowTrace") }
                SettingsGroup(title = { SectionTitle("材质实验") }, verticalArrangement = gap) {
                    SettingsSwitch(
                        state = trace,
                        title = { Text("追踪游戏对材质的写入") },
                        subtitle = { Text("不改画面。切人和装上模型时，把谁改了哪个参数、改成什么写进日志（日志级别为“调试”时记录）") },
                        shapes = ListItemDefaults.segmentedShapes(0, 2),
                        onCheckedChange = { state.setExperiment("glowTrace", it) },
                    )
                    SettingsSwitch(
                        state = games,
                        title = { Text("只换网格，材质用游戏自己的") },
                        subtitle = { Text("贴图会不对；看还发不发光") },
                        shapes = ListItemDefaults.segmentedShapes(1, 2),
                        onCheckedChange = { state.setExperiment("gameMaterials", it) },
                    )
                }
            }
            item {
                val rev = state.revision
                val animation = remember(rev) { state.experimentInt("animationMode") }
                val logic = remember(rev) { state.experiment("logicCore") }
                SettingsGroup(title = { SectionTitle("模型动画实验") }, verticalArrangement = gap) {
                    SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(0, 2)) {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("动画播放方式", style = MaterialTheme.typography.titleSmall)
                            Text("切换后自动重建动画模型。预热完成后开始播放；只影响带动画的 MOD。调试日志记录帧号、耗时和内存估算。", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                for ((value, label) in listOf(0 to "缓存帧网格", 1 to "动态网格", 2 to "暂停（初始帧）")) {
                                    FilterChip(selected = animation == value, onClick = { state.setExperiment("animationMode", value) }, label = { Text(label) })
                                }
                            }
                        }
                    }
                    SettingsSwitch(
                        state = logic,
                        title = { Text("使用 3DMigoto 进行转换") },
                        subtitle = { Text("按 MOD 自带的 INI 规则切换动画和款式，支持 MOD 的按键。旧版本转换的 MOD 要重新转换") },
                        shapes = ListItemDefaults.segmentedShapes(1, 2),
                        onCheckedChange = { state.setExperiment("logicCore", it) },
                    )
                }
            }
            item {
                SettingsGroup(title = { SectionTitle("骨骼和旧流程") }, verticalArrangement = gap) {
                    SettingsMenuLink(
                        title = { Text("导出当前角色骨骼") },
                        subtitle = { Text("游戏里有角色在场时，导出一份带时间的骨骼目录，供手动转换用") },
                        icon = { Icon(Icons.Filled.Person, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(0, 2),
                        onClick = { state.exportBones() },
                    )
                    SettingsMenuLink(
                        title = { Text("手动转换（旧流程）") },
                        subtitle = { Text("直接指定骨骼文件夹、角色 ID 和部件映射，不经过 Mods 列表") },
                        icon = { Icon(Icons.Filled.Build, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(1, 2),
                        onClick = { state.open(Screen.Manual) },
                    )
                }
            }
        }
    }
}
