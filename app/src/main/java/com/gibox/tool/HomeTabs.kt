package com.gibox.tool

import android.os.Build
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import com.itos.coui.effect.CouiFloatingRenderer
import com.itos.coui.effect.couiNavigationFeedback
import com.itos.coui.icons.CouiIcons
import com.itos.coui.layout.Scaffold
import com.itos.coui.navigation.CouiNavigationItem
import com.itos.coui.navigation.NavigationBar
import com.itos.coui.shape.CouiCapsuleShape
import com.itos.coui.text.CouiTextKind
import com.itos.coui.text.Text
import com.itos.coui.theme.CouiTheme

internal val LocalHomeTabInsets = compositionLocalOf<PaddingValues?> { null }

private val destinations = listOf(Screen.Mods, Screen.Accessibility, Screen.Settings, Screen.About)

@Composable
internal fun HomeTabs(state: AppState) {
    var selected by rememberSaveable { mutableIntStateOf(0) }
    var focusTarget by rememberSaveable { mutableStateOf<String?>(null) }
    val keyboard = LocalSoftwareKeyboardController.current
    val select: (Int) -> Unit = { index ->
        if (index != selected) {
            keyboard?.hide()
            focusTarget = null
            selected = index
        }
    }
    val holder = rememberSaveableStateHolder()
    val importer = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) state.importFrom(uri)
    }
    val items = listOf(
        CouiNavigationItem("模组管理", CouiIcons.Filled.Gamepad),
        CouiNavigationItem("辅助功能", CouiIcons.Filled.Accessibility),
        CouiNavigationItem("设置", CouiIcons.Filled.Setting),
        CouiNavigationItem("关于", CouiIcons.Filled.SettingDetail),
    )
    val rtl = LocalLayoutDirection.current == LayoutDirection.Rtl
    BackHandler(selected != 0) { select(0) }
    val ime = if (LocalWindowInfo.current.isWindowFocused) Modifier.imePadding() else Modifier
    Scaffold(modifier = ime, bottomBar = { HomeNavigationBar(items, selected, select) }) { insets ->
        CompositionLocalProvider(LocalHomeTabInsets provides insets) {
            AnimatedContent(targetState = selected, modifier = Modifier.fillMaxSize().clipToBounds(),
                transitionSpec = {
                    val direction = if ((targetState > initialState) != rtl) 1 else -1
                    (slideInHorizontally(tween(240)) { direction * it / 10 } + fadeIn(tween(210)))
                        .togetherWith(slideOutHorizontally(tween(210)) { -direction * it / 10 } + fadeOut(tween(150)))
                }, label = "首页底栏切换") { index ->
                holder.SaveableStateProvider(index) {
                    when (destinations[index]) {
                        Screen.Mods -> ModManagementScreen(state, openFeature = { feature ->
                            val tab = destinations.indexOf(feature.page)
                            if (tab >= 0) {
                                select(tab)
                                focusTarget = feature.title
                            } else state.openFeature(feature)
                        }) { importer.launch(zipTypes) }
                        Screen.Accessibility -> key(state.game) { AccessibilityScreen(state, focusTarget) }
                        Screen.Settings -> key(state.game) { SettingsScreen(state, focusTarget) }
                        Screen.About -> AboutScreen(state)
                        else -> error("Unknown home tab")
                    }
                }
            }
        }
    }
}

@Composable
private fun HomeNavigationBar(items: List<CouiNavigationItem>, selected: Int, select: (Int) -> Unit) {
    val ink = if (CouiTheme.isDark) Color.White else Color.Black
    if (Build.VERSION.SDK_INT >= 34) {
        val renderer = remember { runCatching { CouiFloatingRenderer() }.getOrNull() }
        if (renderer != null) {
            Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.BottomCenter) {
                NavigationBar(renderer = renderer, items = items, selectedIndex = selected,
                    selectedColor = ink, unselectedColor = ink.copy(alpha = .68f), onSelection = select)
            }
            return
        }
    }
    Row(Modifier.fillMaxWidth().background(CouiTheme.colorScheme.card).navigationBarsPadding()
        .heightIn(min = 64.dp).selectableGroup(), verticalAlignment = Alignment.CenterVertically) {
        items.forEachIndexed { index, item ->
            val source = remember { MutableInteractionSource() }
            val color = if (index == selected) ink else ink.copy(alpha = .68f)
            Column(Modifier.weight(1f).clip(CouiCapsuleShape).couiNavigationFeedback(CouiTheme.isDark, source)
                .selectable(index == selected, source, null, role = Role.Tab) { select(index) }
                .padding(vertical = 8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Image(item.icon, null, Modifier.size(24.dp), colorFilter = ColorFilter.tint(color))
                Text(text = item.title, kind = CouiTextKind.GroupTitle, color = color, maxLines = 1)
            }
        }
    }
}
