package com.gibox.tool

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.itos.coui.layout.Divider
import com.itos.coui.layout.Group
import com.itos.coui.menu.CouiMenuItem
import com.itos.coui.menu.Menu
import com.itos.coui.slider.Slider
import com.itos.coui.text.Text
import com.itos.coui.text.CouiTextKind
import kotlin.math.roundToInt

@Composable
internal fun AboutSettingsScreen(settings: AboutCardSettings, back: () -> Unit) {
    Page("关于界面设置", back) { padding ->
        BoxWithConstraints(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding)) {
            val previewHeight = minOf(328.dp, maxHeight * .45f)
            Column {
                AboutWordmark(settings, previewHeight)
                ColumnPage(PaddingValues(0.dp)) { AboutCardControls(settings) }
            }
        }
    }
}

@Composable
private fun AboutCardControls(settings: AboutCardSettings) {
    val config = settings.config
    Group {
        CenterStickerPicker(settings)
        Divider()
        BackgroundCharacterPicker(settings)
        Divider()
        ChoiceRow("轮播样式", config.arrangement, StickerArrangement.entries.map { it to it.label }) {
            settings.update(settings.config.copy(arrangement = it))
        }
        Divider()
        val axis = if (config.diagonal) "斜向" else "横向"
        MenuChoiceRow("轮播动画", "$axis · ${config.direction.label}") { expanded, dismiss, anchor ->
            val items = listOf(
                CouiMenuItem("横向", "横向", selected = !config.diagonal),
                CouiMenuItem("斜向", "斜向", selected = config.diagonal),
                CouiMenuItem("更多", "更多", dividerBefore = true, children = StickerDirection.entries.map {
                    CouiMenuItem(it.name, it.label, selected = it == config.direction)
                }),
            )
            Menu(expanded = expanded, onDismissRequest = dismiss, anchor = anchor, items = items, onItemClick = {
                val current = settings.config
                settings.update(when (it.value) {
                    "横向" -> current.copy(diagonal = false)
                    "斜向" -> current.copy(diagonal = true)
                    else -> current.copy(direction = StickerDirection.valueOf(it.value))
                })
                dismiss()
            })
        }
        Divider()
        CardSlider("动画速度", config.speed, 20, ::tenths) { settings.update(settings.config.copy(speed = it)) }
        Divider()
        CardSlider("背景模糊", config.blur, 20, ::tenths) { settings.update(settings.config.copy(blur = it)) }
        Divider()
        CardSlider("贴纸透明度", config.transparency, 100, { "$it%" }) { settings.update(settings.config.copy(transparency = it)) }
        Divider()
        CardSlider("贴纸与 GIBox 大小", config.stickerSize, cardSizes.last, { "${it} dp" }, min = cardSizes.first) {
            settings.update(settings.config.copy(stickerSize = it))
        }
    }
}

private fun tenths(value: Int) = "${value / 10}.${value % 10}"

@Composable
private fun CardSlider(title: String, value: Int, max: Int, format: (Int) -> String, min: Int = 0, change: (Int) -> Unit) {
    ActionRow(title, format(value))
    Slider(modifier = Modifier.fillMaxWidth().padding(start = 20.dp, end = 20.dp).semantics { contentDescription = title },
        value = value.toFloat(), valueRange = min.toFloat()..max.toFloat(), steps = max - min - 1,
        onValueChange = { change(it.roundToInt().coerceIn(min, max)) })
    Row(Modifier.fillMaxWidth().padding(start = 20.dp, end = 20.dp, bottom = 12.dp),
        horizontalArrangement = Arrangement.SpaceBetween) {
        Text(text = format(min), kind = CouiTextKind.GroupTitle)
        Text(text = format(max), kind = CouiTextKind.GroupTitle)
    }
}
