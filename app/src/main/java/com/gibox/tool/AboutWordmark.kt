package com.gibox.tool

import android.graphics.Bitmap
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.MotionDurationScale
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.itos.coui.shape.CouiShape
import com.itos.coui.text.CouiTextKind
import com.itos.coui.text.rememberCouiTextStyle
import com.itos.coui.theme.CouiTheme
import kotlinx.coroutines.flow.collectLatest
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.roundToInt
import kotlin.math.sin

@Composable
internal fun AboutWordmark(settings: AboutCardSettings, height: Dp = 328.dp) {
    val config = settings.config
    val hasStickers = config.background.any { it != WORDMARK }
    val images by produceState<Map<String, Bitmap>>(emptyMap(), settings, hasStickers) {
        value = if (hasStickers) settings.thumbnails() else emptyMap()
    }
    Box(Modifier.fillMaxWidth().padding(start = 16.dp, top = 12.dp, end = 16.dp)
        .clip(CouiShape(28.dp, 3f)).height(height), contentAlignment = Alignment.Center) {
        if (config.background.isNotEmpty() && (images.isNotEmpty() || config.background == setOf(WORDMARK))) {
            StickerRows(config, images, Modifier.matchParentSize().blur((config.blur * .4f).dp))
        }
        when (val file = config.foreground) {
            WORDMARK -> BasicText("GIBox", style = rememberCouiTextStyle(CouiTextKind.HomeTitle)
                .copy(color = CouiTheme.colorScheme.onSurface, fontSize = config.titleSize.sp, lineHeight = config.titleSize.sp))
            NO_IMAGE -> Unit
            else -> key(file) {
                val full by produceState<Bitmap?>(null, settings, file) { value = settings.foreground(file) }
                val bitmap = full ?: images[file]
                if (bitmap != null) Image(remember(bitmap) { bitmap.asImageBitmap() },
                    cardImages.first { it.first == file }.second, Modifier.size(config.stickerSize.dp))
            }
        }
    }
}

@Composable
private fun StickerRows(config: AboutCardConfig, images: Map<String, Bitmap>, modifier: Modifier) {
    // Grid order is stable when the user selects files in a different order or restarts the app.
    val sequence = remember(config.background, images) {
        cardImages.filter { it.first in config.background }.map { images[it.first] }
    }
    val wholeRows = config.arrangement == StickerArrangement.Together
    val period = sequence.size * if (wholeRows) 3 else 1
    val position = remember { mutableDoubleStateOf(0.0) }
    val speed by rememberUpdatedState(config.speed / 10f)
    LaunchedEffect(period, config.speed == 0) {
        if (speed == 0f) return@LaunchedEffect
        val durationScale = coroutineContext[MotionDurationScale]
        snapshotFlow { durationScale?.scaleFactor ?: 1f }.collectLatest { scale ->
            if (scale <= 0f) return@collectLatest
            var previous = withFrameNanos { it }
            while (true) withFrameNanos { now ->
                position.doubleValue = (position.doubleValue + (now - previous) * speed.toDouble() /
                    (3_400_000_000.0 * scale)) % period
                previous = now
            }
        }
    }
    val ink = CouiTheme.colorScheme.onSurface
    Spacer(modifier.drawWithCache {
        val side = config.stickerSize.dp.toPx()
        val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
            textSize = config.titleSize.sp.toPx()
            textAlign = Paint.Align.CENTER
            color = ink.toArgb()
        }
        val bitmapSide = if (sequence.any { it != null }) side else 0f
        val hasWord = WORDMARK in config.background
        val halfCellWidth = maxOf(bitmapSide, if (hasWord) text.measureText("GIBox") else 0f) / 2f
        val halfCellHeight = maxOf(bitmapSide, if (hasWord) text.descent() - text.ascent() else 0f) / 2f
        val pitchX = halfCellWidth * 2.24f
        val pitchY = halfCellHeight * 2.24f
        val baseline = -(text.descent() + text.ascent()) / 2f
        val angle = if (config.diagonal) -16f else 0f
        val radians = Math.toRadians(angle.toDouble())
        val c = abs(cos(radians)).toFloat()
        val s = abs(sin(radians)).toFloat()
        // Inverse-rotate the viewport bounds and include any cell whose edge intersects them.
        val halfWidth = (size.width * c + size.height * s) / 2
        val halfHeight = (size.height * c + size.width * s) / 2
        val lastRow = floor((halfHeight + halfCellHeight) / pitchY).toInt()
        val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
        val rect = RectF()
        onDrawBehind {
            val progress = position.doubleValue.toFloat()
            val scene = floor(progress / 3f).toInt()
            val fade = if (wholeRows && sequence.size > 1) ((progress - scene * 3 - 2.8f) / .2f).coerceIn(0f, 1f) else 0f
            val canvas = drawContext.canvas.nativeCanvas
            canvas.save()
            canvas.translate(size.width / 2, size.height / 2)
            canvas.rotate(angle)
            fun drawTile(index: Int, x: Float, y: Float, opacity: Float) {
                val bitmap = sequence[Math.floorMod(index, sequence.size)]
                val alpha = (120 * opacity).roundToInt()
                if (bitmap == null) {
                    text.alpha = alpha
                    canvas.drawText("GIBox", x, y + baseline, text)
                } else {
                    rect.set(x - side / 2, y - side / 2, x + side / 2, y + side / 2)
                    paint.alpha = alpha
                    canvas.drawBitmap(bitmap, null, rect, paint)
                }
            }
            for (row in -lastRow..lastRow) {
                val ordinal = row + lastRow
                val odd = ordinal % 2 == 1
                val negative = config.direction == StickerDirection.Left || (config.direction == StickerDirection.Alternate && odd)
                val travel = if (negative) -progress else progress
                val whole = floor(travel).toInt()
                val offset = travel - whole + if (odd) .5f else 0f
                val first = ceil((-halfWidth - halfCellWidth) / pitchX - offset).toInt()
                val last = floor((halfWidth + halfCellWidth) / pitchX - offset).toInt()
                for (col in first..last) {
                    val x = (col + offset) * pitchX
                    val y = row * pitchY
                    val index = if (wholeRows) scene + ordinal else col - whole + ordinal
                    drawTile(index, x, y, 1f - fade)
                    if (fade > 0f) drawTile(index + 1, x, y, fade)
                }
            }
            canvas.restore()
        }
    })
}
