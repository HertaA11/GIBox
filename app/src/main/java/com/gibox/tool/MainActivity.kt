package com.gibox.tool

import android.content.Intent
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.withResumed
import com.itos.coui.menu.ProvideCouiMenuBackdrop
import com.itos.coui.theme.CouiTheme
import kotlinx.coroutines.launch

private const val PAGE = "com.gibox.tool.PAGE"
private const val TARGET = "com.gibox.tool.TARGET"
private val zipTypes = arrayOf("application/zip", "application/x-zip-compressed", "application/x-zip")

class MainActivity : PageActivity() {
    // The exported launcher always opens Home, including for externally supplied intents.
    override val page: Screen get() = Screen.Home
}

open class PageActivity : ComponentActivity() {
    @Suppress("DEPRECATION")
    internal open val page: Screen by lazy {
        if (Build.VERSION.SDK_INT >= 33) requireNotNull(intent.getSerializableExtra(PAGE, Screen::class.java))
        else intent.getSerializableExtra(PAGE) as Screen
    }
    private lateinit var state: AppState
    private var resumed by mutableStateOf(false)
    private var openingPage = false
    private var pendingHandled = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        LspService.start()
        state = AppState.get(applicationContext)
        val screen = page
        val target = if (screen == Screen.Home) null else intent.getStringExtra(TARGET)
        setContent {
            CouiTheme {
                val client = when (screen) {
                    Screen.Settings, Screen.Accessibility, Screen.Log, Screen.Manual,
                    is Screen.Detail, is Screen.Pick -> state.game
                    else -> null
                }
                ProvideCouiMenuBackdrop(isolateContent = false) { windowSource ->
                    Box(Modifier.fillMaxSize().then(windowSource)) {
                        key(client) {
                            when (screen) {
                                Screen.Home -> HomeScreen(state)
                                Screen.FeatureSearch -> FeatureSearchScreen(state, state::openFeature)
                                Screen.Accessibility -> AccessibilityScreen(state, target)
                                Screen.Settings -> SettingsScreen(state, target)
                                Screen.About -> AboutScreen(state)
                                Screen.Mods -> {
                                    val zip = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
                                        if (uri != null) state.importFrom(uri)
                                    }
                                    ModManagementScreen(state) { zip.launch(zipTypes) }
                                }
                                Screen.Log -> LogScreen(state)
                                Screen.Manual -> ManualScreen(state)
                                is Screen.Character -> CharacterScreen(state, screen.id, resumed)
                                is Screen.Detail -> DetailScreen(state, screen.name, resumed)
                                is Screen.Pick -> PickScreen(state, screen)
                            }
                        }
                    }
                    if (resumed) AppDialogs(state)
                }
            }
        }
    }

    internal fun openPage(screen: Screen, target: String? = null, replace: Boolean = false) {
        lifecycleScope.launch {
            // An import can finish while the file picker or another app is still in front.
            lifecycle.withResumed {
                if (!openingPage && !isFinishing) {
                    openingPage = true
                    startActivity(Intent(this@PageActivity, PageActivity::class.java)
                        .putExtra(PAGE, screen).putExtra(TARGET, target))
                    if (replace) finish()
                }
            }
        }
    }

    internal fun finishConfirmedPick() {
        pendingHandled = true
        finish()
    }

    private fun discardUnconfirmedPick() {
        if (pendingHandled) return
        pendingHandled = true
        (page as? Screen.Pick)?.let(state::discardPending)
    }

    override fun finish() {
        if (::state.isInitialized) discardUnconfirmedPick()
        super.finish()
    }

    override fun onStart() {
        super.onStart()
        state.pageStarted()
    }

    override fun onResume() {
        super.onResume()
        openingPage = false
        resumed = true
        state.attach(this)
    }

    override fun onPause() {
        resumed = false
        super.onPause()
    }

    override fun onStop() {
        state.pageStopped()
        super.onStop()
    }

    override fun onDestroy() {
        if (isFinishing) discardUnconfirmedPick()
        state.detach(this)
        super.onDestroy()
    }
}
