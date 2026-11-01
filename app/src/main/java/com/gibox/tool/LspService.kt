package com.gibox.tool

import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import io.github.libxposed.service.XposedService
import io.github.libxposed.service.XposedServiceHelper
import java.util.concurrent.atomic.AtomicBoolean

/**
 * The modern Xposed service (libxposed): a framework that loaded GIBox's modern entry (ModernEntry) hands it to the app
 * (XposedProvider, from the library's manifest). Through it the app reads the scope the user ticked in LSPosed and the
 * framework's name; without it (a legacy framework) both stay null and the app falls back to its own marks.
 */
object LspService {
    var scope by mutableStateOf<List<String>?>(null)
        private set
    var framework by mutableStateOf<String?>(null)
        private set
    private val started = AtomicBoolean(false)
    private val main = Handler(Looper.getMainLooper())

    /** Once per process (the helper takes one listener); a service that arrived before is delivered at once. */
    fun start() {
        if (!started.compareAndSet(false, true)) return
        XposedServiceHelper.registerListener(object : XposedServiceHelper.OnServiceListener {
            override fun onServiceBind(service: XposedService) {
                val s = runCatching { service.scope.toList() }.getOrNull()
                val f = runCatching { "${service.frameworkName} ${service.frameworkVersion}" }.getOrNull()
                main.post { scope = s; framework = f }
            }
            override fun onServiceDied(service: XposedService) {
                main.post { scope = null; framework = null }
            }
        })
    }
}
