package com.gibox.tool

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.selectable
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.itos.coui.dialog.Dialog
import com.itos.coui.effect.couiButtonFeedback
import com.itos.coui.menu.CouiMenuItem
import com.itos.coui.menu.Menu
import com.itos.coui.shape.CouiShape
import com.itos.coui.text.CouiTextKind
import com.itos.coui.text.Text
import com.itos.coui.theme.CouiTheme

@Composable
internal fun CenterStickerPicker(settings: AboutCardSettings) {
    val current = settings.config.foreground
    var visible by remember { mutableStateOf(false) }
    var opened by remember { mutableStateOf(false) }
    val close = { visible = false }
    ActionRow("中央图标选择", cardImages.first { it.first == current }.second, showNext = true) {
        opened = true
        visible = true
    }
    if (!opened) return
    val images by produceState<Map<String, Bitmap>>(emptyMap(), settings) { value = settings.thumbnails() }
    Dialog(visible = visible, material = dialogMaterial(), title = "中央图标选择", onDismissRequest = close,
        confirmAction = primaryDialogAction("完成", close)) {
        // The COUI dialog owns scrolling and keeps its action outside the scrolling body.
        for (row in cardImages.chunked(4)) {
            Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 2.dp),
                horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                row.forEach { (file, label) ->
                    val selected = file == current
                    val source = remember { MutableInteractionSource() }
                    Column(Modifier.weight(1f).clip(CouiShape(12.dp))
                        .background(if (selected) CouiTheme.colorScheme.accent.copy(alpha = .16f) else Color.Transparent)
                        .couiButtonFeedback(interactionSource = source)
                        .selectable(selected, source, null, role = Role.RadioButton) {
                            settings.update(settings.config.copy(foreground = file))
                        }.padding(vertical = 4.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                        Box(Modifier.height(46.dp), contentAlignment = Alignment.Center) {
                            val image = images[file]
                            if (image != null) Image(remember(image) { image.asImageBitmap() }, null, Modifier.size(44.dp))
                            else if (file == WORDMARK || file == NO_IMAGE)
                                Text(text = if (file == WORDMARK) "GIBox" else "∅", kind = CouiTextKind.ItemTitle)
                        }
                        Text(text = label, kind = CouiTextKind.GroupTitle, textAlign = TextAlign.Center)
                    }
                }
                repeat(4 - row.size) { Spacer(Modifier.weight(1f)) }
            }
        }
    }
}

@Composable
internal fun BackgroundCharacterPicker(settings: AboutCardSettings) {
    val selected = settings.config.characters
    val roles = StickerCharacter.entries
    val summary = roles.filter { it in selected }.joinToString("、") { it.label }.ifEmpty { "无" }
    MenuChoiceRow("轮播背景", summary) { expanded, dismiss, anchor ->
        Menu(expanded = expanded, onDismissRequest = dismiss, anchor = anchor,
            modifier = Modifier.heightIn(max = 320.dp),
            items = roles.map { CouiMenuItem(it, it.label, checked = it in selected) },
            onItemClick = { item ->
                val config = settings.config
                val next = if (item.value in config.characters) config.characters - item.value
                    else config.characters + item.value
                settings.update(config.copy(characters = next))
            })
    }
}
