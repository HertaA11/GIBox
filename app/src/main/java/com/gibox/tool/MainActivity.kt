package com.gibox.tool

import android.os.Bundle
import android.view.View
import androidx.activity.OnBackPressedCallback
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch

/** SESL hosts the preference pages; the existing MOD workflows use Compose interop. */
class MainActivity : AppCompatActivity() {
    internal lateinit var state: AppState
        private set
    private lateinit var back: OnBackPressedCallback

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        LspService.start()
        state = AppState(this)
        setContentView(R.layout.activity_main)
        findViewById<ComposeView>(R.id.mod_content).setContent { GIBoxTheme { ModPages(state) } }
        findViewById<ComposeView>(R.id.dialog_content).setContent {
            GIBoxTheme {
                LaunchedEffect(LspService.scope) { state.refresh() }
                PromptDialog(state)
                RenameDialog(state)
                BusyDialog(state)
            }
        }
        back = object : OnBackPressedCallback(state.stack.size > 1) {
            override fun handleOnBackPressed() { state.back() }
        }
        onBackPressedDispatcher.addCallback(this, back)
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.RESUMED) {
                snapshotFlow { PageState(state.stack.last(), state.stack.size, state.game, state.revision, state.moduleActive, LspService.scope) }
                    .collect { renderPage(it.page) }
            }
        }
    }

    private fun renderPage(page: Screen) {
        back.isEnabled = state.stack.size > 1
        val native = page.isPreferencePage()
        findViewById<View>(R.id.mod_content).visibility = if (native) View.GONE else View.VISIBLE
        findViewById<View>(R.id.preferences_host).visibility = if (native) View.VISIBLE else View.GONE
        val existing = supportFragmentManager.findFragmentByTag("preferences") as? OneUiPreferencesFragment
        if (native) {
            val fragment = existing ?: OneUiPreferencesFragment().also {
                supportFragmentManager.beginTransaction().replace(R.id.preferences_host, it, "preferences").commitNow()
            }
            fragment.render(page)
        } else {
            existing?.dismissPreferenceDialog()
        }
    }

    override fun onResume() {
        super.onResume()
        if (::state.isInitialized) state.refresh()
    }

    private data class PageState(val page: Screen, val depth: Int, val game: String, val revision: Int, val active: Boolean, val scope: List<String>?)
}

internal fun Screen.isPreferencePage() = this == Screen.Home || this == Screen.Settings ||
    this == Screen.Accessibility || this == Screen.About || this == Screen.FeatureSearch

private val ZIP_TYPES = arrayOf("application/zip", "application/x-zip-compressed", "application/x-zip")

@Composable
private fun ModPages(state: AppState) {
    val zip = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> if (uri != null) state.importFrom(uri) }
    val screen = state.stack.last()
    CompositionLocalProvider(LocalServerState provides state) {
        androidx.compose.foundation.layout.Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background)) {
            when (screen) {
                Screen.Mods -> ModManagementScreen(state, pickZip = { zip.launch(ZIP_TYPES) })
                Screen.Log -> LogScreen(state)
                Screen.Manual -> ManualScreen(state)
                is Screen.Character -> CharacterScreen(state, screen.id)
                is Screen.Detail -> DetailScreen(state, screen.name)
                is Screen.Pick -> PickScreen(state, screen)
                else -> Unit
            }
        }
    }
}
