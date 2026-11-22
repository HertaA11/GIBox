package com.gibox.tool

import android.graphics.Bitmap
import android.graphics.Paint
import android.graphics.RectF
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

internal val LocalStickerPageActive = compositionLocalOf { true }

@Composable
internal fun AboutWordmark(settings: AboutCardSettings, height: Dp = 328.dp) {
    val config = settings.config
    Box(Modifier.fillMaxWidth().padding(start = 16.dp, top = 12.dp, end = 16.dp)
        .clip(CouiShape(28.dp, 3f)).height(height), contentAlignment = Alignment.Center) {
        CardStickerBackground(settings, Modifier.matchParentSize())
        when (val file = config.foreground) {
            WORDMARK -> BasicText("GIBox", style = rememberCouiTextStyle(CouiTextKind.HomeTitle)
                .copy(color = CouiTheme.colorScheme.onSurface, fontSize = (config.foregroundSize / 2f).sp, lineHeight = (config.foregroundSize / 2f).sp))
            NO_IMAGE -> Unit
            else -> {
                val image = remember(settings, file) { settings.foreground(file).asImageBitmap() }
                val description = cardImages.first { it.first == file }.second
                Image(image, description, Modifier.size(config.foregroundSize.dp),
                    alpha = 1f - config.transparency / 100f)
            }
        }
    }
}

@Composable
internal fun CardStickerBackground(settings: AboutCardSettings, modifier: Modifier) {
    val config = settings.config
    if (config.characters.isEmpty() || config.transparency == 100) return
    val atlas = remember(settings) { settings.stickerAtlas }
    StickerRows(config, atlas, modifier.blur((config.blur * .4f).dp))
}

@Composable
private fun StickerRows(config: AboutCardConfig, atlas: Bitmap, modifier: Modifier) {
    val playlist = remember(config.characters, config.arrangement) {
        StickerPlaylist(config.characters, config.arrangement == StickerArrangement.Together)
    }
    val period = playlist.period
    val position = remember { mutableDoubleStateOf(0.0) }
    val speed by rememberUpdatedState(config.speed / 10f)
    val animate = config.speed != 0 && (!config.pauseOnOtherPages || LocalStickerPageActive.current)
    LaunchedEffect(period, animate) {
        if (!animate) return@LaunchedEffect
        val durationScale = coroutineContext[MotionDurationScale]
        snapshotFlow { durationScale?.scaleFactor ?: 1f }.collectLatest { scale ->
            if (scale <= 0f) return@collectLatest
            var previous = withFrameNanos { it }
            while (true) withFrameNanos { now ->
                // Skip the backlog after a stalled frame or when returning from the background.
                val elapsed = (now - previous).coerceIn(0L, 50_000_000L)
                position.doubleValue = (position.doubleValue + elapsed * speed.toDouble() /
                    (3_400_000_000.0 * scale)) % period
                previous = now
            }
        }
    }
    Spacer(modifier.drawWithCache {
        val side = config.backgroundSize.dp.toPx()
        val halfCellWidth = side / 2
        val halfCellHeight = side / 2
        val maxAlpha = 255f * (1f - config.transparency / 100f)
        val pitchX = halfCellWidth * 2.24f
        val pitchY = halfCellHeight * 2.24f
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
            val fade = if (playlist.wholeRows) ((progress - scene * 3 - 2.8f) / .2f).coerceIn(0f, 1f) else 0f
            val canvas = drawContext.canvas.nativeCanvas
            canvas.save()
            canvas.translate(size.width / 2, size.height / 2)
            canvas.rotate(angle)
            fun drawTile(file: String, x: Float, y: Float, opacity: Float) {
                val source = stickerAtlasTiles.getValue(file)
                rect.set(x - side / 2, y - side / 2, x + side / 2, y + side / 2)
                paint.alpha = (maxAlpha * opacity).roundToInt()
                canvas.drawBitmap(atlas, source, rect, paint)
            }
            for (row in -lastRow..lastRow) {
                // Row zero stays at the card center as the visible range expands or contracts.
                val odd = Math.floorMod(row, 2) == 1
                val negative = config.direction == StickerDirection.Left || (config.direction == StickerDirection.Alternate && odd)
                val travel = if (negative) -progress else progress
                val whole = floor(travel).toInt()
                val offset = travel - whole + if (odd) .5f else 0f
                val first = ceil((-halfWidth - halfCellWidth) / pitchX - offset).toInt()
                val last = floor((halfWidth + halfCellWidth) / pitchX - offset).toInt()
                for (col in first..last) {
                    val x = (col + offset) * pitchX
                    val y = row * pitchY
                    val slot = col - whole
                    drawTile(playlist.fileAt(row, slot, scene), x, y, 1f - fade)
                    if (fade > 0f) drawTile(playlist.fileAt(row, slot, scene + 1), x, y, fade)
                }
            }
            canvas.restore()
        }
    })
}
