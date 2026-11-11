package com.gibox.tool

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.content.pm.LabeledIntent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.DocumentsContract
import android.widget.Toast
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.io.File
import kotlin.concurrent.thread

const val BUILD_ID = "6d85e3141bcd6b508f3e853bab508cc407278eac"
private const val LOGGER = "importer"

/** Pages; the app keeps a back stack of them (system back pops it). */
sealed interface Screen {
    data object Home : Screen
    data object Settings : Screen
    data object Log : Screen
    data object Manual : Screen
    /** Debug and test tools (strategies, test rounds, experiments, bone export, the old manual conversion). */
    data object Debug : Screen
    /** The mods of one character (id 0: mods without a chosen character). */
    data class Character(val id: Int) : Screen
    data class Detail(val name: String) : Screen
    /**
     * Character choice. For a mod being imported, `pending` is its copy in Mods/.pending (dropped when the page is left
     * without 确定); for an imported mod, `source` is its folder in Mods.
     */
    data class Pick(val modName: String, val importedAs: String, val pending: File?, val source: File?, val current: Int) : Screen
}

/** A message or a question. `neutral` is a third choice (另存). */
class Prompt(
    val title: String,
    val text: String,
    val confirm: String = "确定",
    val onConfirm: () -> Unit = {},
    val dismiss: String? = null,
    val neutral: String? = null,
    val onNeutral: () -> Unit = {},
    val destructive: Boolean = false,
)

class Busy(val title: String, val text: String)

/** Everything the pages show and do. File work runs on a background thread; state is changed on the UI thread only. */
class AppState(private val activity: Activity) {
    /** The games GIBox may work for (more than one: both ticked in LSPosed and installed; the home card switches). */
    var games by mutableStateOf<List<String>>(emptyList())
        private set
    /** The game package GIBox works for, picked by detectGame (no setting). */
    var game by mutableStateOf(detectGame())
        private set
    var mods by mutableStateOf<List<Importer.Mod>>(emptyList())
        private set
    var status by mutableStateOf(Control.Status())
        private set
    var boneRefreshRequest by mutableStateOf(0L)
        private set
    var boneRefreshCharacter by mutableStateOf(0)
        private set
    var enabled by mutableStateOf(false)
        private set
    var storage by mutableStateOf(false)
        private set
    /** LSPosed loaded GIBox into its own process (ModuleStatus, set by Entry). */
    var moduleActive by mutableStateOf(false)
        private set
    var frameworkApi by mutableStateOf(0)
        private set
    var logLevel by mutableStateOf("info")
        private set
    /** This APK's versionName (app/build.gradle.kts), read from the package rather than written in the source. */
    val appVersion: String = packageInfo(activity.packageName)?.versionName ?: "?"
    var busy by mutableStateOf<Busy?>(null)
        private set
    var prompt by mutableStateOf<Prompt?>(null)
    /** Mods entry whose name is being edited (RenameDialog). */
    var renaming by mutableStateOf<String?>(null)
    /** Names given to mods (Labels), by Mods entry name. */
    var labels by mutableStateOf<Map<String, String>>(emptyMap())
        private set
    /** Bumped when control.json changes (variants, selection), so pages re-read it. */
    var revision by mutableStateOf(0)
        private set
    val stack = mutableStateListOf<Screen>(Screen.Home)

    /** Imported mods: GIBox's private storage, one copy for every game. Models, skeletons, control/status and the log:
     *  each game's media folder (a mod is converted for every game it is used in). */
    val modsRoot: File get() = Control.mods(activity.filesDir)
    val modelsRoot: File get() = Control.models(game)
    val boneRoot: File get() = Control.bones(game)
    val logFile: File get() = AppLog.file(game)
    private val labelsFile: File get() = File(activity.filesDir, "labels/library.json")

    /** The game the PC hash database was installed for this run (installPcHashes, called from refresh). */
    private var pcHashesFor = ""

    init {
        AppLog.debug(game, "app", "app-start 版本 $appVersion")
        // GIBox's outfit table (assets/outfits.json): the original look and the skins of each character, for the picker,
        // the mod lists and the converter.
        runCatching { Outfits.use(Outfits.parse(activity.assets.open("outfits.json").use { it.readBytes() }.toString(Charsets.UTF_8))) }
            .onFailure { AppLog.warning(game, LOGGER, "outfits-error 外观对照表读取失败：${it.message}") }
        // File access is asked for once, at the first start after installing; later a feature that needs it asks again.
        if (!hasStorage() && readPrivate("storage-asked.txt").isEmpty()) { writePrivate("storage-asked.txt", "1"); grantStorage() }
        runCatching { Control.retireExperiments(game) }
        mergeMods()
        Importer.discardAll(modsRoot)
        refresh()
    }
    /** Before 0.7.101 each game had its own Mods folder and labels: merged into the shared ones, this game's first. */
    private fun mergeMods() {
        for (pkg in listOf(game) + Control.GAMES.filter { it != game }) {
            val legacy = Control.legacyMods(activity.filesDir, pkg)
            val oldLabels = File(activity.filesDir, "labels/$pkg.json")
            if (!legacy.isDirectory && !oldLabels.isFile) continue
            try {
                val log = StringBuilder()
                val renamed = Importer.mergeMods(modsRoot, legacy, Control.models(pkg), log)
                val names = renamed.associate { it.from to it.to }
                for ((name, text) in Labels.read(oldLabels)) { val n = names[name] ?: name; if (Labels.read(labelsFile)[n] == null) Labels.set(labelsFile, n, text) }
                oldLabels.delete()
                for (r in renamed) runCatching { Control.rename(pkg, r.from, r.to) }
                if (log.isNotEmpty()) AppLog.info(game, LOGGER, "mods-merge ${serverName(pkg)}的 Mods 已并入共享 Mods：" + log.toString().trim().replace("\n", "；"))
            } catch (e: Throwable) { AppLog.warning(game, LOGGER, "mods-merge-error ${serverName(pkg)}的 Mods 迁移失败：$e") }
        }
        File(activity.filesDir, "mods").let { if (it.list()?.isEmpty() == true) it.delete() }
    }
    /** The other installed games GIBox has run in (its media folder exists): a conversion is made for them too. */
    private fun otherGames(): List<String> = Control.GAMES.filter { it != game && packageInfo(it) != null && Control.shared(it).isDirectory }
    /**
     * The same mod converted for the other games, each with its own bone library and models folder; a new model there
     * is switched on unless another mod of that character already is. A failure there only shows in the summary.
     */
    private fun convertOthers(source: File, id: Int): List<String> = otherGames().map { pkg ->
        val log = StringBuilder()
        try {
            val r = Importer.convert(source, id, Control.models(pkg), Control.bones(pkg), decoder, log)
            AppLog.debug(pkg, "converter", "conversion-log\n" + log.toString().trim())
            val model = r.model.name
            AppLog.info(pkg, LOGGER, "converted $model：${r.summary.replace('\n', ' ')}（随${serverName(game)}的转换一并生成）")
            val taken = Importer.list(modsRoot, Control.models(pkg)).any { it.name != model && it.id == r.id && it.converted && Control.selected(pkg, it.name) }
            if (!taken && !Control.selected(pkg, model)) { Control.enable(pkg, model); AppLog.info(pkg, LOGGER, "mod-enable $model") }
            "${serverName(pkg)}：已转换" + if (taken) "（该角色已有开启的 MOD，此 MOD 未开启）" else ""
        } catch (e: Throwable) {
            AppLog.debug(pkg, "converter", "conversion-log\n" + log.toString().trim())
            AppLog.warning(pkg, LOGGER, "convert-error ${source.name}：${e.message ?: e}")
            AppLog.warning(game, LOGGER, "convert-other-error ${serverName(pkg)}：${e.message ?: e}")
            "${serverName(pkg)}：转换失败（${e.message ?: e}）"
        }
    }

    // ---------------------------------------------------------------- navigation
    fun open(screen: Screen) { stack.add(screen) }
    fun back() {
        if (stack.size <= 1) return
        val top = stack.removeAt(stack.lastIndex)
        if (top is Screen.Pick && top.pending != null) {
            Importer.discard(top.pending)
            AppLog.info(game, LOGGER, "import-cancel ${top.modName}")
        }
    }
    private fun closeTop() { if (stack.size > 1) stack.removeAt(stack.lastIndex) }

    // ---------------------------------------------------------------- state
    fun refresh() {
        storage = hasStorage()
        detectGame().let { if (it != game) useGame(it) }
        // The legacy entry marks GIBox's own process; a framework on the modern API instead hands over its service.
        moduleActive = ModuleStatus.active || LspService.scope != null
        frameworkApi = ModuleStatus.api
        mods = Importer.list(modsRoot, modelsRoot)
        labels = Labels.read(labelsFile)
        enabled = Control.enabled(game)
        logLevel = Control.logLevel(game)
        status = Control.status(game)
        val control = Control.load(game)
        boneRefreshRequest = control.optLong("refreshBones", 0)
        boneRefreshCharacter = control.optInt("refreshBonesCharacter", 0)
        installPcHashes()
        revision++
    }
    /** The PC hash database (assets/pc-hashes.json, THIRD_PARTY.md) and the device hash map shipped with the App
     *  (assets/device-hashes.json): the game reads both from the shared folder. */
    private fun installPcHashes() {
        if (pcHashesFor == game || !storage) return
        runCatching {
            for ((file, what) in listOf("pc-hashes.json" to "PC hash 数据库", "device-hashes.json" to "手机 hash 表", OfflineSkeletons.FILE to "离线骨骼表")) {
                val bytes = activity.assets.open(file).use { it.readBytes() }
                if (Control.installShared(game, file, bytes)) AppLog.info(game, LOGGER, "shared-installed $what（${bytes.size / 1024} KB）")
            }
            pcHashesFor = game
        }.onFailure { AppLog.warning(game, LOGGER, "shared-error hash 数据安装失败：${it.message}") }
    }
    fun refreshStatus() { status = Control.status(game) }
    fun acceptStatus(pkg: String, snapshot: Control.Status) { if (game == pkg) status = snapshot }
    fun refreshingBones(id: Int): Boolean = boneRefreshRequest > 0 && boneRefreshCharacter == id &&
        !(status.boneRefreshRequest == boneRefreshRequest && status.boneRefreshCharacter == id && status.boneRefreshState == "complete")
    fun boneRefreshMessage(id: Int): String? {
        if (boneRefreshRequest <= 0 || boneRefreshCharacter != id) return null
        if (status.boneRefreshRequest != boneRefreshRequest || status.boneRefreshCharacter != id) return "等待游戏响应，请在大世界切换到${Characters.nameOf(id)}。"
        return when (status.boneRefreshState) {
            "complete" -> "骨骼已重新采集，可重新转换对应 MOD。"
            "waiting-character" -> "请在大世界切换到${Characters.nameOf(id)}，会自动开始采集。"
            "saving" -> "骨骼已通过校验，正在保存…"
            "waiting-full-mesh" -> status.boneRefreshDetail.ifBlank { "当前只有低精度模型。请打开该角色详情或队伍预览并停留，完整模型出现后会自动采集；原骨骼库保留。" }
            else -> status.boneRefreshDetail.ifBlank { "正在重新采集并检查骨骼，未完整时会自动重试…" }
        }
    }

    fun mod(name: String): Importer.Mod? = mods.firstOrNull { it.name == name }
    /** The name shown for a mod: the one given to it, else its Mods entry name. */
    fun label(name: String): String = Labels.of(labels, name)

    /** One row of the home list: a character and its mods. */
    class Group(val id: Int, val title: String, val mods: List<Importer.Mod>)
    /** Mods by character, in character ID order; mods without a chosen character last. */
    fun groups(): List<Group> = mods.groupBy { it.id }.toSortedMap(compareBy { if (it == 0) Int.MAX_VALUE else it })
        .map { (id, list) -> Group(id, if (id == 0) "未选择角色" else Characters.nameOf(id), list.sortedBy { label(it.name).lowercase() }) }
    fun group(id: Int): Group? = groups().firstOrNull { it.id == id }

    fun rename(name: String, text: String) = save("改名", needsStorage = false) {
        Labels.set(labelsFile, name, text)
        AppLog.info(game, LOGGER, "rename $name → ${text.trim().ifEmpty { name }}")
    }
    fun selected(name: String): Boolean = Control.selected(game, name)

    /**
     * The game LSPosed injects GIBox into. The installed games, narrowed to the scope ticked in LSPosed when the modern
     * service tells it (LspService); one left: that one. More than one: the one chosen on the home card (game.txt), else
     * the one injected most recently (Entry leaves GIBox/injected.txt in the game's media folder each time it loads
     * there), else the official one.
     */
    private fun detectGame(): String {
        val all = listOf("com.miHoYo.Yuanshen", "com.miHoYo.ys.bilibili", "com.miHoYo.GenshinImpact")
        val installed = all.filter { packageInfo(it) != null }.ifEmpty { all }
        val scope = LspService.scope
        val candidates = (if (scope == null) installed else installed.filter { it in scope }).ifEmpty { installed }
        games = if (scope != null && candidates.size > 1) candidates else emptyList()
        if (candidates.size == 1) return candidates[0]
        readPrivate("game.txt").takeIf { it in candidates }?.let { return it }
        val marks = candidates.associateWith { File(Control.shared(it), "injected.txt") }.filterValues { it.isFile }
        return marks.maxByOrNull { it.value.lastModified() }?.key ?: candidates[0]
    }
    /** The home card's switch, when both games are ticked in LSPosed. */
    fun chooseGame(pkg: String) {
        if (pkg !in games || pkg == game) return
        writePrivate("game.txt", pkg)
        useGame(pkg)
        refresh()
    }
    private fun useGame(pkg: String) {
        game = pkg
        AppLog.info(pkg, LOGGER, "game ${serverName(pkg)}（$pkg，按 LSPosed 注入自动选择）")
        runCatching { Control.retireExperiments(pkg) }
        Importer.discardAll(modsRoot)
    }
    fun setReplacement(on: Boolean) = save("保存") {
        Control.setEnabled(game, on)
        AppLog.info(game, "app", if (on) "model-replacement 开启" else "model-replacement 关闭")
    }
    /** Switching on a second mod of a character asks first (both, only this one, or not). */
    fun setSelected(mod: Importer.Mod, on: Boolean) {
        if (!on) { save("保存") { Control.disable(game, mod.name); AppLog.info(game, LOGGER, "mod-disable ${mod.name}") }; return }
        val others = enabledWith(mod.name, mod.id)
        if (others.isEmpty()) { save("保存") { enable(mod.name, emptyList()) }; return }
        prompt = bothPrompt("同时开启多个 MOD？", "", mod.name, mod.id, others, "取消")
    }
    /** Other converted, enabled mods of the same character. */
    private fun enabledWith(name: String, id: Int): List<String> =
        if (id == 0) emptyList() else mods.filter { it.name != name && it.id == id && it.converted && Control.selected(game, it.name) }.map { it.name }
    private fun enable(name: String, off: List<String>) {
        for (o in off) Control.disable(game, o)
        Control.enable(game, name)
        AppLog.info(game, LOGGER, "mod-enable $name" + if (off.isEmpty()) "" else "，同时停用 " + off.joinToString("、"))
    }
    /** "Both / only this one / dismiss" for a mod whose character already has enabled mods. */
    private fun bothPrompt(title: String, lead: String, name: String, id: Int, others: List<String>, dismiss: String) = Prompt(
        title = title,
        text = lead + "${Characters.nameOf(id)} 已经开启了：${others.joinToString("、") { label(it) }}\n" +
            "同时开启时，两个 MOD 改到同一个部件的，以先开启的为准；各改各的部件就都会显示。",
        confirm = "都开启", onConfirm = { save("保存") { enable(name, emptyList()) } },
        neutral = "只开这个", onNeutral = { save("保存") { enable(name, others) } },
        dismiss = dismiss,
    )
    fun setVariable(mod: Importer.Mod, variable: String, value: Int) = save("保存") { Control.setVariable(game, mod.name, variable, value) }
    /** A [Key] of the mod's INI logic, pressed in the game by the logic core (experiments.logicCore). */
    fun pressKey(mod: Importer.Mod, key: ModelManifest.Key, back: Boolean = false) = save("按键") {
        Control.pressKey(game, mod.name, key.file, key.section, back)
        AppLog.info(game, LOGGER, "key-press ${mod.name} ${key.section}" + if (back) "（上一个）" else "")
    }
    /** A cycle key's preset chosen directly (0-based), activated in the game as if stepped to. */
    fun selectKey(mod: Importer.Mod, key: ModelManifest.Key, index: Int) = save("按键") {
        Control.keyInput(game, mod.name, key.file, key.section, false, index, null)
        AppLog.info(game, LOGGER, "key-select ${mod.name} ${key.section}：第 ${index + 1} 个")
    }
    /** A hold key held (down) or released (up). */
    fun holdKey(mod: Importer.Mod, key: ModelManifest.Key, down: Boolean) = save("按键") {
        Control.keyInput(game, mod.name, key.file, key.section, false, -1, if (down) "down" else "up")
        AppLog.info(game, LOGGER, "key-hold ${mod.name} ${key.section}：" + if (down) "按住" else "松开")
    }
    /** Settings → 通用: the UID in the game's UI hidden (takes effect at the next game start). */
    fun hideUid(): Boolean = Control.hideUid(game)
    fun setHideUid(on: Boolean) = save("保存") {
        Control.setHideUid(game, on)
        AppLog.info(game, LOGGER, "hide-uid " + if (on) "开启（重启游戏后生效）" else "关闭")
    }
    /** Replaced characters kept on the full model in the world (the game's LOD held at level 0). */
    fun lodPin(): Boolean = Control.lodPin(game)
    fun setLodPin(on: Boolean) = save("保存") {
        Control.setLodPin(game, on)
        AppLog.info(game, LOGGER, "lod-pin " + if (on) "开启" else "关闭")
    }
    /** Material experiments (debug): read back on every recomposition through `revision`. */
    fun experiment(key: String): Boolean = Control.experiment(game, key)
    fun experimentInt(key: String): Int = Control.experimentInt(game, key)
    fun setExperiment(key: String, value: Any) = save("保存实验设置") {
        Control.setExperiment(game, key, value)
        AppLog.info(game, LOGGER, "experiment $key = $value")
    }
    /** Texture-only mods: strategy and LOD option (control.json "textureMods"), and a run of the test rounds. */
    fun textureMods(key: String, fallback: String): String = Control.textureMods(game, key, fallback)
    fun setTextureMods(key: String, value: String) = save("保存纯贴图设置") {
        Control.setTextureMods(game, key, value)
        AppLog.info(game, LOGGER, "texture-option $key = $value")
    }
    fun textureModsFlag(key: String): Boolean = Control.textureModsFlag(game, key)
    fun setTextureModsFlag(key: String, value: Boolean) = save("保存纯贴图设置") {
        Control.setTextureMods(game, key, value)
        AppLog.info(game, LOGGER, "texture-option $key = $value")
    }
    fun hashMode(): String = Control.hashMode(game)
    fun setHashMode(mode: String) = save("保存 hash 模式") {
        Control.setHashMode(game, mode)
        AppLog.info(game, LOGGER, "hash-mode $mode（重启游戏后生效）")
    }
    fun startTextureTest() = save("开始轮测") {
        val n = Control.requestTextureTest(game, 4000)
        AppLog.info(game, LOGGER, "texture-cycle #$n：自动 → 直接改材质 → 克隆材质，各观察 4 秒")
        toast("已开始轮测 #$n，结果见日志 texture-test")
    }
    /** 显示替换 开/关 for a model with its own on/off switch (model.json "replaceWhen"). */
    fun setReplacing(mod: Importer.Mod, variants: Variants, on: Boolean) = save("保存") {
        val change = variants.switchTo(on, variants.current(game, mod.name))
        if (change.isNotEmpty()) Control.setVariables(game, mod.name, change)
    }
    fun changeLogLevel(level: String) = save("保存") { Control.setLogLevel(game, level) }
    fun exportBones() = save("保存") {
        Control.requestBoneExport(game)
        toast("已通知游戏导出骨骼，校验未通过会自动重试")
    }
    fun confirmBoneRefresh(id: Int) {
        if (id !in 10000000..10001999 || refreshingBones(id)) return
        if (!storage) { needStorage(); return }
        val pkg = game
        val other = if (boneRefreshCharacter != id && refreshingBones(boneRefreshCharacter))
            "当前还有${Characters.nameOf(boneRefreshCharacter)}的采集请求，确定后将改为采集本页角色。\n\n" else ""
        prompt = Prompt(
            title = "重新采集骨骼？",
            text = other + "将重新获取${Characters.nameOf(id)}的骨骼。请先在大世界切换到该角色；若提示只有低精度模型，再打开该角色详情或队伍预览并停留。完整模型校验通过后保存，等待期间保留原骨骼库。\n\n采集完成后，请重新转换需要使用新骨骼的 MOD。",
            confirm = "确定", dismiss = "取消",
            onConfirm = { save("重新采集骨骼") {
                Control.requestBoneRefresh(pkg, id)
                AppLog.info(pkg, LOGGER, "bone-recapture ${Characters.nameOf(id)}（$id）：已请求重新采集骨骼")
                toast("已提交，请在大世界切换到${Characters.nameOf(id)}")
            } },
        )
    }
    /** A change to the game's files (control.json, the log): without file access, the reminder instead. */
    private fun save(what: String, needsStorage: Boolean = true, change: () -> Unit) {
        if (needsStorage && !hasStorage()) { needStorage(); return }
        try { change() } catch (e: Throwable) { fail(what, e, null) }
        refresh()
    }

    // ---------------------------------------------------------------- import
    /** A .zip the system file picker returned. */
    fun importFrom(uri: Uri) {
        if (!storage) { needStorage(); return }
        val source = SafSource(activity.contentResolver, uri, activity.cacheDir)
        work("导入 MOD", "正在解压…", { log ->
            val pending = Importer.prepare(source, modsRoot, log)
            AppLog.info(game, LOGGER, "read ${source.name()}：${log.toString().trim()}")
            Pair(pending, source.name())
        }) { (pending, picked) ->
            val name = Importer.suggestName(picked)
            val before = mods.firstOrNull { it.name == name }?.id ?: 0
            open(Screen.Pick(name, picked, pending, null, before))
        }
    }
    /** Model INIs of a package with several schemes (relative paths); fewer than two: nothing to choose. */
    fun schemes(dir: File?): List<String> = dir?.let { Importer.schemes(it) } ?: emptyList()
    fun scheme(dir: File?): String = dir?.let { Importer.scheme(it) } ?: ""
    fun animationBoneFallback(dir: File?): Boolean = dir?.let { Importer.animationBoneFallback(it) } ?: false
    fun setAnimationBoneFallback(mod: Importer.Mod, enabled: Boolean) = save("保存动画转换实验", needsStorage = false) {
        mod.source?.let { Importer.setAnimationBoneFallback(it, enabled) }
        AppLog.info(game, LOGGER, "animation-compat ${mod.name}：" + (if (enabled) "开" else "关") + "（下次转换生效）")
        toast(if (enabled) "已开启动画骨骼兼容，请重新转换此 MOD" else "已恢复严格检查，请重新转换此 MOD")
    }
    /** Another INI scheme for an imported mod: stored, and converted again if it already was. */
    fun chooseScheme(mod: Importer.Mod, rel: String) {
        val source = mod.source ?: return
        if (rel == scheme(source)) return
        try { Importer.setScheme(source, rel); AppLog.info(game, LOGGER, "ini-scheme ${mod.name} → $rel") }
        catch (e: Throwable) { fail("选择 INI 方案", e, null); return }
        refresh()
        if (mod.converted && (mod.id in 10000000..10001999 || Characters.isScene(mod.id))) convert(source, mod.id)
    }
    /** Confirmed on the character page; `scheme` is the INI chosen there for a package with several. */
    fun confirm(pick: Screen.Pick, avatar: Characters.Avatar, scheme: String? = null, outfit: String? = null) {
        val dir = pick.pending ?: pick.source
        if (scheme != null && dir != null) {
            try { Importer.setScheme(dir, scheme) } catch (e: Throwable) { fail("选择 INI 方案", e, null); return }
        }
        // The outfit chosen with the character (a character with skins): kept for re-conversions too.
        if (dir != null) {
            try { Importer.setOutfit(dir, if (Outfits.current().of(avatar.id).isEmpty()) null else outfit) } catch (e: Throwable) { fail("选择外观", e, null); return }
        }
        val pending = pick.pending
        if (pending == null) {
            closeTop()
            pick.source?.let { convert(it, avatar.id) }
            return
        }
        val name = pick.modName
        if (File(modsRoot, name).exists() || File(modelsRoot, name).exists()) {
            val other = Importer.freeName(modsRoot, modelsRoot, name)
            prompt = Prompt(
                title = "已有同名 MOD",
                text = "「$name」已经导入过。\n覆盖：换成这次选的文件\n另存：作为「$other」导入",
                confirm = "覆盖", onConfirm = { commit(pick, pending, name, avatar.id) },
                neutral = "另存", onNeutral = { commit(pick, pending, other, avatar.id) },
                dismiss = "取消",
            )
            return
        }
        commit(pick, pending, name, avatar.id)
    }
    private fun commit(pick: Screen.Pick, pending: File, name: String, id: Int) {
        if (stack.lastOrNull() == pick) closeTop()
        AppLog.info(game, LOGGER, "import ${pick.importedAs} → Mods/$name，角色 ${Characters.nameOf(id)}（$id）")
        convertJob(name) { log -> Importer.convert(Importer.commit(pending, modsRoot, name), id, modelsRoot, boneRoot, decoder, log) }
    }
    /** Re-conversion of an imported mod, for `id`. */
    fun convert(source: File, id: Int) {
        AppLog.info(game, LOGGER, "convert ${source.name} → ${Characters.nameOf(id)}（$id）")
        convertJob(source.name) { log -> Importer.convert(source, id, modelsRoot, boneRoot, decoder, log) }
    }
    private fun convertJob(name: String, job: (StringBuilder) -> Importer.Result) {
        work("转换", "正在转换 $name…\n大的 MOD 可能要一分钟。", { log ->
            val r = try { job(log).also { AppLog.debug(game, "converter", "conversion-log\n" + log.toString().trim()) } }
            catch (e: Throwable) { AppLog.debug(game, "converter", "conversion-log\n" + log.toString().trim()); throw e }
            Pair(r, convertOthers(File(modsRoot, name), r.id))
        }) { (r, others) ->
            val model = r.model.name
            AppLog.info(game, LOGGER, "converted $model：${r.summary.replace('\n', ' ')}")
            val summary = if (others.isEmpty()) r.summary else r.summary + "\n\n" + others.joinToString("\n")
            refresh()
            val enabled = enabledWith(model, r.id)
            if (enabled.isEmpty()) {
                try { enable(model, emptyList()) } catch (e: Throwable) { AppLog.warning(game, LOGGER, "enable-error 启用失败：$e") }
                refresh()
                prompt = Prompt("导入完成", "${label(model)}\n$summary\n\n已启用。游戏运行中也会自动载入。")
            } else {
                prompt = bothPrompt("导入完成", "${label(model)}\n$summary\n\n", model, r.id, enabled, "先不开启")
            }
        }
    }
    /** Debug: every converted mod converted again from its Mods folder, for the character it has (as 重新转换 on its
     *  page), one after another; which mods are on is not changed. A failure is listed and the rest go on. */
    fun reconvertAll() {
        val list = mods.filter { it.source != null && it.converted && it.id != 0 }
        if (list.isEmpty()) { prompt = Prompt("一键重新转换", "没有可重新转换的 MOD。"); return }
        prompt = Prompt(
            title = "一键重新转换", text = "按各自的角色重新转换 ${list.size} 个 MOD，可能要几分钟。", confirm = "开始", dismiss = "取消",
            onConfirm = {
                AppLog.info(game, LOGGER, "reconvert-all ${list.size} 个 MOD")
                work("重新转换", "正在重新转换 ${list.size} 个 MOD…", { _ ->
                    val failed = ArrayList<String>()
                    list.forEachIndexed { i, mod ->
                        val name = label(mod.name)
                        activity.runOnUiThread { busy = Busy("重新转换", "${i + 1} / ${list.size}：$name") }
                        val log = StringBuilder()
                        try { Importer.convert(mod.source, mod.id, modelsRoot, boneRoot, decoder, log); AppLog.debug(game, "converter", "conversion-log\n" + log.toString().trim()) }
                        catch (e: Throwable) { AppLog.debug(game, "converter", "conversion-log\n" + log.toString().trim()); AppLog.warning(game, LOGGER, "reconvert-error ${mod.name}：$e"); failed.add("$name：${e.message ?: e}"); return@forEachIndexed }
                        for (note in convertOthers(mod.source, mod.id)) if (note.contains("失败")) failed.add("$name（$note）")
                    }
                    failed
                }) { failed ->
                    refresh()
                    AppLog.info(game, LOGGER, "reconvert-done 成功 ${list.size - failed.size}，失败 ${failed.size}")
                    prompt = Prompt("重新转换完成", "成功 ${list.size - failed.size} 个" + if (failed.isEmpty()) "。" else "，失败 ${failed.size} 个：\n" + failed.joinToString("\n"))
                }
            },
        )
    }
    fun guess(pick: Screen.Pick): Characters.Guess =
        Importer.guess(pick.pending ?: pick.source ?: File(modsRoot, pick.modName), pick.importedAs, boneRoot)
    /** Characters with a skeleton: in the table made from the game's files, or recorded by the game on this device. */
    fun recorded(): Set<Int> = ModLibrary.library(boneRoot).map { it.id }.toSet() + (OfflineSkeletons.load(boneRoot.absoluteFile.parentFile)?.characters() ?: emptySet())
    /** The outfits of a character (the original first); empty for a character without skins. */
    fun outfits(id: Int): List<Outfits.Outfit> = Outfits.current().of(id)
    /** The outfits of a character that have a bone library (prefabs). */
    fun recordedOutfits(id: Int): Set<String> {
        val table = Outfits.current(); val offline = OfflineSkeletons.load(boneRoot.absoluteFile.parentFile)
        val fromTable = table.of(id).filter { offline?.prefabFor(id, it.prefab) != null }.map { it.prefab }
        return ModLibrary.recordedOutfits(boneRoot, id, table) + fromTable
    }
    /** The outfit the picker suggests: the one the mod's INI hashes name, else the one chosen before, else the original. */
    fun suggestedOutfit(pick: Screen.Pick, id: Int): String {
        val list = outfits(id); if (list.isEmpty()) return ""
        val dir = pick.pending ?: pick.source ?: File(modsRoot, pick.modName)
        Importer.guessOutfit(dir, id, boneRoot).outfit?.let { return it.prefab }
        val before = Importer.outfit(dir)
        return list.firstOrNull { it.prefab == before }?.prefab ?: list[0].prefab
    }
    /** What the mod's INI hashes say about the outfit, for the picker ("" when they say nothing). */
    fun outfitGuessText(pick: Screen.Pick, id: Int): String {
        val dir = pick.pending ?: pick.source ?: File(modsRoot, pick.modName)
        val g = Importer.guessOutfit(dir, id, boneRoot)
        return when {
            g.outfit != null -> "MOD 的 INI 哈希认出是「${g.outfit.name}」：选别的外观会被拒绝（骨骼不同会错位）"
            g.conflict.isNotEmpty() -> "MOD 的 INI 哈希指向多套外观（${g.conflict.joinToString("、")}），请自己选"
            else -> "没从 MOD 的哈希认出外观，请自己选"
        }
    }
    /** "冷花幽露" for a skin's prefab, "原皮" for the original, "" for none. */
    fun outfitLabel(prefab: String): String = Outfits.current().label(prefab)

    fun delete(mod: Importer.Mod) {
        prompt = Prompt(
            title = "删除 ${label(mod.name)}", text = "删除 Mods 里的这份 MOD 和转换好的模型（各服务器的都删）？导入时选的原文件不受影响。",
            confirm = "删除", dismiss = "取消", destructive = true,
            onConfirm = {
                try {
                    Control.disable(game, mod.name); Importer.delete(mod, modelsRoot); Labels.set(labelsFile, mod.name, null); AppLog.info(game, LOGGER, "mod-delete ${mod.name}")
                    // The shared Mods copy is gone: the other games' models of it go too.
                    for (pkg in otherGames()) runCatching { Control.disable(pkg, mod.name); Importer.deleteModel(mod.name, Control.models(pkg)); AppLog.info(pkg, LOGGER, "mod-delete ${mod.name}（在${serverName(game)}删除）") }
                }
                catch (e: Throwable) { fail("删除", e, null) }
                if (stack.lastOrNull() == Screen.Detail(mod.name)) closeTop()
                refresh()
            },
        )
    }

    /** The old manual conversion (settings → 高级): explicit bone folder, ID, name and mapping. */
    fun manualConvert(src: String, bones: String, id: Int, name: String, mapping: String, variants: Boolean) {
        val dest = File(modelsRoot, name)
        if (dest.exists()) { prompt = Prompt("错误", "输出目录已经存在，请换一个模型名：$dest"); return }
        work("手动转换", "正在转换…", { log -> ModelConverter.convert(File(src), File(bones), dest, id, mapping, variants, decoder, log) }) { out ->
            AppLog.info(game, "converter", "manual-convert-done $out")
            refresh()
            prompt = Prompt("转换结束", out.joinToString("\n"))
        }
    }

    // ---------------------------------------------------------------- system
    fun launch() {
        val intent = activity.packageManager.getLaunchIntentForPackage(game)
        if (intent == null) { prompt = Prompt("错误", "没有找到游戏：$game\n（没有安装，或者在设置里选错了服务器）"); return }
        AppLog.info(game, "app", "launch $game")
        activity.startActivity(intent)
    }
    fun grantStorage() {
        try {
            if (Build.VERSION.SDK_INT >= 30) activity.startActivity(Intent("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION", Uri.parse("package:" + activity.packageName)))
            else activity.requestPermissions(arrayOf("android.permission.READ_EXTERNAL_STORAGE", "android.permission.WRITE_EXTERNAL_STORAGE"), 1)
        } catch (e: Throwable) { fail("打开权限设置", e, null) }
    }
    fun needStorage() {
        prompt = Prompt("需要文件访问权限", "转换好的模型要放进原神的目录（Android/media），游戏才读得到，需要“所有文件访问权限”。", confirm = "去授予", onConfirm = { grantStorage() }, dismiss = "取消")
    }
    private fun hasStorage(): Boolean =
        if (Build.VERSION.SDK_INT >= 30) Environment.isExternalStorageManager()
        else activity.checkSelfPermission("android.permission.READ_EXTERNAL_STORAGE") == PackageManager.PERMISSION_GRANTED
    fun copy(label: String, text: String) {
        val clipboard = activity.getSystemService(ClipboardManager::class.java) ?: return
        clipboard.setPrimaryClip(ClipData.newPlainText(label, text))
        toast("已复制：$label")
    }
    fun toast(text: String) { Toast.makeText(activity, text, Toast.LENGTH_SHORT).show() }
    private fun packageInfo(pkg: String): android.content.pm.PackageInfo? = try {
        @Suppress("DEPRECATION")
        if (Build.VERSION.SDK_INT >= 33) activity.packageManager.getPackageInfo(pkg, PackageManager.PackageInfoFlags.of(0))
        else activity.packageManager.getPackageInfo(pkg, 0)
    } catch (_: PackageManager.NameNotFoundException) { null }
    /**
     * One system chooser for several ways of handing over the same thing (the log's share button and the folders):
     * the first intent and its alternates, each app listed once. Android 10+ merges them itself (EXTRA_ALTERNATE_INTENTS);
     * before that the apps of the alternates that do not take the first are added by hand.
     */
    private fun choose(title: String, first: Intent, alternates: List<Intent>) {
        val chooser = Intent.createChooser(first, title)
        if (alternates.isNotEmpty()) {
            if (Build.VERSION.SDK_INT >= 29) chooser.putExtra(Intent.EXTRA_ALTERNATE_INTENTS, alternates.toTypedArray())
            else {
                val pm = activity.packageManager
                val seen = pm.queryIntentActivities(first, 0).map { it.activityInfo.packageName }.toMutableSet()
                val extra = alternates.flatMap { alt ->
                    pm.queryIntentActivities(alt, 0).filter { seen.add(it.activityInfo.packageName) }.map {
                        LabeledIntent(Intent(alt).setClassName(it.activityInfo.packageName, it.activityInfo.name), it.activityInfo.packageName, it.loadLabel(pm), it.iconResource)
                    }
                }
                if (extra.isNotEmpty()) chooser.putExtra(Intent.EXTRA_INITIAL_INTENTS, extra.toTypedArray())
            }
        }
        activity.startActivity(chooser)
    }
    /**
     * Like the log's share button: a chooser of the apps that open a folder (VIEW with either folder MIME type file
     * managers register). Folders on shared storage go as the system's own document URI
     * (com.android.externalstorage.documents); the private Mods folder, and the shared ones again for managers that do
     * not take that URI, through GIBox's DocumentsProvider with a tree grant.
     */
    fun openFolder(folder: File) {
        if (folder != modsRoot && !storage) { needStorage(); return }
        try {
            check(folder.isDirectory || folder.mkdirs()) { "无法创建目录：$folder" }
            val types = listOf(DocumentsContract.Document.MIME_TYPE_DIR, "resource/folder")
            val views = mutableListOf<Intent>()
            DocumentPaths.externalDocumentId(folder)?.let { external ->
                val uri = DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", external)
                for (mime in types) views += Intent(Intent.ACTION_VIEW).setDataAndType(uri, mime)
            }
            val id = when (folder) {
                modsRoot -> "$game:mods"
                modelsRoot -> "$game:shared/models"
                boneRoot -> "$game:shared/skeletons"
                else -> null
            }
            if (id != null) {
                val tree = DocumentsContract.buildTreeDocumentUri(DirectoryProvider.AUTHORITY, id)
                val document = DocumentsContract.buildDocumentUriUsingTree(tree, id)
                for (mime in types) views += Intent(Intent.ACTION_VIEW).apply {
                    setDataAndType(document, mime)
                    clipData = ClipData.newRawUri("GIBox 目录", tree)
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION or Intent.FLAG_GRANT_PREFIX_URI_PERMISSION)
                }
            }
            check(views.isNotEmpty()) { "不支持的目录：$folder" }
            choose("打开文件夹", views.first(), views.drop(1))
        } catch (e: Throwable) { fail("打开文件夹", e, null) }
    }
    fun shareLog() {
        if (!logFile.isFile) { toast("暂无日志文件"); return }
        try {
            val uri = DocumentsContract.buildDocumentUri(DirectoryProvider.AUTHORITY, "$game:shared/${logFile.name}")
            // "Open with" (VIEW) is what text editors and file managers such as MT 管理器 register for;
            // SEND adds the chat and cloud apps. Both go in one chooser, each app listed once.
            fun grant(i: Intent) = i.apply { clipData = ClipData.newRawUri("运行日志", uri); addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION) }
            val view = grant(Intent(Intent.ACTION_VIEW).setDataAndType(uri, "text/plain"))
            val send = grant(Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_STREAM, uri))
            choose("打开运行日志", view, listOf(send))
        } catch (e: Throwable) { fail("分享日志", e, null) }
    }
    /** The hash comparison report the game writes in capture mode (GIBox/hash-report.txt). */
    fun shareHashReport() {
        val file = File(Control.shared(game), "hash-report.txt")
        if (!file.isFile) { toast("还没有报告：把 hash 模式改为“采集”，重启游戏，在图鉴里点一下角色，或在大世界靠近 NPC、怪物"); return }
        try {
            val uri = DocumentsContract.buildDocumentUri(DirectoryProvider.AUTHORITY, "$game:shared/${file.name}")
            fun grant(i: Intent) = i.apply { clipData = ClipData.newRawUri("hash 对比报告", uri); addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION) }
            choose("打开 hash 对比报告", grant(Intent(Intent.ACTION_VIEW).setDataAndType(uri, "text/plain")), listOf(grant(Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_STREAM, uri))))
        } catch (e: Throwable) { fail("打开报告", e, null) }
    }
    fun readLog(): String = AppLog.tail(logFile, 128 * 1024)
    fun clearLog() {
        File(logFile.path + ".1").delete()
        logFile.delete()
    }

    // ---------------------------------------------------------------- background work, errors
    private val decoder = ModelConverter.TextureDecoder { NativeBridge.decodeTexture(it) }
    private fun <T> work(title: String, text: String, job: (StringBuilder) -> T, done: (T) -> Unit) {
        busy = Busy(title, text)
        thread(name = "gibox-work") {
            val log = StringBuilder()
            val result = runCatching { job(log) }
            activity.runOnUiThread {
                busy = null
                if (activity.isFinishing) return@runOnUiThread
                result.fold(onSuccess = { done(it) }, onFailure = { fail(title, it, log) })
            }
        }
    }
    fun fail(what: String, e: Throwable, log: StringBuilder?) {
        AppLog.error(game, LOGGER, "action-error ${what}失败：$e")
        val details = if (log != null && log.isNotEmpty()) "\n\n" + log.toString().trim() else ""
        prompt = Prompt("错误", "${what}失败：${e.message ?: e.toString()}$details\n\n日志文件：$logFile")
    }
    private fun readPrivate(name: String): String = try { File(activity.filesDir, name).readText().trim() } catch (e: Exception) { "" }
    private fun writePrivate(name: String, value: String) { try { File(activity.filesDir, name).writeText(value) } catch (e: Exception) { } }
}

/** "茜特菈莉 · 10000107" ("申鹤 · 冷花幽露 · 10000063" for a skin's mod), or what is missing. */
fun who(mod: Importer.Mod): String = when {
    mod.id != 0 -> Characters.nameOf(mod.id) + (if (mod.outfit.isEmpty()) "" else " · " + Outfits.current().label(mod.outfit)) + " · " + mod.id
    mod.converted -> "按名字识别"
    else -> "未选择角色"
}
fun serverName(pkg: String): String = when (pkg) { "com.miHoYo.Yuanshen" -> "官服"; "com.miHoYo.GenshinImpact" -> "国际服"; else -> "B 服" }
