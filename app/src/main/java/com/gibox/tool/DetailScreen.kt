package com.gibox.tool

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import com.itos.coui.dialog.CouiDialogDefaults
import com.itos.coui.layout.Divider
import com.itos.coui.layout.Footer
import com.itos.coui.layout.Group
import java.io.File
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext

private data class ModDetails(
    val manifest: ModelManifest?,
    val variants: Variants?,
    val schemes: List<Pair<String, String>>,
    val scheme: String,
    val animationBoneFallback: Boolean,
    val record: File?,
)

@Composable
internal fun DetailScreen(state: AppState, name: String, active: Boolean) {
    val mod = state.mod(name)
    val revision = state.revision
    val pkg = state.game
    val model = mod?.model
    val source = mod?.source
    val details by produceState<ModDetails?>(null, mod, pkg) {
        value = withContext(Dispatchers.IO) {
            val manifest = model?.let { runCatching { ModelManifest.read(it) }.getOrNull() }
            val variants = manifest?.let(Variants::fromManifest)
            val schemes = state.schemes(source)
            ModDetails(
                manifest = manifest,
                variants = variants,
                schemes = schemes.map { it to schemeLabel(it, schemes) },
                scheme = state.scheme(source),
                animationBoneFallback = state.animationBoneFallback(source),
                record = model?.let { File(it, "conversion.log") }?.takeIf { it.isFile },
            )
        }
    }
    val modelDetails = details
    val manifest = modelDetails?.manifest
    val keys = manifest?.keys.orEmpty()
    val modEnabled = mod?.converted == true && state.selected(name)
    val logicCore = remember(revision) { state.experiment("logicCore") }
    val steps = remember(revision) { state.experiment("keySteps") }
    val showKeyPanel = modEnabled && logicCore && keys.isNotEmpty()
    if (active && showKeyPanel) LaunchedEffect(pkg, name) {
        while (isActive) {
            state.acceptStatus(pkg, withContext(Dispatchers.IO) { Control.status(pkg) })
            delay(500)
        }
    }

    PreferencePage(state.label(name), state::back) {
        if (mod == null) {
            Footer(text = if (state.libraryReady) "这个 MOD 已不存在" else "正在读取模组…")
            return@PreferencePage
        }
        Group(title = "状态") {
            ToggleRow("启用", modEnabled,
                if (mod.converted) who(mod) else "还没转换：先选择角色", Glyph.Check,
                enabled = mod.converted) { state.setSelected(mod, it) }
        }
        Group(title = "名称") {
            ActionRow("重命名", state.label(mod.name), Glyph.Edit, showNext = true) { state.renaming = mod.name }
        }
        if (source != null) {
            Group(title = "角色") {
                ActionRow(if (mod.converted) "更换角色并重新转换" else "选择角色并转换",
                    who(mod) + if (!mod.outdated) "" else if (mod.reason.isNotEmpty()) "（${mod.reason}，需要重新转换）" else "（已更改，还没重新转换）",
                    Glyph.Person, showNext = true) { state.chooseCharacter(mod) }
                if (mod.converted && mod.id != 0) {
                    Divider()
                    ActionRow("重新转换", "改了 Mods 里的文件后用；角色不变", Glyph.Refresh) { state.convert(source, mod.id) }
                }
            }
            if (state.logLevel == "debug" && modelDetails != null) {
                Group(title = "动画转换实验（调试）") {
                    ToggleRow("忽略越界骨骼关联", modelDetails.animationBoneFallback,
                        "只作用于此 MOD 的逐帧动画：保留有效权重并重新归一化，需完整骨骼库。保存后重新转换。",
                        Glyph.Wrench, enabled = state.busy == null) { state.setAnimationBoneFallback(mod, it) }
                }
            }
            if (modelDetails != null && modelDetails.schemes.size > 1) Group(title = "INI 方案") {
                ChoiceRow("转换方案", modelDetails.scheme, modelDetails.schemes,
                    if (mod.converted) "换一套会重新转换" else "选一套，再选择角色转换", Glyph.Wrench) { state.chooseScheme(mod, it) }
            }
        }
        val variants = modelDetails?.variants
        if (variants != null) {
            val choices = remember(variants, revision, name) { state.variantChoices(variants, name) }
            val names = variants.values.keys.filterNot(variants::hidden)
            if (names.isNotEmpty() || variants.hasSwitch()) Group(title = "款式") {
                if (variants.hasSwitch()) {
                    ToggleRow("显示替换", variants.replacing(choices)) {
                        state.setReplacing(mod, variants, it)
                    }
                }
                names.forEachIndexed { i, variable ->
                    if (i != 0 || variants.hasSwitch()) Divider()
                    val values = variants.values[variable].orEmpty()
                    val selectedValue = state.pendingVariant(name, variable) ?: choices.getValue(variable)
                    if (Characters.isLoading(mod.id) && variable == "\$skip_ui") {
                        ChoiceRow("隐藏加载提示", selectedValue, values.map { it to if (it == 1) "开" else "关" }) {
                            state.setVariable(mod, variable, it)
                        }
                    } else {
                        NumberedChips(variable.removePrefix("$"), values, selectedValue) {
                            state.setVariable(mod, variable, it)
                        }
                    }
                }
            }
        }
        if (showKeyPanel && manifest != null) KeyPanel(state, mod, manifest, steps)
        Group(title = "其他") {
            val record = modelDetails?.record
            if (record != null) {
                ActionRow("转换记录", "部件、骨骼、款式变量", Glyph.Info, showNext = true) {
                    state.showConversionRecord(record)
                }
                Divider()
            }
            ActionRow(
                "删除", if (source != null) "删除 Mods 里的副本和转换好的模型" else "删除转换好的模型",
                Glyph.Delete, titleColor = CouiDialogDefaults.themeColors().destructive,
            ) {
                state.delete(mod)
            }
        }
        if (source == null) Footer(text = "手动转换的模型没有源文件，只能启用、停用和删除。要换角色请重新导入 MOD。")
    }
}
