package com.gibox.tool

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
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ContainedLoadingIndicator
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MotionScheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
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
import androidx.compose.ui.window.DialogProperties

/** GIBox's own blue (light and dark), for systems without wallpaper colours (Monet, Android 12+). */
private val BlueLight = lightColorScheme(
    primary = Color(0xFF1F6A97), onPrimary = Color(0xFFFFFFFF), primaryContainer = Color(0xFFCDE5FF), onPrimaryContainer = Color(0xFF001D32),
    secondary = Color(0xFF51606F), onSecondary = Color(0xFFFFFFFF), secondaryContainer = Color(0xFFD4E4F6), onSecondaryContainer = Color(0xFF0D1D2A),
    tertiary = Color(0xFF67587A), onTertiary = Color(0xFFFFFFFF), tertiaryContainer = Color(0xFFEEDCFF), onTertiaryContainer = Color(0xFF221533),
    error = Color(0xFFBA1A1A), onError = Color(0xFFFFFFFF), errorContainer = Color(0xFFFFDAD6), onErrorContainer = Color(0xFF410002),
    background = Color(0xFFF7F9FF), onBackground = Color(0xFF181C20), surface = Color(0xFFF7F9FF), onSurface = Color(0xFF181C20),
    surfaceVariant = Color(0xFFDEE3EB), onSurfaceVariant = Color(0xFF42474E), outline = Color(0xFF72777F), outlineVariant = Color(0xFFC2C7CF),
    surfaceContainerLowest = Color(0xFFFFFFFF), surfaceContainerLow = Color(0xFFF1F4F9), surfaceContainer = Color(0xFFEBEEF3),
    surfaceContainerHigh = Color(0xFFE5E8EE), surfaceContainerHighest = Color(0xFFE0E2E8),
)
private val BlueDark = darkColorScheme(
    primary = Color(0xFF93CCFF), onPrimary = Color(0xFF003352), primaryContainer = Color(0xFF004B74), onPrimaryContainer = Color(0xFFCDE5FF),
    secondary = Color(0xFFB9C8DA), onSecondary = Color(0xFF233240), secondaryContainer = Color(0xFF3A4857), onSecondaryContainer = Color(0xFFD4E4F6),
    tertiary = Color(0xFFD3BFE6), onTertiary = Color(0xFF372A4A), tertiaryContainer = Color(0xFF4F4061), onTertiaryContainer = Color(0xFFEEDCFF),
    error = Color(0xFFFFB4AB), onError = Color(0xFF690005), errorContainer = Color(0xFF93000A), onErrorContainer = Color(0xFFFFDAD6),
    background = Color(0xFF101418), onBackground = Color(0xFFE0E2E8), surface = Color(0xFF101418), onSurface = Color(0xFFE0E2E8),
    surfaceVariant = Color(0xFF42474E), onSurfaceVariant = Color(0xFFC2C7CF), outline = Color(0xFF8C9199), outlineVariant = Color(0xFF42474E),
    surfaceContainerLowest = Color(0xFF0B0F13), surfaceContainerLow = Color(0xFF181C20), surfaceContainer = Color(0xFF1C2024),
    surfaceContainerHigh = Color(0xFF272A2F), surfaceContainerHighest = Color(0xFF31353A),
)

/** Material 3 Expressive: wallpaper colours (Monet) on Android 12+, otherwise GIBox's blue; light / dark from the system. */
@Composable
fun GIBoxTheme(content: @Composable () -> Unit) {
    val context = LocalContext.current
    val dark = isSystemInDarkTheme()
    val colors = when {
        Build.VERSION.SDK_INT >= 31 -> runCatching { if (dark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context) }.getOrElse { if (dark) BlueDark else BlueLight }
        dark -> BlueDark
        else -> BlueLight
    }
    MaterialExpressiveTheme(colorScheme = colors, motionScheme = MotionScheme.expressive(), content = content)
}

@Composable
fun BackButton(onClick: () -> Unit) {
    IconButton(onClick = onClick) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "返回") }
}

/** Messages and questions (Prompt), on top of every page. */
@Composable
fun PromptDialog(state: AppState) {
    val p = state.prompt ?: return
    val close = { state.prompt = null }
    AlertDialog(
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
    AlertDialog(
        onDismissRequest = close,
        title = { Text("重命名") },
        text = {
            OutlinedTextField(
                value = text,
                onValueChange = { if (it.length <= Labels.MAX) text = it.replace("\n", " ") },
                singleLine = true,
                label = { Text("名称") },
                supportingText = { Text("只改 GIBox 里显示的名字；留空恢复为「$name」") },
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
    AlertDialog(
        onDismissRequest = {},
        properties = DialogProperties(dismissOnBackPress = false, dismissOnClickOutside = false),
        icon = { ContainedLoadingIndicator(modifier = Modifier.size(56.dp)) },
        title = { Text(b.title) },
        text = { Text(b.text, style = MaterialTheme.typography.bodyMedium) },
        confirmButton = {},
    )
}

/** The title of a SettingsGroup (the group adds the padding). */
@Composable
fun SectionTitle(text: String) {
    Text(text, style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary)
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
    OutlinedTextField(
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
