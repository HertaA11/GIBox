package com.gibox.tool

import android.graphics.Bitmap
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.toggleable
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.*
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupPositionProvider
import androidx.compose.ui.window.PopupProperties
import com.itos.coui.backdrop.drawCouiBackdrop
import com.itos.coui.effect.couiButtonFeedback
import com.itos.coui.checkbox.Checkbox
import com.itos.coui.menu.CouiMenuStyle
import com.itos.coui.menu.CouiMenuDefaults
import com.itos.coui.menu.rememberCouiWindowBackdrop
import com.itos.coui.shape.CouiShape
import com.itos.coui.text.CouiTextKind
import com.itos.coui.text.Text

@Composable
internal fun StickerPickerRow(title: String, settings: AboutCardSettings, multiple: Boolean) {
    val config = settings.config
    val selected = if (multiple) config.background.ifEmpty { setOf(NO_IMAGE) } else setOf(config.foreground)
    val summary = if (selected.size == 1) cardImages.first { it.first in selected }.second else "已选 ${selected.size} 项"
    MenuChoiceRow(title, summary) { expanded, dismiss, _ ->
        val visible = remember { MutableTransitionState(false) }.apply { targetState = expanded }
        if (visible.currentState || visible.targetState) {
            val images by produceState<Map<String, Bitmap>>(emptyMap(), settings) { value = settings.thumbnails() }
            val margin = with(LocalDensity.current) { 8.dp.roundToPx() }
            Popup(popupPositionProvider = remember(margin) { GridPosition(margin) }, onDismissRequest = dismiss,
                properties = PopupProperties(focusable = true)) {
                AnimatedVisibility(visibleState = visible, enter = fadeIn() + scaleIn(initialScale = .96f),
                    exit = fadeOut() + scaleOut(targetScale = .96f)) {
                    val style = CouiMenuDefaults.style()
                    val backdrop = rememberCouiWindowBackdrop()
                    val cfg = LocalConfiguration.current
                    val surface = if (backdrop == null) Modifier.background(style.colors.container) else
                        Modifier.drawCouiBackdrop(backdrop, shape = { style.shape }, effects = { blur(16.dp.toPx()) },
                            onDrawSurface = { drawRect(style.colors.container.copy(alpha = .88f)) })
                    LazyVerticalGrid(columns = GridCells.Fixed(4),
                        modifier = Modifier.width(minOf(368.dp, (cfg.screenWidthDp.dp - 24.dp).coerceAtLeast(1.dp)))
                            .heightIn(max = minOf(360.dp, (cfg.screenHeightDp.dp - 48.dp).coerceAtLeast(60.dp)))
                            .shadow(5.dp, style.shape).clip(style.shape).then(surface)
                            .border(1.dp, style.colors.outlineVariant.copy(alpha = .35f), style.shape),
                        contentPadding = PaddingValues(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp),
                        horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        items(cardImages, key = { it.first }) { (file, label) ->
                            val checked = file in selected
                            val choose = {
                                val current = settings.config
                                if (!multiple) {
                                    settings.update(current.copy(foreground = file))
                                    dismiss()
                                } else {
                                    val next = when {
                                        file == NO_IMAGE -> emptySet()
                                        file in current.background -> current.background - file
                                        else -> current.background + file
                                    }
                                    settings.update(current.copy(background = next))
                                }
                            }
                            StickerCell(file, label, images[file], multiple, checked, style, choose)
                        }
                        if (multiple) item(span = { GridItemSpan(maxLineSpan) }) {
                            Row(Modifier.fillMaxWidth()) {
                                StickerCharacter.entries.forEach { character ->
                                    val all = config.background.containsAll(character.files)
                                    Box(Modifier.weight(1f).heightIn(min = 48.dp)
                                        .toggleable(all, role = Role.Checkbox) {
                                            val current = settings.config
                                            val next = if (current.background.containsAll(character.files))
                                                current.background - character.files.toSet() else current.background + character.files
                                            settings.update(current.copy(background = next))
                                        }.semantics { contentDescription = "全选${character.label}贴纸" },
                                        contentAlignment = Alignment.Center) {
                                        Row(verticalAlignment = Alignment.CenterVertically) {
                                            Checkbox(Modifier.size(16.dp), all, colors = style.checkboxColors)
                                            Text(text = character.label, kind = CouiTextKind.GroupTitle)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun StickerCell(file: String, label: String, bitmap: Bitmap?, multiple: Boolean, checked: Boolean,
    style: CouiMenuStyle, choose: () -> Unit) {
    val source = remember { MutableInteractionSource() }
    val action = if (multiple) Modifier.toggleable(checked, source, null, role = Role.Checkbox) { choose() }
        else Modifier.selectable(checked, source, null, role = Role.RadioButton, onClick = choose)
    Column(Modifier.fillMaxWidth().clip(CouiShape(12.dp))
        .background(if (checked) style.colors.primary.copy(alpha = .12f) else Color.Transparent)
        .couiButtonFeedback(interactionSource = source).then(action).padding(4.dp),
        horizontalAlignment = Alignment.CenterHorizontally) {
        Box(Modifier.fillMaxWidth().height(46.dp), contentAlignment = Alignment.Center) {
            if (bitmap != null) Image(remember(bitmap) { bitmap.asImageBitmap() }, null, Modifier.size(44.dp))
            else if (file == WORDMARK || file == NO_IMAGE) {
                Text(text = if (file == WORDMARK) "GIBox" else "∅", kind = CouiTextKind.ItemTitle)
            }
            if (multiple) Checkbox(modifier = Modifier.align(Alignment.TopEnd).size(16.dp),
                checked = checked, colors = style.checkboxColors)
        }
        Text(text = label, kind = CouiTextKind.GroupTitle, textAlign = TextAlign.Center)
    }
}

private class GridPosition(private val margin: Int) : PopupPositionProvider {
    override fun calculatePosition(anchorBounds: IntRect, windowSize: IntSize, layoutDirection: LayoutDirection,
        popupContentSize: IntSize): IntOffset {
        val right = (windowSize.width - margin - popupContentSize.width).coerceAtLeast(margin)
        val x = (anchorBounds.center.x - popupContentSize.width / 2).coerceIn(margin, right)
        val bottom = (windowSize.height - margin - popupContentSize.height).coerceAtLeast(margin)
        val below = anchorBounds.bottom + margin
        val above = anchorBounds.top - margin - popupContentSize.height
        val y = when {
            below <= bottom -> below
            above >= margin -> above
            windowSize.height - anchorBounds.bottom >= anchorBounds.top -> below
            else -> above
        }
        return IntOffset(x, y.coerceIn(margin, bottom))
    }
}
