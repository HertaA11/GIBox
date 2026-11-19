package com.gibox.tool

import android.graphics.Bitmap
import android.graphics.BitmapShader
import android.graphics.BlurMaskFilter
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Shader
import android.graphics.Typeface
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.unit.dp
import com.itos.coui.layout.CouiPageDefaults
import com.itos.coui.text.CouiTextKind
import com.itos.coui.text.Text
import com.itos.coui.theme.CouiTheme

@Composable
internal fun AboutWordmark() {
    val ink = CouiTheme.colorScheme.onSurface
    val phase = rememberInfiniteTransition(label = "About wordmark").animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(16000, easing = LinearEasing), RepeatMode.Restart),
        label = "Row offset",
    )
    Box(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp)
            .clip(CouiPageDefaults.shape).heightIn(min = 192.dp),
        contentAlignment = Alignment.Center,
    ) {
        Spacer(Modifier.matchParentSize().drawWithCache {
            val pitch = 112.dp.roundToPx()
            val row = 46.dp.roundToPx()
            // The second half of the tile is transparent so a second pass can fill the alternate rows.
            val tile = Bitmap.createBitmap(pitch, row * 2, Bitmap.Config.ARGB_8888)
            val lettering = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
                textSize = 25.dp.toPx()
                textAlign = Paint.Align.CENTER
                color = ink.copy(alpha = .18f).toArgb()
                maskFilter = BlurMaskFilter(1.7.dp.toPx(), BlurMaskFilter.Blur.NORMAL)
            }
            Canvas(tile).apply {
                rotate(-12f, pitch / 2f, row / 2f)
                drawText("GIBox", pitch / 2f, (row - lettering.ascent() - lettering.descent()) / 2f, lettering)
            }
            val pattern = Paint(Paint.FILTER_BITMAP_FLAG).apply {
                shader = BitmapShader(tile, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT)
            }
            onDrawBehind {
                val offset = phase.value * pitch
                val canvas = drawContext.canvas.nativeCanvas
                for (band in 0..1) {
                    val x = if (band == 0) offset else pitch * .5f - offset
                    val y = (band * row).toFloat()
                    canvas.save()
                    canvas.translate(x, y)
                    canvas.drawRect(-x, -y, size.width - x, size.height - y, pattern)
                    canvas.restore()
                }
            }
        })
        Text(text = "GIBox", kind = CouiTextKind.HomeTitle, color = ink,
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 20.dp))
    }
}
