package com.gibox.tool

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.dismiss
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.itos.coui.backdrop.CouiBackdrop
import com.itos.coui.backdrop.drawCouiBackdrop
import com.itos.coui.dialog.CouiDialogDefaults
import com.itos.coui.effect.couiButtonFeedback
import com.itos.coui.shape.CouiCapsuleShape
import com.itos.coui.text.CouiTextKind
import com.itos.coui.text.Text
import com.itos.coui.theme.CouiTheme

@Composable
internal fun AboutResetConfirmation(
    visibility: MutableTransitionState<Boolean>,
    backdrop: CouiBackdrop,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit,
) {
    val open = visibility.targetState
    BackHandler(open || visibility.currentState || !visibility.isIdle, onBack = onDismiss)
    val dismissLatest = rememberUpdatedState(onDismiss)
    AnimatedVisibility(
        visibleState = visibility,
        enter = fadeIn(tween(180)),
        exit = fadeOut(tween(160)),
    ) {
        val colors = CouiDialogDefaults.themeColors()
        val shape = CouiDialogDefaults.shape
        val title = "是否重置当前设置"
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.BottomCenter) {
            Box(Modifier.matchParentSize().background(Color.Black.copy(alpha = colors.dimAmount))
                .pointerInput(Unit) { detectTapGestures { dismissLatest.value() } })
            Column(Modifier.navigationBarsPadding()
                .widthIn(max = CouiDialogDefaults.maxWidth)
                .fillMaxWidth()
                .padding(horizontal = CouiDialogDefaults.horizontalMargin, vertical = 16.dp)
                .animateEnterExit(
                    enter = slideInVertically(tween(230)) { it },
                    exit = slideOutVertically(tween(180)) { it },
                )
                .drawCouiBackdrop(backdrop, shape = { shape }, effects = { blur(24.dp.toPx()) },
                    onDrawSurface = { drawRect(colors.surface.copy(alpha = .70f)) })
                .pointerInput(Unit) {
                    awaitPointerEventScope { while (true) awaitPointerEvent() }
                }
                .semantics {
                    paneTitle = title
                    dismiss { dismissLatest.value(); true }
                }) {
                Text(modifier = Modifier.fillMaxWidth().padding(
                    start = CouiDialogDefaults.contentPadding, end = CouiDialogDefaults.contentPadding,
                    top = CouiDialogDefaults.titleTop, bottom = CouiDialogDefaults.titleBottom,
                ).semantics { heading() }, text = title, kind = CouiTextKind.TopBar,
                    color = colors.title, textAlign = TextAlign.Center)
                Text(modifier = Modifier.fillMaxWidth().padding(horizontal = CouiDialogDefaults.contentPadding)
                    .padding(bottom = CouiDialogDefaults.messageBottom), text = "请确认下面的操作",
                    kind = CouiTextKind.ItemSummary, color = colors.message, textAlign = TextAlign.Center)
                val primary = CouiDialogDefaults.primaryActionColors(CouiTheme.colorScheme.accent)
                Row(Modifier.fillMaxWidth().padding(horizontal = CouiDialogDefaults.buttonEdgePadding)
                    .padding(bottom = CouiDialogDefaults.buttonBottomPadding),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    ResetAction(Modifier.weight(1f), "取消", colors.surface.copy(alpha = .76f), colors.action, open, onDismiss)
                    ResetAction(Modifier.weight(1f), "确定", primary.surface, primary.content, open, onConfirm)
                }
            }
        }
    }
}

@Composable
private fun ResetAction(modifier: Modifier, text: String, surface: Color, ink: Color,
    enabled: Boolean, click: () -> Unit) {
    val source = remember { MutableInteractionSource() }
    Box(modifier.heightIn(min = CouiDialogDefaults.buttonHeight)
        .clip(CouiCapsuleShape).background(surface)
        .couiButtonFeedback(interactionSource = source, enabled = enabled)
        .clickable(source, indication = null, enabled = enabled, role = Role.Button, onClick = click)
        .padding(horizontal = CouiDialogDefaults.buttonHorizontalPadding, vertical = 4.dp),
        contentAlignment = Alignment.Center) {
        Text(text = text, kind = CouiTextKind.ItemTitle, color = ink)
    }
}
