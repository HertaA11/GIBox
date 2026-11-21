package com.gibox.tool

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Rect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

internal enum class StickerCharacter(val label: String, count: Int) {
    Hutao("胡桃", 15), Citlali("茜特菈莉", 8), Furina("芙宁娜", 6);

    val files = (1..count).map { "$name/${it.toString().padStart(2, '0')}.png" }
}

internal enum class StickerDirection(val label: String) {
    Left("从右到左"), Right("从左到右"), Alternate("左右交替"),
}

internal enum class StickerArrangement(val label: String) {
    Together("整行替换"), Alternate("逐个交替"),
}

internal val cardSizes = 32..144
internal const val NO_IMAGE = "none"
internal const val WORDMARK = "title"
internal val cardImages = listOf(NO_IMAGE to "无", WORDMARK to "GIBox") +
    StickerCharacter.entries.flatMap { character ->
        character.files.mapIndexed { index, file -> file to "${character.label} ${index + 1}" }
    }
private val imageKeys = cardImages.map { it.first }.toSet()

internal data class AboutCardConfig(
    val foreground: String = WORDMARK,
    val characters: Set<StickerCharacter> = StickerCharacter.entries.toSet(),
    val transparency: Int = 0,
    val arrangement: StickerArrangement = StickerArrangement.Alternate,
    val diagonal: Boolean = true,
    val direction: StickerDirection = StickerDirection.Alternate,
    val speed: Int = 10,
    val blur: Int = 10,
    val stickerSize: Int = 80,

)

internal class AboutCardSettings(private val context: Context) {
    private val prefs = context.getSharedPreferences("about-card", Context.MODE_PRIVATE)
    private val defaults = AboutCardConfig()
    var config by mutableStateOf(AboutCardConfig(
        foreground = prefs.getString("foreground", null)?.takeIf { it in imageKeys } ?: defaults.foreground,
        characters = prefs.getStringSet("characters", null)?.let { saved ->
            StickerCharacter.entries.filter { it.name in saved }.toSet()
        } ?: prefs.getStringSet("background", null)?.let { files ->
            StickerCharacter.entries.filter { role -> role.files.any { it in files } }.toSet()
        } ?: defaults.characters,
        transparency = prefs.getInt("transparency", defaults.transparency).coerceIn(0, 100),
        arrangement = StickerArrangement.entries.find { it.name == prefs.getString("arrangement", null) } ?: defaults.arrangement,
        diagonal = prefs.getBoolean("diagonal", defaults.diagonal),
        direction = StickerDirection.entries.find { it.name == prefs.getString("direction", null) } ?: defaults.direction,
        speed = prefs.getInt("speed", defaults.speed).coerceIn(0, 20),
        blur = prefs.getInt("blur", defaults.blur).coerceIn(0, 20),
        stickerSize = prefs.getInt("stickerSize", defaults.stickerSize).coerceIn(cardSizes),
    ))
        private set

    fun update(next: AboutCardConfig) {
        if (next == config) return
        config = next
        prefs.edit().putString("foreground", next.foreground).remove("background")
            .putStringSet("characters", next.characters.map { it.name }.toSet()).putInt("transparency", next.transparency)
            .putString("arrangement", next.arrangement.name).putBoolean("diagonal", next.diagonal)
            .putString("direction", next.direction.name).putInt("speed", next.speed).putInt("blur", next.blur)
            .putInt("stickerSize", next.stickerSize).remove("titleSize").apply()
    }

    // The packed PNG contains the same files as the picker, in character and image order.
    // Decoding and uploading one small bitmap avoids waiting for every original to be decoded.
    internal val stickerAtlas by lazy {
        val bitmap = decode("atlas.png", 1)
        bitmap.prepareToDraw()
        bitmap
    }

    private val lock = Mutex()
    private var thumbnails: Map<String, Bitmap>? = null

    suspend fun thumbnails(): Map<String, Bitmap> = withContext(Dispatchers.IO) {
        lock.withLock {
            thumbnails ?: StickerCharacter.entries.flatMap { it.files }.associateWith { decode(it, 2) }
                .also { thumbnails = it }
        }
    }

    suspend fun foreground(file: String): Bitmap = withContext(Dispatchers.IO) { decode(file, 1) }

    private fun decode(file: String, sample: Int): Bitmap = context.assets.open("about_stickers/$file").use {
        requireNotNull(BitmapFactory.decodeStream(it, null, BitmapFactory.Options().apply { inSampleSize = sample }))
    }
}

private const val atlasTileSize = 192
private const val atlasColumns = 8
internal val stickerAtlasTiles = StickerCharacter.entries.flatMap { it.files }.mapIndexed { index, file ->
    val x = index % atlasColumns * atlasTileSize
    val y = index / atlasColumns * atlasTileSize
    file to Rect(x, y, x + atlasTileSize, y + atlasTileSize)
}.toMap()
