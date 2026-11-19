package com.gibox.tool

import android.graphics.Bitmap
import android.graphics.BitmapShader
import android.graphics.BlurMaskFilter
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.graphics.Shader
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
import com.itos.coui.shape.CouiShape
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
        Modifier.fillMaxWidth().padding(start = 16.dp, top = 12.dp, end = 16.dp)
            .clip(CouiShape(28.dp, 3f)).heightIn(min = 328.dp),
        contentAlignment = Alignment.Center,
    ) {
        Spacer(Modifier.matchParentSize().drawWithCache {
            val pitch = 126.dp.roundToPx()
            val row = 48.dp.roundToPx()
            val stamp = Bitmap.createBitmap(pitch, row * 2, Bitmap.Config.ARGB_8888)
            val lettering = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
                textSize = 25.dp.toPx()
                textAlign = Paint.Align.CENTER
                color = ink.copy(alpha = .16f).toArgb()
                maskFilter = BlurMaskFilter(1.7.dp.toPx(), BlurMaskFilter.Blur.NORMAL)
            }
            Canvas(stamp).drawText("GIBox", pitch / 2f, (row - lettering.ascent() - lettering.descent()) / 2f, lettering)
            val pattern = Paint(Paint.FILTER_BITMAP_FLAG).apply {
                shader = BitmapShader(stamp, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT)
            }
            onDrawBehind {
                val offset = phase.value * pitch
                val canvas = drawContext.canvas.nativeCanvas
                canvas.save()
                // Both the row grid and its translation share these tilted axes.
                canvas.rotate(-16f, size.width / 2f, size.height / 2f)
                for (band in 0..1) {
                    canvas.save()
                    canvas.translate(if (band == 0) offset else pitch * .5f - offset, (band * row).toFloat())
                    canvas.drawPaint(pattern)
                    canvas.restore()
                }
                canvas.restore()
            }
        })
        Text(text = "GIBox", kind = CouiTextKind.HomeTitle, color = ink)
    }
}
