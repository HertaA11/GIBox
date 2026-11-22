package com.gibox.tool

internal class StickerPlaylist(selected: Set<StickerCharacter>, val wholeRows: Boolean) {
    private val roles = StickerCharacter.entries.filter { it in selected }
    private val pinHuTao = wholeRows && StickerCharacter.Hutao in selected

    init { require(roles.isNotEmpty()) }

    // Each character gets exactly one turn per round, even after a shorter folder wraps.
    // End the animation only after all folders and the fixed middle row line up again.
    val period: Int = run {
        var rounds = 1
        while (roles.any { rounds % it.files.size != 0 }) rounds++
        rounds * roles.size * if (wholeRows) 3 else 1
    }

    fun fileAt(row: Int, slot: Int, scene: Int): String {
        val turn = if (wholeRows) scene + row else slot + row
        if (row == 0 && pinHuTao) {
            val files = StickerCharacter.Hutao.files
            return files[Math.floorMod(scene, files.size)]
        }
        val files = roles[Math.floorMod(turn, roles.size)].files
        return files[Math.floorMod(Math.floorDiv(turn, roles.size), files.size)]
    }
}
