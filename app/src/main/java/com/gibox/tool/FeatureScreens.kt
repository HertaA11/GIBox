package com.gibox.tool

import androidx.compose.foundation.Image
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.BasicText
import androidx.compose.foundation.relocation.BringIntoViewRequester
import androidx.compose.foundation.relocation.bringIntoViewRequester
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itos.coui.effect.couiButtonFeedback
import com.itos.coui.text.CouiTextKind
import com.itos.coui.text.rememberCouiTextStyle
import com.itos.coui.layout.CouiPageDefaults
import com.itos.coui.layout.Divider
import com.itos.coui.layout.Footer
import com.itos.coui.layout.Group
import com.itos.coui.theme.CouiTheme

@Composable
internal fun ModuleActivation(state: AppState) {
    val scope = LspService.scope
    val active = state.moduleActive
    val colors = CouiTheme.colorScheme
    val framework = LspService.framework ?: when {
        scope != null -> "LSPosed 服务已连接"
        state.frameworkApi > 0 -> "LSPosed · 框架 API ${state.frameworkApi}"
        else -> "LSPosed 服务未连接"
    }
    val title = if (active) "模块已激活" else "模块未激活"
    val scopeSummary = when {
        scope == null -> "作用域暂未获取"
        scope.isEmpty() -> "0 个作用域"
        else -> {
            val clients = scope.filter { it in AppState.SUPPORTED_GAMES }.joinToString("、", transform = ::serverName)
            "${scope.size} 个作用域" + if (clients.isEmpty()) "" else " · $clients"
        }
    }
    val page = CouiPageDefaults.themeColors()
    val dark = CouiTheme.isDark
    val surface = when {
        !active -> page.card
        dark -> Color(0xFF1A3825)
        else -> Color(0xFFDFFAE4)
    }
    val ink = when {
        !active -> colors.onSurface
        dark -> Color(0xFFC5EED1)
        else -> Color(0xFF164A29)
    }
    val feedback = remember { MutableInteractionSource() }
    val caption = rememberCouiTextStyle(CouiTextKind.ItemSummary).copy(fontSize = 15.sp, color = ink)
    Group(colors = page.copy(card = surface)) {
        Box(Modifier.fillMaxWidth().heightIn(min = 144.dp)
            .couiButtonFeedback(dark, feedback, list = true)
            .clickable(feedback, null, role = Role.Button) {
                val details = buildList {
                    add("模块版本 ${state.appVersion} · $framework")
                    add("作用域 · " + (scope?.joinToString("、")?.ifEmpty { "未选择应用" } ?: "暂未获取"))
                    if (!active) add("在 LSPosed 中启用 GIBox，勾选所用的原神客户端，然后重启游戏。")
                }
                state.prompt = Prompt(title, details.joinToString("\n\n"))
            }) {
            if (active) {
                val watermark = ink.copy(alpha = .16f)
                Image(painterResource(R.drawable.gibox_status_check), null,
                    Modifier.align(Alignment.BottomEnd).padding(end = 12.dp, bottom = 8.dp)
                        .size(84.dp).border(4.dp, watermark, CircleShape).padding(17.dp),
                    colorFilter = ColorFilter.tint(watermark))
            }
            Column(Modifier.fillMaxWidth().padding(20.dp)) {
                BasicText(title, style = rememberCouiTextStyle(CouiTextKind.ItemTitle)
                    .copy(color = ink, fontSize = 22.sp, fontWeight = FontWeight.SemiBold))
                BasicText("模块版本 ${state.appVersion}", style = caption)
                Spacer(Modifier.height(12.dp))
                BasicText(framework, style = caption)
                BasicText(scopeSummary, style = caption)
            }
        }
    }
}

@Composable
internal fun FeatureSearchScreen(state: AppState, open: (Feature) -> Unit) {
    var query by rememberSaveable { mutableStateOf("") }
    Page("搜索功能", state::back, topContent = {
        SearchControl(query, { query = it }, "搜索功能", initiallyExpanded = true, exit = state::back)
    }) { padding -> ColumnPage(padding) { FeatureResults(state, query, open) } }
}

@Composable
internal fun FeatureResults(state: AppState, query: String, open: (Feature) -> Unit) {
    val results = remember(query) {
        val terms = query.trim().split(Regex("\\s+")).filter(String::isNotEmpty)
        if (terms.isEmpty()) emptyList() else features.filter { feature ->
            terms.all { (feature.title + " " + feature.words).contains(it, ignoreCase = true) }
        }
    }
    ClientRow(state)
    if (results.isEmpty()) Footer(text = if (query.isBlank()) "搜索模组管理、辅助功能、设置或关于" else "没有找到相关功能")
    else Group {
        results.forEachIndexed { index, feature ->
            if (index > 0) Divider()
            ActionRow(feature.title, glyph = feature.icon, showNext = true) { open(feature) }
        }
    }
}

@Composable
private fun focusRow(title: String, target: String?): Modifier {
    if (title != target) return Modifier
    val requester = remember { BringIntoViewRequester() }
    LaunchedEffect(requester) { withFrameNanos { }; requester.bringIntoView() }
    return Modifier.bringIntoViewRequester(requester)
}

@Composable
internal fun AccessibilityScreen(state: AppState, target: String?) {
    PreferencePage("辅助功能", state::back) {
        ClientRow(state, showIcon = false)
        val revision = state.revision
        val values = remember(state.game, revision) {
            listOf(state.hideUid(), state.npcModels(), state.synthesisButton(), state.cameraFade() == "disabled")
        }
        val fit = remember(state.game, revision) { state.loadingFit() }
        Group {
            ToggleRow("隐藏用户UID", values[0], modifier = focusRow("隐藏用户UID", target), change = state::setHideUid)
            Divider()
            ToggleRow("角色 MOD 同时替换对应 NPC", values[1], modifier = focusRow("角色 MOD 同时替换对应 NPC", target), change = state::setNpcModels)
            Divider()
            ToggleRow("随身合成台", values[2], "在游戏右上角显示合成台图标", modifier = focusRow("随身合成台", target), change = state::setSynthesisButton)
            Divider()
            ToggleRow("取消角色渐隐", values[3], modifier = focusRow("取消角色渐隐", target)) { state.setCameraFade(if (it) "disabled" else "original") }
            Divider()
            ChoiceRow("加载图片比例", fit, Control.LOADING_FITS.zip(Control.LOADING_FIT_LABELS),
                modifier = focusRow("加载图片比例", target), change = state::setLoadingFit)
        }
    }
}

@Composable
internal fun SettingsScreen(state: AppState, target: String?) {
    PreferencePage("设置", state::back) {
        ClientRow(state, showIcon = false)
        // One flat list: no secondary named categories or nested Settings menus.
        val values = remember(state.game, state.revision) {
            listOf(state.textureMods("strategy", "auto"), state.textureMods("lod", "full"), state.hashMode(), state.experimentInt("animationMode").toString())
        }
        val toggles = remember(state.game, state.revision) { listOf(state.lodPin(), state.experiment("logicCore"), state.experiment("keySteps")) }
        Group {
            ActionRow("打开 Mods 文件夹", showNext = true,
                modifier = focusRow("打开 Mods 文件夹", target)) { state.openFolder(state.modsRoot) }
            Divider()
            ActionRow("打开模型文件夹", showNext = true,
                modifier = focusRow("打开模型文件夹", target)) { state.openFolder(state.modelsRoot) }
            Divider()
            ActionRow("打开骨骼库文件夹", showNext = true,
                modifier = focusRow("打开骨骼库文件夹", target)) { state.openFolder(state.boneRoot) }
            Divider()
            ChoiceRow("替换方式", values[0], listOf("auto" to "自动（默认）", "diagnose" to "只检查，不替换",
                "direct" to "直接改游戏材质", "clone" to "复制材质再改"),
                "其他方式用于对比排查，切换前会先还原", modifier = focusRow("替换方式", target)) { state.setTextureMods("strategy", it) }
            Divider()
            ChoiceRow("远处的简化模型", values[1], listOf("full" to "只换完整模型", "migrate" to "简化模型也换"),
                "只替换核对一致的简化模型", modifier = focusRow("远处的简化模型", target)) { state.setTextureMods("lod", it) }
            Divider()
            ChoiceRow("hash 模式", values[2], listOf("off" to "关闭", "match" to "匹配", "collect" to "采集", "full" to "完整诊断"),
                when (values[2]) {
                    "match" -> "模型上传时计算 hash，匹配角色、NPC 和怪物部件；可常开"
                    "collect" -> "匹配并记录出现过的网格；采集完成后改回匹配"
                    "full" -> "计算全部 hash 并记录诊断报告，开销较大"
                    else -> "仅使用内置和本机已采集的 hash 表"
                } + "。重启游戏后生效", modifier = focusRow("hash 模式", target), change = state::setHashMode)
            Divider()
            ActionRow("hash 对比报告", "安卓与 PC 逐个部件对比", modifier = focusRow("hash 对比报告", target), click = state::shareHashReport)
            Divider()
            ActionRow("轮流测试替换方式", "每轮观察 4 秒后还原核对", modifier = focusRow("轮流测试替换方式", target), click = state::startTextureTest)
            Divider()
            ToggleRow("被替换的角色保持完整模型", toggles[0], "保留 LOD0，关闭后恢复游戏切换",
                modifier = focusRow("被替换的角色保持完整模型", target), change = state::setLodPin)
            Divider()
            ChoiceRow("动画播放方式", values[3], listOf("0" to "缓存帧网格", "1" to "动态网格", "2" to "暂停（初始帧）"),
                "切换后自动重建，只影响带动画的 MOD", modifier = focusRow("动画播放方式", target)) { state.setExperiment("animationMode", it.toInt()) }
            Divider()
            ToggleRow("使用 3DMigoto 进行转换", toggles[1], "按 INI 规则切换动画、款式和按键；旧版 MOD 需重新转换",
                modifier = focusRow("使用 3DMigoto 进行转换", target)) { state.setExperiment("logicCore", it) }
            Divider()
            ToggleRow("逐个切换按钮", toggles[2], "按键切换里显示上一个、下一个",
                modifier = focusRow("逐个切换按钮", target)) { state.setExperiment("keySteps", it) }
            Divider()
            ActionRow("一键重新转换", "所有 MOD 按各自角色重新转换",
                modifier = focusRow("一键重新转换", target), click = state::reconvertAll)
            Divider()
            ActionRow("导出当前角色骨骼", "供手动转换使用",
                modifier = focusRow("导出当前角色骨骼", target), click = state::exportBones)
            Divider()
            ActionRow("手动转换（旧流程）", "指定骨骼、角色 ID 和部件映射", showNext = true) { state.open(Screen.Manual) }
        }
    }
}

@Composable
internal fun AboutScreen(state: AppState) {
    PreferencePage("关于 GIBox", state::back) {
        AboutWordmark()
        Group {
            ActionRow("开发者", "Yuanqi314")
            Divider()
            ActionRow("开源许可证", "GNU General Public License v3.0")
        }
        Group {
            ActionRow("查看源代码", click = state::viewSource)
        }
    }
}
