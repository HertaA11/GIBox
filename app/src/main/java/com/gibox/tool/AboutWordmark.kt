package com.gibox.tool

import android.graphics.Bitmap
import android.graphics.BitmapShader
import android.graphics.BlurMaskFilter
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Shader
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.platform.LocalDensity
import com.itos.coui.shape.CouiShape
import com.itos.coui.theme.CouiTheme

@Composable
internal fun AboutWordmark(version: String) {
    val ink = CouiTheme.colorScheme.onSurface
    val context = LocalContext.current
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
            val mark = requireNotNull(context.getDrawable(R.drawable.gibox_wordmark)).mutate()
            val height = 29.dp.roundToPx()
            val left = (pitch - height * 3) / 2
            val top = (row - height) / 2
            mark.setBounds(left, top, left + height * 3, top + height)
            mark.draw(Canvas(stamp))

            val blur = Paint().apply { maskFilter = BlurMaskFilter(1.7.dp.toPx(), BlurMaskFilter.Blur.NORMAL) }
            val origin = IntArray(2)
            val mask = stamp.extractAlpha(blur, origin)
            stamp.eraseColor(android.graphics.Color.TRANSPARENT)
            Canvas(stamp).drawBitmap(mask, origin[0].toFloat(), origin[1].toFloat(),
                Paint().apply { color = ink.copy(alpha = .16f).toArgb() })
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
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
            Image(painterResource(R.drawable.gibox_wordmark), "GIBox",
                Modifier.size(width = 180.dp, height = 60.dp), colorFilter = ColorFilter.tint(ink))
            BasicText(version, style = TextStyle(color = ink.copy(alpha = .85f),
                fontFamily = FontFamily.SansSerif, fontWeight = FontWeight.Medium,
                fontSize = with(LocalDensity.current) { 16.dp.toSp() }))
        }
    }
}
