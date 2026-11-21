package com.gibox.tool

import android.content.Context
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.content.pm.LabeledIntent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.DocumentsContract
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.lifecycleScope
import java.io.File
import java.io.Serializable
import java.lang.ref.WeakReference
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject

const val BUILD_ID = "6d85e3141bcd6b508f3e853bab508cc407278eac"
private const val LOGGER = "importer"

/** Small, restorable arguments for a page in the Android task. */
sealed interface Screen : Serializable {
    data object Home : Screen
    data object Mods : Screen
    data object Accessibility : Screen
    data object AboutSettings : Screen
    data object About : Screen
    data object FeatureSearch : Screen
    data object Settings : Screen
    data object Debug : Screen
    data object Log : Screen
    data object Manual : Screen
    /** The mods of one character (id 0: mods without a chosen character). */
    data class Character(val id: Int) : Screen
    data class Detail(val name: String) : Screen
    /**
     * Character choice. For a mod being imported, `pending` is its copy in Mods/.pending (dropped when the page is left
     * without 确定); for an imported mod, `source` is its folder in Mods.
     */
    data class Pick(
        val modName: String, val importedAs: String, val pending: File?, val source: File?,
        val current: Int, val recommendedIds: List<Int>,
    ) : Screen
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
    val emphasizeNeutral: Boolean = false,
)

/** Everything the pages show and do. File work runs on a background thread; state is changed on the UI thread only. */
class AppState private constructor(private val context: Context) {
    companion object {
        val SUPPORTED_GAMES = listOf("com.miHoYo.Yuanshen", "com.miHoYo.ys.bilibili", "com.miHoYo.GenshinImpact")
        private var shared: AppState? = null

        // Activity creation and all page navigation run on the main thread.
        internal fun get(context: Context): AppState =
            shared ?: AppState(context.applicationContext).also { shared = it }
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var host = WeakReference<PageActivity>(null)
    private var startedPages = 0
    private var checkedStorage = false

    /** The games GIBox may work for (more than one: both ticked in LSPosed and installed; the home card switches). */
    var games by mutableStateOf<List<String>>(emptyList())
        private set
    private var availableClients by mutableStateOf<List<String>>(emptyList())
    /** The game package GIBox works for, picked by detectGame (no setting). */
    var game by mutableStateOf(detectGame())
        private set
    var libraryReady by mutableStateOf(false)
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
    val moduleActive: Boolean get() = ModuleStatus.active || LspService.scope != null
    val frameworkApi: Int get() = ModuleStatus.api
    var logLevel by mutableStateOf("info")
        private set
    internal val aboutCard by lazy { AboutCardSettings(context) }
    /** This APK's versionName (app/build.gradle.kts), read from the package rather than written in the source. */
    val appVersion: String = packageInfo(context.packageName)?.versionName ?: "?"
    var busy by mutableStateOf<String?>(null)
        private set
    var prompt by mutableStateOf<Prompt?>(null)
    var notice by mutableStateOf<String?>(null)
    /** Mods entry whose name is being edited (RenameDialog). */
    var renaming by mutableStateOf<String?>(null)
    /** Names given to mods (Labels), by Mods entry name. */
    var labels by mutableStateOf<Map<String, String>>(emptyMap())
        private set
    /** Bumped when control.json changes (variants, selection), so pages re-read it. */
    var revision by mutableStateOf(0)
        private set
    private var selectedMods by mutableStateOf<Set<String>>(emptySet())
    private var refreshJob: Job? = null
    private var currentControl = JSONObject()
    private class PendingVariant(val value: Int)
    private val pendingVariants = mutableStateMapOf<Triple<String, String, String>, PendingVariant>()
    private val preferenceWrites = Mutex()

    /** One imported source and converted payload, distributed to each game's readable directory; live records stay local. */
    val modsRoot: File get() = Control.mods(context.filesDir)
    val modelsRoot: File get() = Control.models(game)
    val boneRoot: File get() = Control.bones(game)
    val logFile: File get() = AppLog.file(game)
    private val labelsFile: File get() = File(context.filesDir, "labels/library.json")

    /** The game the PC hash database was installed for this run (installPcHashes, called from refresh). */
    @Volatile private var pcHashesFor = ""
    @Volatile private var sharedModels: SharedMods? = null

    private val initialGame = game
    private val startup = scope.async(Dispatchers.IO) {
        AppLog.debug(initialGame, "app", "app-start 版本 $appVersion")
        runCatching { Outfits.use(Outfits.parse(context.assets.open("outfits.json").use { it.readBytes() }.toString(Charsets.UTF_8))) }
            .onFailure { AppLog.warning(initialGame, LOGGER, "outfits-error 外观对照表读取失败：${it.message}") }
        runCatching { Control.retireExperiments(initialGame) }
        mergeMods(initialGame)
    }

    init {
        val card = aboutCard
        if (card.config.characters.isNotEmpty() && card.config.transparency < 100) {
            scope.launch(Dispatchers.IO) { card.stickerAtlas }
        }
        val foreground = card.config.foreground
        if (foreground != NO_IMAGE && foreground != WORDMARK) {
            scope.launch(Dispatchers.IO) { card.foreground(foreground) }
        }
        scope.launch {
            snapshotFlow { LspService.scope }.drop(1).collect { if (startedPages > 0) refreshInBackground() }
        }
    }

    internal fun pageStarted() {
        startedPages++
        if (startedPages == 1) refreshInBackground()
    }

    internal fun pageStopped() { startedPages-- }

    internal fun attach(activity: PageActivity) {
        host = WeakReference(activity)
        if (checkedStorage) return
        checkedStorage = true
        // Ask once on first run. File-access features can ask again when needed.
        if (!hasStorage() && readPrivate("storage-asked.txt").isEmpty()) {
            writePrivate("storage-asked.txt", "1")
            grantStorage()
        }
    }

    internal fun detach(activity: PageActivity) {
        if (host.get() === activity) host.clear()
    }

    /** Before 0.7.101 each game had its own Mods folder and labels: merged into the shared ones, this game's first. */
    private fun mergeMods(preferred: String) {
        for (pkg in listOf(preferred) + Control.GAMES.filter { it != preferred }) {
            val legacy = Control.legacyMods(context.filesDir, pkg)
            val oldLabels = File(context.filesDir, "labels/$pkg.json")
            if (!legacy.isDirectory && !oldLabels.isFile) continue
            try {
                val log = StringBuilder()
                val renamed = Importer.mergeMods(modsRoot, legacy, Control.models(pkg), log)
                val names = renamed.associate { it.from to it.to }
                for ((name, text) in Labels.read(oldLabels)) { val n = names[name] ?: name; if (Labels.read(labelsFile)[n] == null) Labels.set(labelsFile, n, text) }
                oldLabels.delete()
                for (r in renamed) runCatching { Control.rename(pkg, r.from, r.to) }
                if (log.isNotEmpty()) AppLog.info(preferred, LOGGER, "mods-merge ${serverName(pkg)}的 Mods 已并入共享 Mods：" + log.toString().trim().replace("\n", "；"))
            } catch (e: Throwable) { AppLog.warning(preferred, LOGGER, "mods-merge-error ${serverName(pkg)}的 Mods 迁移失败：$e") }
        }
        File(context.filesDir, "mods").let { if (it.list()?.isEmpty() == true) it.delete() }
    }
    private fun configureShared(pkg: String = game): SharedMods {
        val packages = Control.GAMES.filter { packageInfo(it) != null || Control.shared(it).isDirectory }.ifEmpty { listOf(pkg) }
        return Control.configureShared(context.filesDir, packages, pkg).also { sharedModels = it }
    }
    private fun convertShared(job: () -> Importer.Result): Pair<Importer.Result, List<String>> {
        val shared = sharedModels ?: configureShared()
        return shared.convert { val result = job(); Pair(result, shared.publish(result.model)) }
    }

    // ---------------------------------------------------------------- navigation
    fun open(screen: Screen) { host.get()?.openPage(screen) }

    internal fun openFeature(feature: Feature) {
        val activity = host.get() ?: return
        if (activity.isFinishing) return
        val replace = activity.page == Screen.FeatureSearch
        if (!replace && activity.page != Screen.Home) return
        if (feature.page != Screen.Home) activity.openPage(feature.page, feature.title, replace = replace)
        else if (replace) activity.finish()
    }

    fun back() { host.get()?.finish() }

    internal fun discardPending(pick: Screen.Pick) {
        val pending = pick.pending ?: return
        val pkg = game
        val abandoned = File(pending.parentFile, ".discard-${System.nanoTime()}")
        // Release the name before another import can stage a folder with the same name.
        val moved = pending.renameTo(abandoned)
        if (!moved) Importer.discard(pending)
        scope.launch(Dispatchers.IO) {
            if (moved) Importer.discard(abandoned)
            AppLog.info(pkg, LOGGER, "import-cancel ${pick.modName}")
        }
    }

    // ---------------------------------------------------------------- state
    fun refresh() {
        refreshJob?.cancel()
        val pkg = prepareRefresh()
        applyRefresh(readLibrary(pkg, storage))
    }

    fun refreshInBackground() {
        refreshJob?.cancel()
        val pkg = prepareRefresh()
        val canReadStorage = storage
        refreshJob = scope.launch {
            startup.await()
            val controlRevision = revision
            val snapshot = withContext(Dispatchers.IO) { readLibrary(pkg, canReadStorage) }
            // A completed preference write already supplied a newer control snapshot.
            applyRefresh(snapshot, updateControl = revision == controlRevision)
        }
    }

    private fun prepareRefresh(): String {
        storage = hasStorage()
        return detectGame().also { if (it != game) useGame(it) }
    }

    private class LibrarySnapshot(
        val mods: List<Importer.Mod>, val labels: Map<String, String>, val status: Control.Status,
        val control: JSONObject,
    )

    private fun readLibrary(pkg: String, canReadStorage: Boolean): LibrarySnapshot {
        if (canReadStorage) runCatching { configureShared(pkg) }
            .onFailure { AppLog.warning(pkg, LOGGER, "shared-mods-error ${it.message}") }
        return LibrarySnapshot(Importer.list(modsRoot, Control.models(pkg)), Labels.read(labelsFile),
            Control.status(pkg), Control.load(pkg))
    }

    private fun applyRefresh(snapshot: LibrarySnapshot, updateControl: Boolean = true) {
        mods = snapshot.mods
        labels = snapshot.labels
        status = snapshot.status
        if (updateControl) applyControl(snapshot.control)
        libraryReady = true
        installPcHashes()
    }

    private fun applyControl(control: JSONObject) {
        currentControl = control
        val selected = control.optJSONArray("selected")
        selectedMods = if (selected == null) emptySet() else buildSet {
            for (i in 0 until selected.length()) add(selected.optString(i))
        }
        enabled = control.optBoolean("enabled", false)
        logLevel = control.optString("logLevel", "info").takeIf { it in Control.LOG_LEVELS } ?: "info"
        boneRefreshRequest = control.optLong("refreshBones", 0)
        boneRefreshCharacter = control.optInt("refreshBonesCharacter", 0)
        revision++
    }
    /** The PC hash database (assets/pc-hashes.json, THIRD_PARTY.md) and the device hash map shipped with the App
     *  (assets/device-hashes.json): the game reads both from the shared folder. */
    private fun installPcHashes() {
        if (pcHashesFor == game || !storage) return
        // On a thread of its own: the three files are ~28 MB, read and compared with the installed copies (1.0.4).
        val pkg = game; pcHashesFor = pkg
        Thread {
            runCatching {
                val shared = sharedModels ?: configureShared(pkg)
                val targets = Control.GAMES.filter { packageInfo(it) != null || Control.shared(it).isDirectory }.ifEmpty { listOf(pkg) }
                for ((file, what) in listOf("pc-hashes.json" to "PC hash 数据库", "device-hashes.json" to "手机 hash 表", OfflineSkeletons.FILE to "离线骨骼表")) {
                    val bytes = context.assets.open(file).use { it.readBytes() }
                    for (target in targets) if (Control.installShared(target, file, bytes)) AppLog.info(target, LOGGER, "shared-installed $what（${bytes.size / 1024} KB）")
                }
                for (target in targets) Control.installShared(target, "skeleton-policy.json", OfflineSkeletons.capturePolicy(Control.shared(target)))
                shared.prepare(pkg)
                scope.launch { refreshInBackground() }
            }.onFailure { pcHashesFor = ""; AppLog.warning(pkg, LOGGER, "shared-error hash 数据安装失败：${it.message}") }
        }.apply { name = "gibox-install-tables"; isDaemon = true }.start()
    }
    fun refreshStatus() { status = Control.status(game) }
    fun acceptStatus(pkg: String, snapshot: Control.Status) { if (game == pkg) status = snapshot }
    fun refreshingBones(id: Int): Boolean = boneRefreshRequest > 0 && boneRefreshCharacter == id &&
        !(status.boneRefreshRequest == boneRefreshRequest && status.boneRefreshCharacter == id && status.boneRefreshState == "complete")
    fun boneRefreshMessage(id: Int): String? {
        if (boneRefreshRequest <= 0 || boneRefreshCharacter != id) return null
        if (status.boneRefreshRequest != boneRefreshRequest || status.boneRefreshCharacter != id) return "等待游戏响应，请在图鉴打开${Characters.nameOf(id)}并停留，或在大世界切换到该角色。"
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
    private val groupedMods by derivedStateOf {
        mods.groupBy { it.id }.toSortedMap(compareBy { if (it == 0) Int.MAX_VALUE else it })
            .map { (id, list) ->
                Group(id, if (id == 0) "未选择角色" else Characters.nameOf(id), list.sortedBy { label(it.name).lowercase() })
            }
    }
    fun groups(): List<Group> = groupedMods
    fun group(id: Int): Group? = groups().firstOrNull { it.id == id }

    fun rename(name: String, text: String) {
        val pkg = game
        scope.launch {
            preferenceWrites.withLock {
                val updated = withContext(Dispatchers.IO) {
                    runCatching {
                        Labels.set(labelsFile, name, text)
                        AppLog.info(pkg, LOGGER, "rename $name → ${text.trim().ifEmpty { name }}")
                        Labels.read(labelsFile)
                    }
                }
                updated.fold(
                    onSuccess = {
                        labels = it
                        if (refreshJob?.isActive == true) refreshInBackground()
                    },
                    onFailure = { fail("改名", it, null) },
                )
            }
        }
    }

    fun selected(name: String): Boolean = name in selectedMods

    /**
     * The game LSPosed injects GIBox into. The installed games, narrowed to the scope ticked in LSPosed when the modern
     * service tells it (LspService); one left: that one. More than one: the one chosen on the home card (game.txt), else
     * the one injected most recently (Entry leaves GIBox/injected.txt in the game's media folder each time it loads
     * there), else the official one.
     */
    private fun detectGame(): String {
        val all = SUPPORTED_GAMES
        val found = all.filter { packageInfo(it) != null }
        availableClients = Control.GAMES.filter { it != "com.miHoYo.ys.bilibili" || it in found }
        val installed = found.ifEmpty { all }
        val scope = LspService.scope
        val candidates = (if (scope == null) installed else installed.filter { it in scope }).ifEmpty { installed }
        games = if (scope != null && candidates.size > 1) candidates else emptyList()
        readPrivate("game.txt").takeIf { it in clientChoices() }?.let { return it }
        if (candidates.size == 1) return candidates[0]
        val marks = candidates.associateWith { File(Control.shared(it), "injected.txt") }.filterValues { it.isFile }
        return marks.maxByOrNull { it.value.lastModified() }?.key ?: candidates[0]
    }
    /** The official servers stay available for configuration even before the games are installed. */
    fun clientChoices(): List<String> = availableClients
    fun chooseClient(pkg: String) {
        if (pkg !in clientChoices() || pkg == game || !libraryReady || busy != null || host.get()?.page is Screen.Pick) return
        writePrivate("game.txt", pkg)
        useGame(pkg)
        refreshInBackground()
    }
    fun viewSource() {
        try { host.get()?.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/Yuanqi314/GIBox"))) }
        catch (e: Throwable) { fail("打开源代码", e, null) }
    }
    fun chooseGame(pkg: String) {
        if (pkg !in games || pkg == game) return
        writePrivate("game.txt", pkg)
        useGame(pkg)
        refresh()
    }
    private fun useGame(pkg: String) {
        libraryReady = false
        mods = emptyList()
        selectedMods = emptySet()
        currentControl = JSONObject()
        logLevel = "info"
        game = pkg
        AppLog.info(pkg, LOGGER, "game ${serverName(pkg)}（$pkg，按 LSPosed 注入自动选择）")
        runCatching { Control.retireExperiments(pkg) }
    }
    fun setReplacement(on: Boolean) = savePreference("保存") { pkg ->
        Control.setEnabled(pkg, on)
        AppLog.info(pkg, "app", if (on) "model-replacement 开启" else "model-replacement 关闭")
    }
    /** Switching on a second mod of a character asks first (both, only this one, or not). */
    fun setSelected(mod: Importer.Mod, on: Boolean) {
        savePreference("保存", beforeWrite = {
            val others = if (on) enabledWith(mod.name, mod.id) else emptyList()
            if (others.isNotEmpty()) prompt = bothPrompt("同时开启多个 MOD？", "", mod.name, mod.id, others, "取消")
            others.isEmpty()
        }) { pkg ->
            if (on) enable(pkg, mod.name, emptyList())
            else {
                Control.disable(pkg, mod.name)
                AppLog.info(pkg, LOGGER, "mod-disable ${mod.name}")
            }
        }
    }
    /** Evaluated on the UI thread after earlier selection writes have applied their snapshot. */
    private fun enabledWith(name: String, id: Int): List<String> =
        if (id == 0 || Characters.isScene(id)) emptyList()
        else mods.filter { it.name != name && it.id == id && it.converted && it.name in selectedMods }.map { it.name }

    private fun enable(pkg: String, name: String, off: List<String>) {
        for (o in off) Control.disable(pkg, o)
        Control.enable(pkg, name)
        AppLog.info(pkg, LOGGER, "mod-enable $name" + if (off.isEmpty()) "" else "，同时停用 " + off.joinToString("、"))
    }
    private fun bothPrompt(title: String, lead: String, name: String, id: Int, others: List<String>, dismiss: String) = Prompt(
        title = title,
        text = lead + "${Characters.nameOf(id)} 已经开启了：${others.joinToString("、") { label(it) }}\n" +
            "同时开启多个模组可能会导致模组冲突或者不生效。",
        confirm = "全部开启", onConfirm = { savePreference("保存") { pkg -> enable(pkg, name, emptyList()) } },
        neutral = "只开这个", onNeutral = { savePreference("保存") { pkg -> enable(pkg, name, others) } },
        dismiss = dismiss,
        emphasizeNeutral = true,
    )
    internal fun variantChoices(variants: Variants, model: String): Map<String, Int> =
        variants.choices(currentControl, model)

    internal fun pendingVariant(model: String, variable: String): Int? =
        pendingVariants[Triple(game, model, variable)]?.value

    fun setVariable(mod: Importer.Mod, variable: String, value: Int) {
        val key = Triple(game, mod.name, variable)
        val choice = PendingVariant(value)
        pendingVariants[key] = choice
        savePreference("保存", settled = {
            if (pendingVariants[key] === choice) pendingVariants.remove(key)
        }) { pkg -> Control.setVariable(pkg, mod.name, variable, value) }
    }

    /** Serialize game commands with other preference writes, including repeated taps on the same key. */
    fun pressKey(mod: Importer.Mod, key: ModelManifest.Key, defaults: Map<String, Int>, back: Boolean = false) {
        val reported = status.keys[mod.name]?.firstOrNull { it.section == key.section }?.current ?: -1
        savePreference("按键") { pkg ->
            if (key.selectable()) {
                val stored = Control.load(pkg).optJSONObject("variables")?.optJSONObject(mod.name)
                val presets = key.presets.indices.map { presetValues(key, it) }
                val matches = presets.indices.filter { i ->
                    presets[i].isNotEmpty() && presets[i].all { (name, value) ->
                        val fallback = defaults[name] ?: 0
                        (stored?.optInt(name, fallback) ?: fallback) == value
                    }
                }
                val current = matches.singleOrNull() ?: reported.coerceAtLeast(0)
                val next = (current + (if (back) -1 else 1) + presets.size) % presets.size
                Control.keyInputWithVariables(pkg, mod.name, key.file, key.section, false, next, null, presets[next])
            } else Control.pressKey(pkg, mod.name, key.file, key.section, back)
            AppLog.info(pkg, LOGGER, "key-press ${mod.name} ${key.section}" + if (back) "（上一个）" else "")
        }
    }
    /** A cycle key's preset chosen directly (0-based), activated in the game as if stepped to. */
    fun selectKey(mod: Importer.Mod, key: ModelManifest.Key, index: Int) = savePreference("按键") { pkg ->
        Control.keyInputWithVariables(pkg, mod.name, key.file, key.section, false, index, null, presetValues(key, index))
        AppLog.info(pkg, LOGGER, "key-select ${mod.name} ${key.section}：第 ${index + 1} 个")
    }
    private fun presetValues(key: ModelManifest.Key, index: Int): Map<String, Int> =
        key.presets[index].values.mapNotNull { (name, text) ->
            val value = text.toDoubleOrNull()
            if (value != null && value.isFinite() && value >= Int.MIN_VALUE && value <= Int.MAX_VALUE && value == value.toInt().toDouble() && name.startsWith("$")) name to value.toInt() else null
        }.toMap()
    /** A hold key held (down) or released (up). */
    fun holdKey(mod: Importer.Mod, key: ModelManifest.Key, down: Boolean) = savePreference("按键") { pkg ->
        Control.keyInput(pkg, mod.name, key.file, key.section, false, -1, if (down) "down" else "up")
        AppLog.info(pkg, LOGGER, "key-hold ${mod.name} ${key.section}：" + if (down) "按住" else "松开")
    }
    /** Settings → 通用: the UID in the game's UI hidden (takes effect at the next game start). */
    fun hideUid(): Boolean = currentControl.optBoolean("hideUid", false)
    fun setHideUid(on: Boolean) = savePreference("保存") { pkg ->
        Control.setHideUid(pkg, on)
        AppLog.info(pkg, LOGGER, "hide-uid " + if (on) "开启（重启游戏后生效）" else "关闭")
    }
    fun synthesisButton(): Boolean = currentControl.optBoolean("synthesisButton", false)
    fun setSynthesisButton(on: Boolean) = savePreference("保存随身合成台设置") { pkg -> Control.setSynthesisButton(pkg, on) }
    fun cameraFade(): String = Control.cameraFade(currentControl)
    fun setCameraFade(value: String) = savePreference("保存角色渐隐设置") { pkg -> Control.setCameraFade(pkg, value) }
    fun loadingFit(): String = Control.loadingFit(currentControl)
    fun setLoadingFit(value: String) = savePreference("保存加载图片比例") { pkg -> Control.setLoadingFit(pkg, value) }
    fun npcModels(): Boolean = currentControl.optBoolean("npcModels", false)
    fun setNpcModels(on: Boolean) = savePreference("保存") { pkg ->
        Control.setNpcModels(pkg, on)
        AppLog.info(pkg, LOGGER, "npc-models $on")
    }
    /** Replaced characters kept on the full model (LOD0) by the game's own LOD component. */
    fun lodPin(): Boolean = currentControl.optBoolean("lodPin", true)
    fun setLodPin(on: Boolean) = savePreference("保存") { pkg ->
        Control.setLodPin(pkg, on)
        AppLog.info(pkg, LOGGER, "lod-pin " + if (on) "开启" else "关闭")
    }
    /** Material experiments (debug): read back on every recomposition through `revision`. */
    fun experiment(key: String): Boolean = currentControl.optJSONObject("experiments")?.optBoolean(key, false) ?: false
    fun experimentInt(key: String): Int = currentControl.optJSONObject("experiments")?.optInt(key, 0) ?: 0
    fun setExperiment(key: String, value: Any) = savePreference("保存实验设置") { pkg ->
        Control.setExperiment(pkg, key, value)
        AppLog.info(pkg, LOGGER, "experiment $key = $value")
    }
    /** Texture-only mods: strategy and LOD option (control.json "textureMods"), and a run of the test rounds. */
    fun textureMods(key: String, fallback: String): String = currentControl.optJSONObject("textureMods")?.optString(key, fallback) ?: fallback
    fun setTextureMods(key: String, value: String) = savePreference("保存纯贴图设置") { pkg ->
        Control.setTextureMods(pkg, key, value)
        AppLog.info(pkg, LOGGER, "texture-option $key = $value")
    }
    fun textureModsFlag(key: String): Boolean = currentControl.optJSONObject("textureMods")?.optBoolean(key, false) ?: false
    fun setTextureModsFlag(key: String, value: Boolean) = savePreference("保存纯贴图设置") { pkg ->
        Control.setTextureMods(pkg, key, value)
        AppLog.info(pkg, LOGGER, "texture-option $key = $value")
    }
    fun hashMode(): String = Control.hashMode(currentControl)
    fun setHashMode(mode: String) = savePreference("保存 hash 模式") { pkg ->
        Control.setHashMode(pkg, mode)
        AppLog.info(pkg, LOGGER, "hash-mode $mode（重启游戏后生效）")
    }
    /** 显示替换 开/关 for a model with its own on/off switch (model.json "replaceWhen"). */
    fun setReplacing(mod: Importer.Mod, variants: Variants, on: Boolean) = savePreference("保存") { pkg ->
        val change = variants.switchTo(on, variants.current(pkg, mod.name))
        if (change.isNotEmpty()) Control.setVariables(pkg, mod.name, change)
    }
    fun changeLogLevel(level: String) = savePreference("保存") { pkg -> Control.setLogLevel(pkg, level) }
    fun exportBones() = save("保存") {
        Control.requestBoneExport(game)
        notifyUser("已通知游戏导出骨骼，校验未通过会自动重试")
    }
    fun exportCharacterBones(id: Int) {
        if (!storage) { needStorage(); return }
        val pkg = game
        work("导出骨骼", "正在整理${Characters.nameOf(id)}的骨骼…", { _ ->
            try { SkeletonExport.write(Control.shared(pkg), id) }
            catch (e: IllegalArgumentException) {
                if (e.message?.startsWith("暂无骨骼。") == true) null else throw e
            }
        }) { file ->
            if (file == null) {
                Control.requestBoneRefresh(pkg, id)
                refresh()
                prompt = Prompt("等待采集骨骼", "请打开游戏图鉴中的${Characters.nameOf(id)}并停留数秒。采集完成后，再点一次本页的导出骨骼按钮即可分享。")
                return@work
            }
            val uri = DocumentsContract.buildDocumentUri(DirectoryProvider.AUTHORITY, "$pkg:shared/skeleton-exports/${file.name}")
            val send = Intent(Intent.ACTION_SEND).setType("application/json").putExtra(Intent.EXTRA_STREAM, uri).apply {
                clipData = ClipData.newRawUri("骨骼表", uri); addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            try {
                host.get()?.startActivity(Intent.createChooser(send, "导出骨骼表"))
                notifyUser("已导出：${file.absolutePath}")
            } catch (e: Throwable) { fail("分享骨骼", e, null) }
        }
    }
    fun confirmBoneRefresh(id: Int) {
        if (id !in 10000000..10001999 || refreshingBones(id)) return
        if (!storage) { needStorage(); return }
        val pkg = game
        val other = if (boneRefreshCharacter != id && refreshingBones(boneRefreshCharacter))
            "当前还有${Characters.nameOf(boneRefreshCharacter)}的采集请求，确定后将改为采集本页角色。\n\n" else ""
        prompt = Prompt(
            title = "重新采集骨骼？",
            text = other + "将重新获取${Characters.nameOf(id)}的骨骼。可直接打开图鉴中的该角色并停留，或在大世界切换到该角色；若提示只有低精度模型，再打开该角色详情或队伍预览并停留。完整模型校验通过后保存，等待期间保留原骨骼库。\n\n采集完成后，请重新转换需要使用新骨骼的 MOD。",
            confirm = "确定", dismiss = "取消",
            onConfirm = { save("重新采集骨骼") {
                Control.requestBoneRefresh(pkg, id)
                AppLog.info(pkg, LOGGER, "bone-recapture ${Characters.nameOf(id)}（$id）：已请求重新采集骨骼")
                notifyUser("已提交，请打开图鉴中的${Characters.nameOf(id)}并停留")
            } },
        )
    }
    private fun savePreference(
        what: String, settled: () -> Unit = {}, beforeWrite: (() -> Boolean)? = null, change: (String) -> Unit,
    ) {
        if (!hasStorage()) { settled(); needStorage(); return }
        val pkg = game
        scope.launch {
            try {
                startup.await()
                preferenceWrites.withLock {
                    if (beforeWrite != null && (game != pkg || !beforeWrite())) return@withLock
                    val updated = withContext(Dispatchers.IO) {
                        runCatching {
                            configureShared(pkg)
                            change(pkg)
                            Control.load(pkg)
                        }
                    }
                    if (pkg == game) updated.fold(
                        onSuccess = ::applyControl,
                        onFailure = { fail(what, it, null) },
                    )
                }
            } finally {
                settled()
            }
        }
    }

    fun showConversionRecord(file: File) {
        val activity = host.get() ?: return
        activity.lifecycleScope.launch {
            val text = withContext(Dispatchers.IO) { runCatching { file.readText() } }
            if (host.get() !== activity || activity.isFinishing || prompt != null || renaming != null || busy != null) return@launch
            text.fold(
                onSuccess = { prompt = Prompt("转换记录", it) },
                onFailure = { fail("读取转换记录", it, null) },
            )
        }
    }

    /** A change to the game's files (control.json, the log): without file access, the reminder instead. */
    private fun save(what: String, needsStorage: Boolean = true, change: () -> Unit) {
        if (needsStorage && !hasStorage()) { needStorage(); return }
        try { if (needsStorage) configureShared(); change() } catch (e: Throwable) { fail(what, e, null) }
        refresh()
    }

    // ---------------------------------------------------------------- import
    /** A .zip the system file picker returned. */
    fun importFrom(uri: Uri) {
        if (busy != null) return
        if (!storage) { needStorage(); return }
        val source = SafSource(context.contentResolver, uri, context.cacheDir)
        val pkg = game
        val bones = boneRoot
        work("导入 MOD", "正在导入", { log ->
            // A restored picker still owns its staged files. Reclaim abandoned imports on the next import.
            Importer.discardAll(modsRoot)
            val pending = Importer.prepare(source, modsRoot, log)
            try {
                val picked = source.name()
                AppLog.info(pkg, LOGGER, "read $picked：${log.toString().trim()}")
                Triple(pending, picked, Importer.guess(pending, picked, bones))
            } catch (e: Exception) {
                Importer.discard(pending)
                throw e
            }
        }) { (pending, picked, guess) ->
            val name = Importer.suggestName(picked)
            val before = mods.firstOrNull { it.name == name }?.id ?: 0
            open(Screen.Pick(name, picked, pending, null, before, guess.recommended.map { it.id }))
        }
    }

    fun chooseCharacter(mod: Importer.Mod) {
        val source = mod.source ?: return
        val bones = boneRoot
        work("识别角色", "正在识别角色", { _ ->
            Importer.guess(source, mod.sourceName, bones)
        }) { guess -> open(Screen.Pick(mod.name, mod.sourceName, null, source, mod.id, guess.recommended.map { it.id })) }
    }

    /** Model INIs of a package with several schemes (relative paths); fewer than two: nothing to choose. */
    fun schemes(dir: File?): List<String> = dir?.let { Importer.schemes(it) } ?: emptyList()
    fun scheme(dir: File?): String = dir?.let { Importer.scheme(it) } ?: ""
    fun animationBoneFallback(dir: File?): Boolean = dir?.let { Importer.animationBoneFallback(it) } ?: false
    fun setAnimationBoneFallback(mod: Importer.Mod, enabled: Boolean) = save("保存动画转换实验", needsStorage = false) {
        mod.source?.let { Importer.setAnimationBoneFallback(it, enabled) }
        AppLog.info(game, LOGGER, "animation-compat ${mod.name}：" + (if (enabled) "开" else "关") + "（下次转换生效）")
        notifyUser(if (enabled) "已开启动画骨骼兼容，请重新转换此 MOD" else "已恢复严格检查，请重新转换此 MOD")
    }
    /** Another INI scheme for an imported mod: stored, and converted again if it already was. */
    fun chooseScheme(mod: Importer.Mod, rel: String) {
        val source = mod.source ?: return
        if (rel == scheme(source)) return
        try { Importer.setScheme(source, rel); AppLog.info(game, LOGGER, "ini-scheme ${mod.name} → $rel") }
        catch (e: Throwable) { fail("选择 INI 方案", e, null); return }
        refresh()
        if (mod.converted && (mod.id in 10000000..10001999 || Characters.isScene(mod.id) || Characters.isLoading(mod.id))) convert(source, mod.id)
    }
    /** Confirmed on the character page; `scheme` is the INI chosen there for a package with several. */
    fun confirm(pick: Screen.Pick, avatar: Characters.Avatar, scheme: String? = null, outfit: String? = null) {
        val activity = host.get() ?: return
        if (activity.page != pick || activity.isFinishing) return
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
            activity.finishConfirmedPick()
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
        val activity = host.get() ?: return
        if (activity.page != pick || activity.isFinishing) return
        activity.finishConfirmedPick()
        convertJob { log, use3dm ->
            AppLog.info(game, LOGGER, "import ${pick.importedAs} → Mods/$name，角色 ${Characters.nameOf(id)}（$id）")
            Importer.convert(Importer.commit(pending, modsRoot, name), id, modelsRoot, boneRoot, decoder, log, use3dm)
        }
    }
    /** Re-conversion of an imported mod, for `id`. */
    fun convert(source: File, id: Int) {
        convertJob { log, use3dm ->
            AppLog.info(game, LOGGER, "convert ${source.name} → ${Characters.nameOf(id)}（$id）")
            Importer.convert(source, id, modelsRoot, boneRoot, decoder, log, use3dm)
        }
    }
    private fun convertJob(job: (StringBuilder, Boolean) -> Importer.Result) {
        val use3dm = !experiment("skip3dmConversion")
        work("转换", "正在转换...", { log ->
            convertShared {
                try { job(log, use3dm).also { AppLog.debug(game, "converter", "conversion-log\n" + log.toString().trim()) } }
                catch (e: Throwable) { AppLog.debug(game, "converter", "conversion-log\n" + log.toString().trim()); throw e }
            }
        }) { (r, others) ->
            val model = r.model.name
            AppLog.info(game, LOGGER, "converted $model：${r.summary.replace('\n', ' ')}")
            val summary = if (others.isEmpty()) r.summary else r.summary + "\n\n" + others.joinToString("\n")
            refresh()
            val enabled = enabledWith(model, r.id)
            if (enabled.isEmpty()) {
                try { enable(game, model, emptyList()) } catch (e: Throwable) { AppLog.warning(game, LOGGER, "enable-error 启用失败：$e") }
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
                val use3dm = !experiment("skip3dmConversion")
                AppLog.info(game, LOGGER, "reconvert-all ${list.size} 个 MOD")
                work("重新转换", "正在转换...", { _ ->
                    val failed = ArrayList<String>()
                    list.forEach { mod ->
                        val name = label(mod.name)
                        val log = StringBuilder()
                        try {
                            val (_, notes) = convertShared { Importer.convert(mod.source, mod.id, modelsRoot, boneRoot, decoder, log, use3dm) }
                            AppLog.debug(game, "converter", "conversion-log\n" + log.toString().trim())
                            for (note in notes) if (note.contains("失败")) failed.add("$name（$note）")
                        } catch (e: Throwable) { AppLog.debug(game, "converter", "conversion-log\n" + log.toString().trim()); AppLog.warning(game, LOGGER, "reconvert-error ${mod.name}：$e"); failed.add("$name：${e.message ?: e}") }
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
    /** Characters with a skeleton: in the table made from the game's files, or recorded by the game on this device. */
    fun boneImportHint(id: Int, outfit: String): String {
        if (Characters.isLoading(id)) return "每次加载随机切换图片，无需骨骼"
        if (id !in 10000000..10001999) return "按 hash 匹配场景中的部件"
        val table = OfflineSkeletons.load(boneRoot.absoluteFile.parentFile)
        if (table?.prefabFor(id, outfit.takeIf { it.isNotEmpty() }) != null) return "优先使用内置骨骼，无需采集"
        return "内置暂无此骨骼；打开图鉴后自动采集，可在角色页导出补充"
    }
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
            title = "删除 ${label(mod.name)}", text = "删除这份 MOD 和它的所有转换结果？导入时选择的原文件会保留。",
            confirm = "删除", dismiss = "取消", destructive = true,
            onConfirm = {
                work("删除", "正在删除 ${label(mod.name)}…", { _ ->
                    val shared = sharedModels ?: configureShared()
                    shared.convert {
                        Control.disable(game, mod.name)
                        shared.delete(mod.name)
                        Importer.delete(mod, modelsRoot)
                        Labels.set(labelsFile, mod.name, null)
                        AppLog.info(game, LOGGER, "mod-delete ${mod.name}")
                    }
                }) {
                    host.get()?.takeIf { it.page == Screen.Detail(mod.name) }?.finish()
                    refresh()
                }
            },
        )
    }

    /** The old manual conversion (settings → 高级): explicit bone folder, ID, name and mapping. */
    fun manualConvert(src: String, bones: String, id: Int, name: String, mapping: String, variants: Boolean) {
        val dest = File(modelsRoot, name)
        if (dest.exists()) { prompt = Prompt("错误", "输出目录已经存在，请换一个模型名：$dest"); return }
        work("手动转换", "正在转换...", { log ->
            val shared = sharedModels ?: configureShared()
            shared.convert {
                val out = ModelConverter.convert(File(src), File(bones), dest, id, mapping, variants, decoder, log)
                Pair(out, out.flatMap { shared.publish(it) })
            }
        }) { (out, failures) ->
            AppLog.info(game, "converter", "manual-convert-done $out")
            refresh()
            prompt = Prompt("转换结束", (out.map { it.toString() } + failures).joinToString("\n"))
        }
    }

    // ---------------------------------------------------------------- system
    fun launch() {
        val intent = context.packageManager.getLaunchIntentForPackage(game)
        if (intent == null) { prompt = Prompt("错误", "没有找到游戏：$game\n（没有安装，或者在设置里选错了服务器）"); return }
        AppLog.info(game, "app", "launch $game")
        host.get()?.startActivity(intent)
    }
    fun grantStorage() {
        try {
            if (Build.VERSION.SDK_INT >= 30) host.get()?.startActivity(Intent("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION", Uri.parse("package:" + context.packageName)))
            else host.get()?.requestPermissions(arrayOf("android.permission.READ_EXTERNAL_STORAGE", "android.permission.WRITE_EXTERNAL_STORAGE"), 1)
        } catch (e: Throwable) { fail("打开权限设置", e, null) }
    }
    fun needStorage() {
        prompt = Prompt("需要文件访问权限", "转换好的模型要放进原神的目录（Android/media），游戏才读得到，需要“所有文件访问权限”。", confirm = "去授予", onConfirm = { grantStorage() }, dismiss = "取消")
    }
    private fun hasStorage(): Boolean =
        if (Build.VERSION.SDK_INT >= 30) Environment.isExternalStorageManager()
        else context.checkSelfPermission("android.permission.READ_EXTERNAL_STORAGE") == PackageManager.PERMISSION_GRANTED
    fun copy(label: String, text: String) {
        val clipboard = context.getSystemService(ClipboardManager::class.java) ?: return
        clipboard.setPrimaryClip(ClipData.newPlainText(label, text))
        if (Build.VERSION.SDK_INT < 33) notifyUser("已复制：$label")
    }
    private fun notifyUser(text: String) { notice = text }
    private fun packageInfo(pkg: String): android.content.pm.PackageInfo? = try {
        @Suppress("DEPRECATION")
        if (Build.VERSION.SDK_INT >= 33) context.packageManager.getPackageInfo(pkg, PackageManager.PackageInfoFlags.of(0))
        else context.packageManager.getPackageInfo(pkg, 0)
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
                val pm = context.packageManager
                val seen = pm.queryIntentActivities(first, 0).map { it.activityInfo.packageName }.toMutableSet()
                val extra = alternates.flatMap { alt ->
                    pm.queryIntentActivities(alt, 0).filter { seen.add(it.activityInfo.packageName) }.map {
                        LabeledIntent(Intent(alt).setClassName(it.activityInfo.packageName, it.activityInfo.name), it.activityInfo.packageName, it.loadLabel(pm), it.iconResource)
                    }
                }
                if (extra.isNotEmpty()) chooser.putExtra(Intent.EXTRA_INITIAL_INTENTS, extra.toTypedArray())
            }
        }
        host.get()?.startActivity(chooser)
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
        if (!logFile.isFile) { notifyUser("暂无日志文件"); return }
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
    suspend fun clearLog(file: File): Boolean = runCatching {
        withContext(Dispatchers.IO) {
            for (entry in listOf(File(file.path + ".1"), file)) {
                check(!entry.exists() || entry.delete()) { "无法删除 ${entry.name}" }
            }
        }
    }.fold(onSuccess = { true }, onFailure = {
        if (it is kotlinx.coroutines.CancellationException) throw it
        fail("清空日志", it, null)
        false
    })

    // ---------------------------------------------------------------- background work, errors
    private val decoder = ModelConverter.TextureDecoder { NativeBridge.decodeTexture(it) }
    private fun <T> work(title: String, text: String, job: (StringBuilder) -> T, done: (T) -> Unit) {
        busy = text
        scope.launch {
            val log = StringBuilder()
            val result = withContext(Dispatchers.IO) { runCatching { job(log) } }
            busy = null
            result.fold(onSuccess = done, onFailure = { fail(title, it, log) })
        }
    }
    fun fail(what: String, e: Throwable, log: StringBuilder?) {
        AppLog.error(game, LOGGER, "action-error ${what}失败：$e")
        val details = if (log != null && log.isNotEmpty()) "\n\n" + log.toString().trim() else ""
        prompt = Prompt("错误", "${what}失败：${e.message ?: e.toString()}$details\n\n日志文件：$logFile")
    }
    private fun readPrivate(name: String): String = try { File(context.filesDir, name).readText().trim() } catch (e: Exception) { "" }
    private fun writePrivate(name: String, value: String) { try { File(context.filesDir, name).writeText(value) } catch (e: Exception) { } }
}

/** "茜特菈莉 · 10000107" ("申鹤 · 冷花幽露 · 10000063" for a skin's mod), or what is missing. */
fun who(mod: Importer.Mod): String = when {
    mod.id != 0 -> Characters.nameOf(mod.id) + (if (mod.outfit.isEmpty()) "" else " · " + Outfits.current().label(mod.outfit)) + " · " + mod.id
    mod.converted -> "按名字识别"
    else -> "未选择角色"
}
fun serverName(pkg: String): String = when (pkg) { "com.miHoYo.Yuanshen" -> "国服"; "com.miHoYo.GenshinImpact" -> "国际服"; else -> "国服 · B 服" }
