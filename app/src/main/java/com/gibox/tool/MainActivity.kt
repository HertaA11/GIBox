package com.gibox.tool

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.ui.Modifier
import androidx.compose.material3.MaterialTheme
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.background
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect

/** GIBox: the module's app. Pages are a simple back stack in AppState; every page is a full screen. */
class MainActivity : ComponentActivity() {
    private lateinit var state: AppState

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        LspService.start()
        state = AppState(this)
        setContent { GIBoxTheme { GIBoxApp(state) } }
    }

    override fun onResume() {
        super.onResume()
        if (::state.isInitialized) state.refresh()
    }
}

/** Archives only: the system file manager offers .zip files and nothing else. */
private val ZIP_TYPES = arrayOf("application/zip", "application/x-zip-compressed", "application/x-zip")

@Composable
fun GIBoxApp(state: AppState) {
    // The system file manager (Storage Access Framework), .zip only.
    val zip = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> if (uri != null) state.importFrom(uri) }
    BackHandler(enabled = state.stack.size > 1) { state.back() }
    // The framework's service can arrive after the first page: the scope it brings picks the game and the module state.
    LaunchedEffect(LspService.scope) { state.refresh() }
    // The page colour under every page: while two pages cross-fade, the window's own (white) background never shows.
    AnimatedContent(targetState = state.stack.last(), label = "page", modifier = Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background)) { screen ->
        when (screen) {
            Screen.Home -> HomeScreen(state, pickZip = { zip.launch(ZIP_TYPES) })
            Screen.Settings -> SettingsScreen(state)
            Screen.Log -> LogScreen(state)
            Screen.Manual -> ManualScreen(state)
            Screen.Debug -> DebugScreen(state)
            is Screen.Character -> CharacterScreen(state, screen.id)
            is Screen.Detail -> DetailScreen(state, screen.name)
            is Screen.Pick -> PickScreen(state, screen)
        }
    }
    PromptDialog(state)
    RenameDialog(state)
    BusyDialog(state)
}
