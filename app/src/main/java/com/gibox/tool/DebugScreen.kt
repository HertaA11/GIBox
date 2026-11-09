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
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Share
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
 * 3DMigoto logic core, the animation experiments, bone export and the old manual conversion.
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
                colors = pageBarColors(),
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
                // What the framework told the app (LspService): no service means GIBox was not loaded on the modern API.
                SettingsGroup(title = { SectionTitle("框架") }, verticalArrangement = gap) {
                    SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(0, 1)) {
                        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            Text("LSPosed 服务", style = MaterialTheme.typography.titleSmall)
                            val scope = LspService.scope
                            Text(
                                if (scope == null) "未收到（GIBox 没有按新接口加载）"
                                else "${LspService.framework ?: "?"}\n作用域：" + (scope.ifEmpty { listOf("（空）") }).joinToString("、"),
                                style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
            }
            item {
                // Texture-only mods: strategy and LOD take effect within a second; hash matching at the next game start.
                val rev = state.revision
                val strategy = remember(rev) { state.textureMods("strategy", "auto") }
                val lod = remember(rev) { state.textureMods("lod", "full") }
                val hashMode = remember(rev) { state.hashMode() }
                SettingsGroup(title = { SectionTitle("纯贴图 MOD") }, verticalArrangement = gap) {
                    SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(0, 5)) {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("替换方式", style = MaterialTheme.typography.titleSmall)
                            Text("其他方式只用于对比排查，切换前会先还原。", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                for ((value, label) in listOf("auto" to "自动（默认）", "diagnose" to "只检查，不替换", "direct" to "直接改游戏材质", "clone" to "复制材质再改")) {
                                    FilterChip(selected = strategy == value, onClick = { state.setTextureMods("strategy", value) }, label = { Text(label) })
                                }
                            }
                        }
                    }
                    SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(1, 5)) {
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
                    SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(2, 5)) {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("hash 模式", style = MaterialTheme.typography.titleSmall)
                            Text(when (hashMode) {
                                "match" -> "像 XXMI 平时一样：游戏上传模型时算一次 hash，和 MOD 的规则对比，找刘海、眼睛、NPC 和怪物的部件。开销小，可以一直开着"
                                "collect" -> "在匹配的基础上，把出现过的角色、NPC、怪物的网格都记进本机 hash 表（hash-map.json）和对比报告。在图鉴里点角色、在大世界靠近 NPC 和怪物；采完改回匹配"
                                "full" -> "排查问题用：每次上传算全部 12 种 hash，逐条写进 ib-hashes.txt 并逐个网格对比。很耗性能，查完就关"
                                else -> "不读取游戏的索引上传。只靠 PC 数据库、随 App 附带的 hash 表和本机已采集的表找部件"
                            } + "。重启游戏后生效", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                for ((value, label) in listOf("off" to "关闭", "match" to "匹配", "collect" to "采集", "full" to "完整诊断")) {
                                    FilterChip(selected = hashMode == value, onClick = { state.setHashMode(value) }, label = { Text(label) })
                                }
                            }
                        }
                    }
                    SettingsMenuLink(
                        title = { Text("hash 对比报告") },
                        subtitle = { Text("安卓与 PC 逐个部件对比") },
                        icon = { Icon(Icons.Filled.Share, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(3, 5),
                        onClick = { state.shareHashReport() },
                    )
                    SettingsMenuLink(
                        title = { Text("轮流测试替换方式") },
                        subtitle = { Text("依次试三种方式，每轮观察 4 秒后还原核对") },
                        icon = { Icon(Icons.Filled.Build, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(4, 5),
                        onClick = { state.startTextureTest() },
                    )
                }
            }
            item {
                val rev = state.revision
                val pin = remember(rev) { state.lodPin() }
                SettingsGroup(title = { SectionTitle("大世界 LOD") }, verticalArrangement = gap) {
                    SettingsSwitch(
                        state = pin,
                        title = { Text("被替换的角色保持完整模型") },
                        subtitle = { Text("让游戏对正在替换的角色不切换到低精度模型（LOD0），避免低精度模型骨骼不同导致变形或不生效；关闭后恢复游戏的切换") },
                        shapes = ListItemDefaults.segmentedShapes(0, 1),
                        onCheckedChange = { state.setLodPin(it) },
                    )
                }
            }
            item {
                val rev = state.revision
                val animation = remember(rev) { state.experimentInt("animationMode") }
                val logic = remember(rev) { state.experiment("logicCore") }
                val steps = remember(rev) { state.experiment("keySteps") }
                SettingsGroup(title = { SectionTitle("模型动画实验") }, verticalArrangement = gap) {
                    SegmentedListItem(shapes = ListItemDefaults.segmentedShapes(0, 3)) {
                        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("动画播放方式", style = MaterialTheme.typography.titleSmall)
                            Text("切换后自动重建动画模型，预热完成后开始播放，只影响带动画的 MOD", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
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
                        subtitle = { Text("按 MOD 自带的 INI 规则切换动画、款式和按键；旧版转换的 MOD 需重新转换") },
                        shapes = ListItemDefaults.segmentedShapes(1, 3),
                        onCheckedChange = { state.setExperiment("logicCore", it) },
                    )
                    SettingsSwitch(
                        state = steps,
                        title = { Text("逐个切换按钮") },
                        subtitle = { Text("按键切换里显示上一个、下一个") },
                        shapes = ListItemDefaults.segmentedShapes(2, 3),
                        onCheckedChange = { state.setExperiment("keySteps", it) },
                    )
                }
            }
            item {
                SettingsGroup(title = { SectionTitle("转换和骨骼") }, verticalArrangement = gap) {
                    SettingsMenuLink(
                        title = { Text("一键重新转换") },
                        subtitle = { Text("所有 MOD 按各自角色重新转换") },
                        icon = { Icon(Icons.Filled.Refresh, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(0, 3),
                        onClick = { state.reconvertAll() },
                    )
                    SettingsMenuLink(
                        title = { Text("导出当前角色骨骼") },
                        subtitle = { Text("导出当前角色的骨骼目录，供手动转换用") },
                        icon = { Icon(Icons.Filled.Person, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(1, 3),
                        onClick = { state.exportBones() },
                    )
                    SettingsMenuLink(
                        title = { Text("手动转换（旧流程）") },
                        subtitle = { Text("手动指定骨骼、角色 ID 和部件映射") },
                        icon = { Icon(Icons.Filled.Build, contentDescription = null) },
                        shapes = ListItemDefaults.segmentedShapes(2, 3),
                        onClick = { state.open(Screen.Manual) },
                    )
                }
            }
        }
    }
}
