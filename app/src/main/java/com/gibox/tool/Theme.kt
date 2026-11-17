package com.gibox.tool

import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.material3.Surface
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.clickable
import androidx.compose.foundation.BorderStroke
import androidx.compose.animation.core.tween
import androidx.compose.animation.togetherWith
import androidx.compose.animation.fadeOut
import androidx.compose.animation.fadeIn
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibilityScope
import androidx.compose.animation.BoundsTransform
import androidx.compose.animation.EnterExitState
import androidx.compose.animation.SharedTransitionLayout
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.foundation.background
import androidx.compose.animation.ContentTransform
import androidx.compose.animation.SharedTransitionScope
import androidx.activity.compose.BackHandler
import android.os.Build
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBarColors
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.DialogProperties

/** One UI Settings palette: neutral surfaces, blue actions, generous type and rounded groups. */
@Composable
fun GIBoxTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    val colors = if (dark) darkColorScheme(
        primary=Color(0xFF8AB4FF), onPrimary=Color(0xFF101010), primaryContainer=Color(0xFF243951),
        background=Color(0xFF000000), surface=Color(0xFF17171A), onSurface=Color(0xFFF5F5F5),
        onBackground=Color(0xFFF5F5F5), onSurfaceVariant=Color(0xFFABABB0), outline=Color(0xFF66666B), outlineVariant=Color(0xFF303034),
        surfaceContainerLowest=Color(0xFF17171A), surfaceContainer=Color(0xFF17171A), surfaceContainerLow=Color(0xFF1C1C20), surfaceContainerHigh=Color(0xFF29292E),
    ) else lightColorScheme(
        primary=Color(0xFF2866C9), onPrimary=Color.White, primaryContainer=Color(0xFFEAF1FC),
        background=Color(0xFFF4F4F7), surface=Color.White, onSurface=Color(0xFF171719), onBackground=Color(0xFF171719),
        onSurfaceVariant=Color(0xFF74747B), outline=Color(0xFF98989F), outlineVariant=Color(0xFFE8E8EC),
        surfaceContainerLowest=Color.White, surfaceContainer=Color(0xFFF4F4F7), surfaceContainerLow=Color(0xFFF5F5F8), surfaceContainerHigh=Color(0xFFEEEEF2),
    )
    MaterialTheme(colorScheme=colors, shapes=androidx.compose.material3.Shapes(
        small=androidx.compose.foundation.shape.RoundedCornerShape(12.dp), medium=androidx.compose.foundation.shape.RoundedCornerShape(18.dp),
        large=androidx.compose.foundation.shape.RoundedCornerShape(24.dp), extraLarge=androidx.compose.foundation.shape.RoundedCornerShape(28.dp)),
        typography=androidx.compose.material3.Typography(
            headlineLarge=androidx.compose.ui.text.TextStyle(fontSize=34.sp,fontWeight=androidx.compose.ui.text.font.FontWeight.Normal,letterSpacing=(-.7).sp),
            titleMedium=androidx.compose.ui.text.TextStyle(fontSize=18.sp,fontWeight=androidx.compose.ui.text.font.FontWeight.Medium),
            bodyLarge=androidx.compose.ui.text.TextStyle(fontSize=18.sp,lineHeight=25.sp),
            bodySmall=androidx.compose.ui.text.TextStyle(fontSize=13.sp,lineHeight=19.sp),
        ),content=content)
}

/** Top bars in the page's tint, scrolled or not (GIBoxTheme). */
@Composable
fun pageBarColors(): TopAppBarColors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.background, scrolledContainerColor = MaterialTheme.colorScheme.background)

@Composable
fun BackButton(onClick: () -> Unit) {
    IconButton(onClick = onClick, modifier = Modifier.semantics { contentDescription = "返回" }) { OneIcon(OneGlyph.Arrow, Modifier.rotate(180f)) }
}

/** Messages and questions (Prompt), on top of every page. */
@Composable
fun PromptDialog(state: AppState) {
    val p = state.prompt ?: return
    val close = { state.prompt = null }
    OneUiDialog(
        onDismissRequest = close,
        title = { Text(p.title) },
        text = {
            SelectionContainer {
                Text(p.text, modifier = Modifier.verticalScroll(rememberScrollState()), style = MaterialTheme.typography.bodyMedium)
            }
        },
        confirmButton = {
            TextButton(
                onClick = { close(); p.onConfirm() },
                colors = if (p.destructive) ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error) else ButtonDefaults.textButtonColors(),
            ) { Text(p.confirm) }
        },
        dismissButton = if (p.dismiss == null && p.neutral == null) null else {
            {
                Row {
                    if (p.neutral != null) TextButton(onClick = { close(); p.onNeutral() }) { Text(p.neutral) }
                    if (p.dismiss != null) TextButton(onClick = close) { Text(p.dismiss) }
                }
            }
        },
    )
}

/** The name shown for a mod (AppState.renaming); blank goes back to the Mods entry name. */
@Composable
fun RenameDialog(state: AppState) {
    val name = state.renaming ?: return
    var text by remember(name) { mutableStateOf(state.label(name)) }
    val close = { state.renaming = null }
    OneUiDialog(
        onDismissRequest = close,
        title = { Text("重命名") },
        text = {
            OneUiTextField(
                value = text,
                onValueChange = { if (it.length <= Labels.MAX) text = it.replace("\n", " ") },
                singleLine = true,
                label = { Text("名称") },
            )
        },
        confirmButton = { TextButton(onClick = { close(); state.rename(name, text) }) { Text("保存") } },
        dismissButton = { TextButton(onClick = close) { Text("取消") } },
    )
}

/** Progress of background work: an expressive loading indicator that cannot be dismissed. */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun BusyDialog(state: AppState) {
    val b = state.busy ?: return
    OneUiDialog(
        onDismissRequest = {},
        properties = DialogProperties(dismissOnBackPress = false, dismissOnClickOutside = false),
        icon = { androidx.compose.material3.CircularProgressIndicator(modifier = Modifier.size(32.dp), strokeWidth = 3.dp) },
        title = { Text(b.title) },
        text = { Text(b.text, style = MaterialTheme.typography.bodyMedium) },
        confirmButton = {},
    )
}

/** The title of a SettingsGroup (the group adds the padding). */
@Composable
fun SectionTitle(text: String) {
    Text(text, style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

/** A full-width row for an empty list. */
@Composable
fun EmptyNote(text: String) {
    Column(Modifier.fillMaxWidth().heightIn(min = 96.dp).padding(24.dp), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(text, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Spacer(Modifier.size(4.dp))
    }
}

/** A rounded single-line search field with a magnifier and a clear button. */
@Composable
fun SearchField(value: String, onValueChange: (String) -> Unit, placeholder: String, modifier: Modifier = Modifier) {
    OneUiTextField(
        value = value,
        onValueChange = onValueChange,
        modifier = modifier.fillMaxWidth(),
        singleLine = true,
        shape = MaterialTheme.shapes.extraLarge,
        placeholder = { Text(placeholder) },
        leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
        trailingIcon = { if (value.isNotEmpty()) IconButton(onClick = { onValueChange("") }) { Icon(Icons.Filled.Close, contentDescription = "清除") } },
    )
}

/** What a page shows before searching: a field-shaped button; tapping it opens the SearchPage. */
@Composable
fun SearchBox(placeholder: String, onClick: () -> Unit, modifier: Modifier = Modifier) {
    val shape = MaterialTheme.shapes.extraLarge
    Surface(
        modifier = modifier.fillMaxWidth().heightIn(min = 56.dp).clip(shape).clickable(onClick = onClick),
        shape = shape,
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
    ) {
        Row(Modifier.padding(horizontal = 16.dp, vertical = 16.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Filled.Search, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.width(12.dp))
            Text(placeholder, style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

/**
 * Search: the page fades to just the field at the top (focused, keyboard up), the results under it. Back (the arrow or
 * the system) closes it. The caller fades between its page and this one.
 */
@Composable
fun SearchPage(query: String, onQuery: (String) -> Unit, placeholder: String, onClose: () -> Unit, field: Modifier = Modifier, entered: Boolean = true, results: LazyListScope.() -> Unit) {
    val focus = remember { FocusRequester() }
    // The keyboard only once the field has arrived: opening it mid-move relays out both pages on every frame.
    LaunchedEffect(entered) { if (entered) focus.requestFocus() }
    BackHandler(onBack = onClose)
    OneUiScaffold { padding ->
        Column(Modifier.fillMaxSize().padding(top = padding.calculateTopPadding() + 8.dp)) {
            Row(Modifier.padding(start = 4.dp, end = 16.dp), verticalAlignment = Alignment.CenterVertically) {
                BackButton(onClose)
                SearchField(query, onQuery, placeholder, Modifier.weight(1f).then(field).focusRequester(focus))
            }
            LazyColumn(
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 16.dp, bottom = padding.calculateBottomPadding() + 24.dp),
                verticalArrangement = Arrangement.spacedBy(OneUiList.SegmentedGap),
                content = results,
            )
        }
    }
}

/** The fade between a page and its SearchPage: the page goes quickly, the search page comes in behind the moving field. */
fun searchFade(): ContentTransform = fadeIn(tween(220, delayMillis = 60)) togetherWith fadeOut(tween(120))

/**
 * The search box (or button) on a page and the field at the top of its SearchPage as one shared element: it moves and
 * stretches up to the top while the rest of the page fades (and back). The content is remeasured, not scaled.
 */
@Composable
fun SharedTransitionScope.searchBounds(scope: AnimatedVisibilityScope): Modifier =
    Modifier.sharedBounds(
        rememberSharedContentState("search"), scope,
        boundsTransform = BoundsTransform { _, _ -> tween(280, easing = FastOutSlowInEasing) },
        resizeMode = SharedTransitionScope.ResizeMode.RemeasureToBounds,
    )

/** A page and its SearchPage, cross-faded on the page colour (never the window's white) with the field shared. */
@Composable
fun SearchSwitch(searching: Boolean, content: @Composable SharedTransitionScope.(open: Boolean, field: Modifier, entered: Boolean) -> Unit) {
    SharedTransitionLayout(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background)) {
        AnimatedContent(targetState = searching, transitionSpec = { searchFade() }, label = "search") { open ->
            val entered = transition.currentState == EnterExitState.Visible && transition.targetState == EnterExitState.Visible
            this@SharedTransitionLayout.content(open, searchBounds(this), entered)
        }
    }
}
