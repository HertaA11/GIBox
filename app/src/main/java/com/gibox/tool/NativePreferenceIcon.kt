package com.gibox.tool

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ColorFilter
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PixelFormat
import android.graphics.drawable.Drawable
import android.util.TypedValue

/** Circular Settings badges with simple line-art glyphs, sized in dp rather than pixels. */
internal class NativePreferenceIcon(context: Context, private val glyph: OneGlyph, private val badge: Boolean = true) : Drawable() {
    private val size = ((if (badge) 36 else 24) * context.resources.displayMetrics.density + .5f).toInt()
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { strokeCap = Paint.Cap.ROUND; strokeJoin = Paint.Join.ROUND }
    private val ink = if (badge) Color.WHITE else TypedValue().also {
        context.theme.resolveAttribute(android.R.attr.textColorPrimary, it, true)
    }.let { if (it.resourceId != 0) context.getColor(it.resourceId) else it.data }
    private val fill = when (glyph) {
        OneGlyph.Parcel, OneGlyph.Folder, OneGlyph.Refresh -> 0xff387aff.toInt()
        OneGlyph.Grid, OneGlyph.Check -> 0xff63bd38.toInt()
        OneGlyph.Wrench -> 0xff8580bd.toInt()
        else -> 0xff6e86a7.toInt()
    }
    private var opacity = 255

    override fun getIntrinsicWidth() = size
    override fun getIntrinsicHeight() = size
    override fun getOpacity() = PixelFormat.TRANSLUCENT
    override fun setAlpha(alpha: Int) { opacity = alpha; invalidateSelf() }
    override fun setColorFilter(colorFilter: ColorFilter?) { paint.colorFilter = colorFilter; invalidateSelf() }

    override fun draw(canvas: Canvas) {
        val save = canvas.save()
        canvas.translate(bounds.left.toFloat(), bounds.top.toFloat())
        canvas.scale(bounds.width() / 24f, bounds.height() / 24f)
        paint.alpha = opacity
        if (badge) {
            paint.color = fill
            paint.style = Paint.Style.FILL
            canvas.drawCircle(12f, 12f, 12f, paint)
            canvas.translate(4.5f, 4.5f)
            canvas.scale(.625f, .625f)
        }
        paint.color = ink
        paint.alpha = opacity
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = 1.7f
        fun line(x: Float, y: Float, a: Float, b: Float) = canvas.drawLine(x, y, a, b, paint)
        fun path(vararg points: Float) {
            val p = Path().apply {
                moveTo(points[0], points[1])
                for (i in 2 until points.size step 2) lineTo(points[i], points[i + 1])
            }
            canvas.drawPath(p, paint)
        }
        when (glyph) {
            OneGlyph.Parcel -> {
                path(3f,7f,12f,3f,21f,7f,21f,18f,12f,22f,3f,18f,3f,7f,12f,11f,21f,7f)
                line(12f,11f,12f,22f); line(7f,5f,16f,9f)
            }
            OneGlyph.Grid -> {
                paint.style = Paint.Style.FILL
                for (x in listOf(3f,14f)) for (y in listOf(3f,14f)) canvas.drawRoundRect(x,y,x+7f,y+7f,1.3f,1.3f,paint)
            }
            OneGlyph.Wrench -> {
                path(14f,3f,12f,7f,16f,11f,20f,9f)
                path(14f,3f,10f,4f,8f,8f,9f,12f,3f,18f,6f,21f,12f,15f,16f,16f,20f,13f,20f,9f)
            }
            OneGlyph.Info -> { canvas.drawCircle(12f,12f,10f,paint); line(12f,11f,12f,17f); line(12f,7f,12f,7.1f) }
            OneGlyph.Search -> { canvas.drawCircle(10f,10f,7f,paint); line(15f,15f,22f,22f) }
            OneGlyph.Folder -> path(2f,6f,9f,6f,11f,9f,22f,9f,22f,20f,2f,20f,2f,6f)
            OneGlyph.Refresh -> { canvas.drawArc(2f,2f,22f,22f,30f,290f,false,paint); path(20f,3f,20f,9f,14f,9f) }
            OneGlyph.Person -> { canvas.drawCircle(12f,6f,4f,paint); path(3f,22f,4f,17f,8f,14f,16f,14f,20f,17f,21f,22f) }
            OneGlyph.Share -> { path(5f,9f,5f,21f,20f,21f,20f,9f); path(8f,6f,12f,2f,16f,6f); line(12f,2f,12f,15f) }
            OneGlyph.Check -> path(4f,12f,10f,18f,21f,5f)
            OneGlyph.Arrow -> path(9f,5f,16f,12f,9f,19f)
            OneGlyph.Pencil -> { path(4f,16f,16f,4f,20f,8f,8f,20f,3f,21f,4f,16f); line(13f,7f,17f,11f) }
            OneGlyph.Trash -> { path(4f,6f,20f,6f); path(8f,6f,8f,3f,16f,3f,16f,6f); path(6f,6f,7f,21f,17f,21f,18f,6f) }
        }
        canvas.restoreToCount(save)
    }
}
