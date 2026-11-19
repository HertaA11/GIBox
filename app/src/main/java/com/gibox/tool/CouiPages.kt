package com.gibox.tool

import android.os.Build
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.Image
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.animateScrollBy
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.itos.coui.backdrop.couiBackdrop
import com.itos.coui.backdrop.rememberCouiBackdrop
import com.itos.coui.chip.ChipTab
import com.itos.coui.dialog.CouiDialogAction
import com.itos.coui.dialog.CouiDialogActionColors
import com.itos.coui.dialog.CouiDialogActionStyle
import com.itos.coui.dialog.CouiDialogDefaults
import com.itos.coui.dialog.Dialog
import com.itos.coui.dialog.InputDialog
import com.itos.coui.dialog.LoadingDialog
import com.itos.coui.effect.couiButtonFeedback
import com.itos.coui.icons.CouiIcons
import com.itos.coui.input.CouiHapticFeedback
import com.itos.coui.input.performCouiHapticFeedback
import com.itos.coui.layout.Group
import com.itos.coui.layout.Item
import com.itos.coui.layout.Scaffold
import com.itos.coui.menu.CouiMenuAnchor
import com.itos.coui.menu.CouiMenuItem
import com.itos.coui.menu.Menu
import com.itos.coui.menu.couiMenuAnchor
import com.itos.coui.menu.rememberCouiMenuAnchor
import com.itos.coui.scroll.couiVerticalScroll
import com.itos.coui.search.SearchBar
import com.itos.coui.search.rememberCouiSearchBarState
import com.itos.coui.shape.CouiCapsuleShape
import com.itos.coui.switch.Switch
import com.itos.coui.text.CouiTextKind
import com.itos.coui.text.Text
import com.itos.coui.theme.CouiTheme
import com.itos.coui.theme.LocalCouiTextStyle
import com.itos.coui.topappbar.CouiTopAppBarDefaults
import com.itos.coui.topappbar.LargeTopAppBar
import com.itos.coui.topappbar.TopAppBar
import com.itos.coui.topappbar.TopAppBarButton
import com.itos.coui.topappbar.TopAppBarButtonGroup
import com.itos.coui.topappbar.TopAppBarSearch
import com.itos.coui.topappbar.TopAppBarSearchAction
import com.itos.coui.topappbar.TopAppBarSearchButton
import com.itos.coui.topappbar.TopAppBarSurface
import com.itos.coui.topappbar.rememberCouiTopAppBarSearchState

internal enum class Glyph {
    Parcel, Accessibility, Grid, Wrench, Info, Folder, Person, Share, Refresh,
    Search, Back, Next, Add, Check, Inactive, Edit, Delete, Log, Level,
}

@Composable
internal fun GlyphImage(
    glyph: Glyph, description: String? = null, size: Dp = 24.dp, monochrome: Boolean = false,
) {
    val vector: ImageVector = when (glyph) {
        Glyph.Parcel -> CouiIcons.Filled.Gamepad
        Glyph.Accessibility -> CouiIcons.Filled.Accessibility
        Glyph.Grid -> CouiIcons.Filled.Grid
        Glyph.Wrench -> CouiIcons.Filled.Setting
        Glyph.Info -> CouiIcons.Filled.SettingDetail
        Glyph.Folder -> CouiIcons.Filled.Album
        Glyph.Person -> CouiIcons.Filled.UserCircle
        Glyph.Share -> CouiIcons.Filled.Transfer
        Glyph.Refresh -> CouiIcons.Filled.AppUpdate
        Glyph.Search -> CouiIcons.Outlined.Search
        Glyph.Back -> CouiIcons.Outlined.Back
        Glyph.Next -> CouiIcons.Outlined.Next
        Glyph.Add -> CouiIcons.Outlined.Add
        Glyph.Check -> CouiIcons.Filled.ShieldCheck
        Glyph.Inactive -> CouiIcons.Filled.ShieldFlash
        Glyph.Edit -> CouiIcons.Outlined.SliderCustom
        Glyph.Delete -> CouiIcons.Outlined.Close
        Glyph.Log -> CouiIcons.Outlined.ListFilter
        Glyph.Level -> CouiIcons.Outlined.SliderCustom
    }
    val tint = monochrome || when (glyph) {
        Glyph.Search, Glyph.Back, Glyph.Next, Glyph.Add, Glyph.Edit, Glyph.Delete, Glyph.Log, Glyph.Level -> true
        else -> false
    }
    Image(vector, description, Modifier.size(size),
        colorFilter = if (tint) ColorFilter.tint(CouiTheme.colorScheme.onSurface) else null)
}

@Composable
internal fun Page(
    title: String,
    back: (() -> Unit)? = null,
    actions: @Composable RowScope.() -> Unit = {},
    topContent: @Composable () -> Unit = {},
    bottom: (@Composable () -> Unit)? = null,
    toolbar: (@Composable () -> Unit)? = null,
    modifier: Modifier = Modifier,
    content: @Composable (PaddingValues) -> Unit,
) {
    val ime = if (LocalWindowInfo.current.isWindowFocused) Modifier.imePadding() else Modifier
    Scaffold(
        modifier = modifier.fillMaxSize().then(ime),
        topBar = {
            Column {
                if (toolbar != null) toolbar() else TopAppBar(
                    title = { Text(text = title, kind = CouiTextKind.TopBar, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                    navigationIcon = { if (back != null) ToolbarAction(Glyph.Back, "返回", back) },
                    actions = actions,
                )
                topContent()
            }
        },
        bottomBar = { if (bottom != null) Column(Modifier.navigationBarsPadding()) { bottom() } },
        content = content,
    )
}

@Composable
internal fun ToolbarAction(icon: Glyph, label: String, click: () -> Unit) {
    ToolbarButton(click) { GlyphImage(icon, label, monochrome = true) }
}

@Composable
internal fun ToolbarButton(click: () -> Unit, content: @Composable () -> Unit) {
    if (Build.VERSION.SDK_INT >= 34) {
        TopAppBarButton(onClick = click) { content() }
    } else {
        val source = remember { MutableInteractionSource() }
        Box(Modifier.size(48.dp).clip(CouiCapsuleShape)
            .couiButtonFeedback(interactionSource = source)
            .clickable(source, null, role = Role.Button, onClick = click),
            contentAlignment = Alignment.Center) { content() }
    }
}

@Composable
internal fun SearchToolbar(
    title: String, placeholder: String, query: String, change: (String) -> Unit,
    expanded: Boolean, expand: (Boolean) -> Unit, back: (() -> Unit)? = null,
    leadingAction: (@Composable () -> Unit)? = null,
) {
    val keyboard = LocalSoftwareKeyboardController.current
    val close = { expand(false); keyboard?.hide(); Unit }
    BackHandler(expanded, close)
    if (Build.VERSION.SDK_INT < 34) {
        TopAppBar(title = { Text(text = title, kind = CouiTextKind.TopBar) },
            navigationIcon = { if (back != null) ToolbarAction(Glyph.Back, "返回", if (expanded) close else back) })
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp), verticalAlignment = Alignment.CenterVertically) {
            leadingAction?.invoke()
            SearchBar(modifier = Modifier.weight(1f), state = rememberCouiSearchBarState(expanded),
                query = query, onQueryChange = change, onExpandedChange = expand, placeholder = placeholder,
                cancelText = "取消", clearDescription = "清除搜索")
        }
        return
    }
    val motion = rememberCouiTopAppBarSearchState(expanded)
    var focusPending by remember { mutableStateOf(false) }
    TopAppBarSurface {
        Row(Modifier.fillMaxWidth().statusBarsPadding().heightIn(min = 64.dp).padding(horizontal = 16.dp),
            verticalAlignment = Alignment.CenterVertically) {
            if (back != null) TopAppBarSearchAction(state = motion) { ToolbarAction(Glyph.Back, "返回", back) }
            TopAppBarSearch(modifier = Modifier.weight(1f), state = motion, query = query, onQueryChange = change,
                onSearch = { keyboard?.hide() }, placeholder = placeholder, focusPending = expanded && focusPending,
                onFocused = { focusPending = false },
                action = { Text(text = "取消", kind = CouiTextKind.ItemValue) }, onActionClick = close) {
                Text(text = title, kind = CouiTextKind.TopBar, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            TopAppBarButtonGroup {
                if (leadingAction != null) TopAppBarSearchAction(state = motion) { leadingAction() }
                TopAppBarSearchButton(state = motion, contentDescription = placeholder,
                    onClick = { focusPending = true; expand(true) })
            }
        }
    }
}

@Composable
internal fun PreferencePage(
    title: String, back: () -> Unit, bottom: (@Composable () -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    val scroll = rememberScrollState()
    val behavior = CouiTopAppBarDefaults.exitUntilCollapsedScrollBehavior { scroll.maxValue > 0 }
    val density = LocalDensity.current
    Page(title, bottom = bottom, modifier = Modifier.nestedScroll(behavior.nestedScrollConnection), toolbar = {
        LargeTopAppBar(title = { BasicText(title, style = LocalCouiTextStyle.current, maxLines = 1,
            overflow = TextOverflow.Ellipsis) }, navigationIcon = { ToolbarAction(Glyph.Back, "返回", back) },
            scrollBehavior = behavior)
    }) { padding ->
        val body = remember(behavior, density, padding) { behavior.state.contentPadding(padding, density) }
        Column(Modifier.fillMaxSize().couiVerticalScroll(scroll).padding(body)
            .consumeWindowInsets(padding).padding(bottom = 20.dp), content = content)
    }
}

@Composable
internal fun ColumnPage(padding: PaddingValues, scroll: ScrollState = rememberScrollState(),
    content: @Composable ColumnScope.() -> Unit) {
    Column(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding)
        .couiVerticalScroll(scroll).padding(bottom = 20.dp), content = content)
}

@Composable
internal fun ActionRow(
    title: String,
    summary: String? = null,
    glyph: Glyph? = null,
    enabled: Boolean = true,
    showNext: Boolean = false,
    modifier: Modifier = Modifier,
    trailing: @Composable () -> Unit = {},
    titleColor: Color = Color.Unspecified,
    click: (() -> Unit)? = null,
) {
    Item(
        modifier = modifier,
        icon = glyph?.let { { GlyphImage(it) } },
        enabled = enabled,
        summary = summary?.let { { Text(text = it, kind = CouiTextKind.ItemSummary, color = CouiTheme.colorScheme.onSurfaceVariant) } },
        trailing = { trailing(); if (showNext) GlyphImage(Glyph.Next, size = 16.dp) },
        onClick = click,
    ) { Text(text = title, kind = CouiTextKind.ItemTitle, color = titleColor) }
}

@Composable
internal fun ToggleRow(
    title: String, checked: Boolean, summary: String? = null, glyph: Glyph? = null, enabled: Boolean = true,
    modifier: Modifier = Modifier, change: (Boolean) -> Unit,
) {
    val source = remember { MutableInteractionSource() }
    val view = LocalView.current
    ActionRow(title, summary, glyph, enabled, modifier = modifier
        .couiButtonFeedback(CouiTheme.isDark, source, enabled, list = true)
        .toggleable(checked, source, null, enabled, Role.Switch) { target ->
            view.performCouiHapticFeedback(CouiHapticFeedback.Switch, true)
            change(target)
        },
        trailing = {
            Switch(checked = checked, onCheckedChange = null, onDragCheckedChange = change,
                interactionSource = source, enabled = enabled)
        })
}

@Composable
internal fun <T> ChoiceRow(
    title: String, selected: T?, options: List<Pair<T, String>>, summary: String? = null, glyph: Glyph? = null,
    modifier: Modifier = Modifier, enabled: Boolean = true, change: (T) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    val anchor = rememberCouiMenuAnchor()
    val source = remember { MutableInteractionSource() }
    val canChoose = enabled && options.isNotEmpty()
    val value = options.firstOrNull { it.first == selected }?.second
    val dark = CouiTheme.isDark

    Box(modifier) {
        ActionRow(
            title, summary, glyph, enabled = canChoose,
            modifier = Modifier.couiMenuAnchor(anchor)
                .couiButtonFeedback(dark, source, canChoose, list = true, hoverLocked = expanded)
                .clickable(source, null, canChoose, role = Role.Button) { anchor.open { expanded = true } }
                .semantics { stateDescription = if (expanded) "已展开" else "已收起" },
            trailing = {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (value != null) {
                        Text(text = value, kind = CouiTextKind.ItemValue, maxLines = 2,
                            overflow = TextOverflow.Ellipsis, textAlign = TextAlign.End,
                            color = CouiTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(start = 12.dp, end = 4.dp).widthIn(max = 116.dp))
                    }
                    Image(painterResource(com.itos.coui.R.drawable.coui_pop_up_next), null,
                        Modifier.size(width = 14.dp, height = 24.dp),
                        colorFilter = ColorFilter.tint(Color(if (dark) 0x4DFFFFFF else 0x42000000)))
                }
            },
        )
        ChoiceMenu(expanded && canChoose, { expanded = false }, selected, options, anchor) {
            expanded = false
            change(it)
        }
    }
}

// Mount the kit's popup motion and renderer on first use. Keep them composed on close
// so the component controls its exit and can reverse it if immediately reopened.
@Composable
private fun hasBeenShown(visible: Boolean): Boolean {
    var opened by remember { mutableStateOf(false) }
    if (visible && !opened) SideEffect { opened = true }
    return visible || opened
}

@Composable
internal fun <T> ChoiceMenu(
    expanded: Boolean, dismiss: () -> Unit, selection: T?, options: List<Pair<T, String>>,
    anchor: CouiMenuAnchor? = null, select: (T) -> Unit,
) {
    if (!hasBeenShown(expanded)) return
    val items = remember(options, selection) {
        options.map { (value, label) -> CouiMenuItem(value, label, selected = value == selection) }
    }
    Menu(
        modifier = Modifier.heightIn(max = 320.dp),
        expanded = expanded,
        onDismissRequest = dismiss,
        items = items,
        onItemClick = { select(it.value) },
        anchor = anchor,
    )
}

@Composable
internal fun NumberedChips(
    title: String, options: List<Int>, selected: Int?, summary: String? = null,
    label: (Int) -> String = Int::toString, select: (Int) -> Unit,
) {
    ActionRow(title, summary)
    if (options.isEmpty()) return

    val choices = remember(options) { options.distinct() }
    val colors = CouiTheme.colorScheme
    val selectedIndex = choices.indexOfFirst { it == selected }
    val list = rememberLazyListState(initialFirstVisibleItemIndex = selectedIndex.coerceAtLeast(0))
    LaunchedEffect(selectedIndex) {
        if (selectedIndex < 0) return@LaunchedEffect
        val viewport = list.layoutInfo
        val item = viewport.visibleItemsInfo.firstOrNull { it.index == selectedIndex }
        if (item == null) {
            if (list.firstVisibleItemIndex != selectedIndex) list.animateScrollToItem(selectedIndex)
        } else {
            val delta = when {
                item.offset < viewport.viewportStartOffset -> item.offset - viewport.viewportStartOffset
                item.offset + item.size > viewport.viewportEndOffset -> item.offset + item.size - viewport.viewportEndOffset
                else -> 0
            }
            if (delta != 0) list.animateScrollBy(delta.toFloat())
        }
    }
    Box(Modifier.fillMaxWidth()) {
        val backdrop = if (Build.VERSION.SDK_INT >= 34) rememberCouiBackdrop() else null
        if (backdrop != null) Box(Modifier.matchParentSize().couiBackdrop(backdrop).background(colors.card))
        LazyRow(
            modifier = Modifier.fillMaxWidth().selectableGroup(),
            state = list,
            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            items(choices, key = { it }) { option ->
                val checked = option == selected
                if (Build.VERSION.SDK_INT >= 34 && backdrop != null) {
                    ChipTab(selected = checked, onClick = { select(option) }, label = label(option), backdrop = backdrop)
                } else {
                    val source = remember { MutableInteractionSource() }
                    Box(
                        Modifier.defaultMinSize(minWidth = 42.dp, minHeight = 40.dp).clip(CouiCapsuleShape)
                            .background(if (checked) colors.accent else colors.onSurface.copy(alpha = .08f))
                            .couiButtonFeedback(interactionSource = source)
                            .selectable(checked, source, null, role = Role.Tab) { select(option) }
                            .padding(horizontal = 12.dp),
                        contentAlignment = Alignment.Center,
                    ) {
                        Text(text = label(option), kind = CouiTextKind.ItemValue,
                            color = if (checked) colors.onAccent else colors.onSurface)
                    }
                }
            }
        }
    }
}

@Composable
internal fun ClientRow(state: AppState, switchable: Boolean = true, showIcon: Boolean = true) {
    val icon = Glyph.Grid.takeIf { showIcon }
    Group {
        if (switchable) ChoiceRow("游戏版本", state.game, state.clientChoices().map { it to serverName(it) },
            glyph = icon, enabled = state.libraryReady && state.busy == null) { state.chooseClient(it) }
        else ActionRow("游戏版本", serverName(state.game), icon)
    }
}

@Composable
internal fun SearchControl(query: String, change: (String) -> Unit, placeholder: String, initiallyExpanded: Boolean = false, exit: (() -> Unit)? = null) {
    var expanded by rememberSaveable { mutableStateOf(initiallyExpanded) }
    BackHandler(expanded) { expanded = false; change(""); exit?.invoke() }
    SearchBar(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp),
        state = rememberCouiSearchBarState(expanded),
        query = query,
        onQueryChange = change,
        onExpandedChange = {
            expanded = it
            if (!it) { change(""); exit?.invoke() }
        },
        placeholder = placeholder,
        cancelText = "取消",
        clearDescription = "清除搜索",
    )
}

@Composable
internal fun EditableRow(title: String, value: String, summary: String? = null, change: (String) -> Unit) {
    var editing by remember { mutableStateOf(false) }
    var draft by remember { mutableStateOf(TextFieldValue()) }
    ActionRow(title, listOfNotNull(value.takeIf(String::isNotBlank), summary).joinToString("\n"),
        Glyph.Edit, showNext = true) { draft = TextFieldValue(value); editing = true }
    if (!hasBeenShown(editing)) return
    InputDialog(visible = editing, material = dialogMaterial(), value = draft, onValueChange = { draft = it },
        title = title, placeholder = title, clearContentDescription = "清除内容",
        onDismissRequest = { editing = false },
        confirmAction = primaryDialogAction("保存") { editing = false; change(draft.text) },
        dismissAction = CouiDialogAction("取消", { editing = false }))
}

@Composable
private fun dialogMaterial() =
    if (CouiDialogDefaults.isMaterialSupported()) CouiDialogDefaults.material(CouiTheme.isDark) else null

@Composable
private fun primaryDialogAction(label: String, click: () -> Unit) = CouiDialogAction(
    text = label,
    onClick = click,
    style = CouiDialogActionStyle.Primary,
    colors = CouiDialogDefaults.primaryActionColors(CouiTheme.colorScheme.accent),
)

@Composable
internal fun AppDialogs(state: AppState) {
    val currentPrompt = state.prompt
    var lastPrompt by remember { mutableStateOf<Prompt?>(null) }
    SideEffect { if (currentPrompt != null) lastPrompt = currentPrompt }
    (currentPrompt ?: lastPrompt)?.let { prompt ->
        val close = { if (state.prompt === prompt) state.prompt = null }
        val confirm = { if (state.prompt === prompt) { close(); prompt.onConfirm() } }
        val colors = CouiDialogDefaults.themeColors()
        val ink = CouiTheme.colorScheme.onSurface
        Dialog(
            visible = currentPrompt != null, material = dialogMaterial(), title = prompt.title,
            onDismissRequest = close,
            confirmAction = if (prompt.destructive) CouiDialogAction(
                text = prompt.confirm,
                onClick = confirm,
                colors = CouiDialogActionColors(
                    surface = ink.copy(alpha = .08f), content = colors.destructive,
                    disabledSurface = ink.copy(alpha = .04f), disabledContent = colors.disabledAction,
                ),
            ) else primaryDialogAction(prompt.confirm, confirm),
            dismissAction = prompt.dismiss?.let { CouiDialogAction(it, close) },
            neutralAction = prompt.neutral?.let {
                CouiDialogAction(
                    text = it,
                    onClick = { if (state.prompt === prompt) { close(); prompt.onNeutral() } },
                    colors = if (prompt.emphasizeNeutral) CouiDialogDefaults.primaryActionColors(Color(0xFF484A51)) else null,
                )
            },
        ) {
            DisposableEffect(prompt) {
                onDispose { if (state.prompt == null && lastPrompt === prompt) lastPrompt = null }
            }
            // Keep long logs and conversion records selectable inside the kit's scrolling dialog.
            androidx.compose.foundation.text.selection.SelectionContainer {
                Text(text = prompt.text, kind = CouiTextKind.ItemSummary, modifier = Modifier.fillMaxWidth()
                    .padding(horizontal = CouiDialogDefaults.contentPadding).padding(bottom = CouiDialogDefaults.messageBottom))
            }
        }
    }
    val name = state.renaming
    var renameDraft by remember { mutableStateOf(TextFieldValue()) }
    var editingName by remember { mutableStateOf<String?>(null) }
    if (editingName != name) {
        editingName = name
        if (name != null) renameDraft = TextFieldValue(state.label(name))
    }
    val closeRename = { state.renaming = null }
    if (hasBeenShown(name != null)) InputDialog(visible = name != null, material = dialogMaterial(), value = renameDraft,
        onValueChange = { if (it.text.length <= Labels.MAX) renameDraft = it.copy(text = it.text.replace('\n', ' ')) },
        title = "重命名", placeholder = "名称", clearContentDescription = "清除名称", onDismissRequest = closeRename,
        confirmAction = primaryDialogAction("保存") { if (name != null) { closeRename(); state.rename(name, renameDraft.text) } },
        dismissAction = CouiDialogAction("取消", closeRename))
    val busyMessage = state.busy
    var lastBusyMessage by remember { mutableStateOf("正在处理…") }
    SideEffect { if (busyMessage != null) lastBusyMessage = busyMessage }
    if (hasBeenShown(busyMessage != null)) LoadingDialog(visible = busyMessage != null, material = dialogMaterial(), message = busyMessage ?: lastBusyMessage)
    val notice = state.notice
    var lastNotice by remember { mutableStateOf("") }
    SideEffect { if (notice != null) lastNotice = notice }
    val closeNotice = { state.notice = null }
    val showingNotice = currentPrompt == null && name == null && busyMessage == null && notice != null
    if (hasBeenShown(showingNotice)) Dialog(visible = showingNotice,
        material = dialogMaterial(), message = notice ?: lastNotice, onDismissRequest = closeNotice,
        confirmAction = CouiDialogAction("确定", closeNotice))
}

@Composable
internal fun BottomAction(title: String, enabled: Boolean = true, click: () -> Unit) {
    Group(Modifier.padding(bottom = 8.dp)) {
        Item(onClick = click, enabled = enabled) {
            Text(text = title, kind = CouiTextKind.ItemTitle, textAlign = TextAlign.Center,
                color = if (enabled) CouiTheme.colorScheme.accentOnSurface else CouiTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.fillMaxWidth())
        }
    }
}
