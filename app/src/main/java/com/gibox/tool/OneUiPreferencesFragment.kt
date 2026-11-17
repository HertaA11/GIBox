package com.gibox.tool

import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.os.Bundle
import android.view.LayoutInflater
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.appcompat.widget.SearchView
import androidx.fragment.app.DialogFragment
import androidx.preference.ListPreference
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceGroupAdapter
import androidx.preference.SwitchPreferenceCompat
import androidx.recyclerview.widget.RecyclerView
import dev.oneuiproject.oneui.layout.ToolbarLayout
import dev.oneuiproject.oneui.preference.InsetPreferenceCategory

/** Native SESL preferences. Control/AppState remain the only source of settings values. */
class OneUiPreferencesFragment : PreferenceFragmentCompat() {
    private val state get() = (requireActivity() as MainActivity).state
    private var page: Screen = Screen.Home
    private var renderedGame: String? = null
    private var toolbarLayout: ToolbarLayout? = null
    private var toolbarPage: Screen? = null
    private var query = ""
    private var group = ""

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        query = savedInstanceState?.getString("query").orEmpty()
        preferenceScreen = preferenceManager.createPreferenceScreen(requireContext())
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        val root = inflater.inflate(R.layout.fragment_preferences, container, false) as ToolbarLayout
        toolbarLayout = root
        root.findViewById<FrameLayout>(R.id.preferences_content)
            .addView(super.onCreateView(inflater, container, savedInstanceState))
        root.setNavigationButtonOnClickListener { state.back() }
        root.findViewById<View>(R.id.source_button).setOnClickListener { state.viewSource() }
        return root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        // Keep SESL's rounded background decoration; draw only the inset separators ourselves.
        setDivider(null)
        listView.addItemDecoration(GroupDividers())
        val margin = resources.getDimensionPixelSize(R.dimen.preference_page_margin)
        setPadding(margin, 0, margin, dp(24))
        listView.clipToPadding = false
        listView.seslSetLastRoundedCorner(false)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putString("query", query)
        super.onSaveInstanceState(outState)
    }

    override fun onDestroyView() {
        toolbarLayout = null
        toolbarPage = null
        super.onDestroyView()
    }

    internal fun dismissPreferenceDialog() {
        (parentFragmentManager.findFragmentByTag("androidx.preference.PreferenceFragment.DIALOG") as? DialogFragment)?.dismiss()
    }

    internal fun render(next: Screen) {
        if (toolbarLayout == null) return
        val changedPage = page != next
        val changedClient = renderedGame != state.game
        if (changedPage || changedClient) dismissPreferenceDialog()
        val scroll = if (!changedPage && !changedClient) listView.layoutManager?.onSaveInstanceState() else null
        page = next
        renderedGame = state.game
        configureToolbar()
        rebuild()
        if (scroll != null) listView.post { if (view != null) listView.layoutManager?.onRestoreInstanceState(scroll) }
        else if (changedPage || changedClient) listView.scrollToPosition(0)
    }

    private fun configureToolbar() {
        val layout = toolbarLayout ?: return
        layout.setTitle(when (page) {
            Screen.Home -> "GIBox"
            Screen.Accessibility -> "辅助功能"
            Screen.About -> "关于 GIBox"
            Screen.FeatureSearch -> "搜索功能"
            else -> "设置"
        })
        layout.showNavigationButtonAsBack = page != Screen.Home
        layout.findViewById<View>(R.id.source_button).visibility = if (page == Screen.About) View.VISIBLE else View.GONE
        val menu = layout.toolbar.menu
        // Keep the SearchView and keyboard intact while a preference value is refreshed.
        if (toolbarPage == page) return
        toolbarPage = page
        menu.clear()
        if (page == Screen.Home) {
            menu.add("搜索功能").apply {
                icon = NativePreferenceIcon(requireContext(), OneGlyph.Search, false)
                setShowAsAction(MenuItem.SHOW_AS_ACTION_ALWAYS)
                setOnMenuItemClickListener { state.open(Screen.FeatureSearch); true }
            }
        } else if (page == Screen.FeatureSearch) {
            val search = SearchView(requireContext()).apply {
                queryHint = "搜索功能"
                isIconified = false
                setQuery(query, false)
                setOnQueryTextListener(object : SearchView.OnQueryTextListener {
                    override fun onQueryTextSubmit(text: String?): Boolean { clearFocus(); return true }
                    override fun onQueryTextChange(text: String?): Boolean {
                        query = text.orEmpty()
                        rebuild()
                        listView.scrollToPosition(0)
                        return true
                    }
                })
            }
            menu.add("搜索功能").apply {
                actionView = search
                setShowAsAction(MenuItem.SHOW_AS_ACTION_ALWAYS)
            }
        }
    }

    private fun rebuild() {
        preferenceScreen.removeAll()
        group = ""
        if (page != Screen.Home) client()
        when (page) {
            Screen.Home -> home()
            Screen.Accessibility -> accessibility()
            Screen.About -> about()
            Screen.FeatureSearch -> search()
            else -> settings()
        }
    }

    private fun startGroup(key: String) {
        preferenceScreen.addPreference(InsetPreferenceCategory(requireContext()).apply {
            this.key = "gap_$key"
            setHeight(if (group.isEmpty()) dp(8) else resources.getDimensionPixelSize(R.dimen.preference_group_gap))
            seslSetSubheaderRoundedBackground(15)
        })
        group = key
    }

    private fun <T : Preference> add(pref: T, id: String, title: String, glyph: OneGlyph? = null): T {
        pref.key = id
        pref.title = title
        pref.isPersistent = false
        pref.isIconSpaceReserved = false
        pref.extras.putString("group", group)
        pref.isSingleLineTitle = false
        if (glyph != null) pref.icon = NativePreferenceIcon(requireContext(), glyph)
        preferenceScreen.addPreference(pref)
        return pref
    }

    private fun action(id: String, title: String, glyph: OneGlyph, summary: String? = null, click: (() -> Unit)? = null) {
        add(Preference(requireContext()), id, title, glyph).apply {
            this.summary = summary
            isSelectable = click != null
            if (click != null) setOnPreferenceClickListener { click(); true }
        }
    }

    private fun toggle(id: String, title: String, checked: Boolean, summary: String? = null, change: (Boolean) -> Unit) {
        add(SwitchPreferenceCompat(requireContext()), id, title).apply {
            this.summary = summary
            isChecked = checked
            setOnPreferenceChangeListener { _, value -> change(value as Boolean); false }
        }
    }

    private fun choice(id: String, title: String, current: String, values: List<Pair<String, String>>, note: String? = null, change: (String) -> Unit) {
        add(ListPreference(requireContext()), id, title).apply {
            dialogTitle = title
            entries = values.map { it.second }.toTypedArray()
            entryValues = values.map { it.first }.toTypedArray()
            value = current
            summary = listOfNotNull(values.firstOrNull { it.first == current }?.second, note).joinToString("\n")
            setOnPreferenceChangeListener { _, value -> change(value as String); false }
        }
    }

    private fun client() {
        startGroup("client")
        choice("client", "游戏版本", state.game, state.clientChoices().map { it to serverName(it) }) { state.chooseClient(it) }
    }

    private fun home() {
        startGroup("features")
        features.take(4).forEachIndexed { i, feature ->
            action("feature_$i", feature.title, feature.icon) { state.open(feature.page) }
        }
    }

    private fun accessibility() {
        startGroup("general")
        toggle("hide_uid", "隐藏用户UID", state.hideUid()) { state.setHideUid(it) }
        toggle("npc_models", "角色 MOD 同时替换对应 NPC", state.npcModels()) { state.setNpcModels(it) }
        startGroup("tools")
        toggle("synthesis", "随身合成台", state.synthesisButton(), "在游戏右上角显示合成台图标") { state.setSynthesisButton(it) }
        toggle("camera_fade", "取消角色渐隐", state.cameraFade() == "disabled") { state.setCameraFade(if (it) "disabled" else "original") }
        startGroup("loading")
        choice("loading_fit", "加载图片比例", state.loadingFit(), Control.LOADING_FITS.mapIndexed { i, value -> value to Control.LOADING_FIT_LABELS[i] }) { state.setLoadingFit(it) }
    }

    private fun settings() {
        startGroup("files")
        action("mods", "打开 Mods 文件夹", OneGlyph.Folder) { state.openFolder(state.modsRoot) }
        action("models", "打开模型文件夹", OneGlyph.Parcel) { state.openFolder(state.modelsRoot) }
        action("bones", "打开骨骼库文件夹", OneGlyph.Person) { state.openFolder(state.boneRoot) }
        startGroup("logs")
        action("log", "运行日志", OneGlyph.Info) { state.open(Screen.Log) }
        action("share_log", "分享日志文件", OneGlyph.Share) { state.shareLog() }
        val scope = LspService.scope
        action("framework", "LSPosed 服务", OneGlyph.Info,
            if (scope == null) "未收到（GIBox 没有按新接口加载）"
            else "${LspService.framework ?: "?"}\n作用域：${scope.ifEmpty { listOf("（空）") }.joinToString("、")}")
        startGroup("textures")
        choice("texture_strategy", "替换方式", state.textureMods("strategy", "auto"), listOf(
            "auto" to "自动（默认）", "diagnose" to "只检查，不替换", "direct" to "直接改游戏材质", "clone" to "复制材质再改"
        ), "其他方式用于对比排查，切换前会先还原") { state.setTextureMods("strategy", it) }
        choice("texture_lod", "远处的简化模型", state.textureMods("lod", "full"), listOf(
            "full" to "只换完整模型", "migrate" to "简化模型也换"
        ), "只替换核对一致的简化模型") { state.setTextureMods("lod", it) }
        val mode = state.hashMode()
        choice("hash_mode", "hash 模式", mode, listOf("off" to "关闭", "match" to "匹配", "collect" to "采集", "full" to "完整诊断"), when (mode) {
            "match" -> "模型上传时计算 hash，匹配角色、NPC 和怪物部件；可常开"
            "collect" -> "匹配并记录出现过的网格；采集完成后改回匹配"
            "full" -> "计算全部 hash 并记录诊断报告，开销较大"
            else -> "仅使用内置和本机已采集的 hash 表"
        } + "。重启游戏后生效") { state.setHashMode(it) }
        action("hash_report", "hash 对比报告", OneGlyph.Share, "安卓与 PC 逐个部件对比") { state.shareHashReport() }
        action("texture_test", "轮流测试替换方式", OneGlyph.Wrench, "每轮观察 4 秒后还原核对") { state.startTextureTest() }
        startGroup("lod")
        toggle("lod_pin", "被替换的角色保持完整模型", state.lodPin(), "保留 LOD0，避免简化骨骼导致变形；关闭后恢复游戏切换") { state.setLodPin(it) }
        startGroup("conversion")
        choice("animation", "动画播放方式", state.experimentInt("animationMode").toString(), listOf(
            "0" to "缓存帧网格", "1" to "动态网格", "2" to "暂停（初始帧）"
        ), "切换后自动重建，只影响带动画的 MOD") { state.setExperiment("animationMode", it.toInt()) }
        toggle("logic_core", "使用 3DMigoto 进行转换", state.experiment("logicCore"), "按 INI 规则切换动画、款式和按键；旧版 MOD 需重新转换") { state.setExperiment("logicCore", it) }
        toggle("key_steps", "逐个切换按钮", state.experiment("keySteps"), "按键切换里显示上一个、下一个") { state.setExperiment("keySteps", it) }
        startGroup("bone_tools")
        action("reconvert", "一键重新转换", OneGlyph.Refresh, "所有 MOD 按各自角色重新转换") { state.reconvertAll() }
        action("export_bones", "导出当前角色骨骼", OneGlyph.Person, "供手动转换使用") { state.exportBones() }
        action("manual", "手动转换（旧流程）", OneGlyph.Wrench, "指定骨骼、角色 ID 和部件映射") { state.open(Screen.Manual) }
    }

    private fun about() {
        startGroup("app")
        action("app", "GIBox", OneGlyph.Parcel, "版本 ${state.appVersion}\nAndroid 原神 MOD 管理")
        startGroup("about")
        action("developer", "开发者", OneGlyph.Person, "Yuanqi314")
        action("license", "开源许可证", OneGlyph.Info, "GNU General Public License v3.0")
        action("module", "模块状态", OneGlyph.Grid, if (state.moduleActive) "已激活" else "未激活")
    }

    private fun search() {
        startGroup("results")
        val found = features.filter { query.isNotBlank() && (it.title + it.words).contains(query.trim(), true) }
        if (found.isEmpty()) {
            action("empty", if (query.isBlank()) "搜索功能" else "没有找到相关功能", OneGlyph.Search,
                if (query.isBlank()) "MOD、辅助功能、设置或关于" else null)
        } else found.forEachIndexed { i, feature ->
            action("result_$i", feature.title, feature.icon) { state.back(); state.open(feature.page) }
        }
    }

    private fun dp(value: Int) = (value * resources.displayMetrics.density + .5f).toInt()

    private inner class GroupDividers : RecyclerView.ItemDecoration() {
        private val paint = Paint().apply { color = requireContext().getColor(R.color.preference_divider); strokeWidth = resources.displayMetrics.density * .5f }
        private val titleBounds = Rect()
        override fun onDrawOver(canvas: Canvas, parent: RecyclerView, state: RecyclerView.State) {
            val adapter = parent.adapter as? PreferenceGroupAdapter ?: return
            for (i in 0 until parent.childCount) {
                val row = parent.getChildAt(i)
                val position = parent.getChildAdapterPosition(row)
                if (position == RecyclerView.NO_POSITION || position + 1 >= adapter.itemCount) continue
                val item = adapter.getItem(position) ?: continue
                val next = adapter.getItem(position + 1) ?: continue
                val sameGroup = item.extras.getString("group")?.let { it == next.extras.getString("group") } == true
                if (!sameGroup) continue
                val title = row.findViewById<View>(android.R.id.title) ?: continue
                titleBounds.set(0, 0, title.width, title.height)
                parent.offsetDescendantRectToMyCoords(title, titleBounds)
                val y = row.bottom + row.translationY
                if (parent.layoutDirection == View.LAYOUT_DIRECTION_RTL) {
                    canvas.drawLine((parent.paddingLeft + dp(20)).toFloat(), y, titleBounds.right.toFloat(), y, paint)
                } else {
                    canvas.drawLine(titleBounds.left.toFloat(), y, (parent.width - parent.paddingRight - dp(20)).toFloat(), y, paint)
                }
            }
        }
    }
}
