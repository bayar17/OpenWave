package com.openwave.tinh

import android.app.ActivityManager
import android.os.Bundle
import android.os.SystemClock
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.sp

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        setContent {
            EmptyScreen()
        }
    }

    override fun onResume() {
        super.onResume()
        if (!returnToRunningApp()) reportIdle(true)
    }

    override fun onPause() {
        reportIdle(false)
        super.onPause()
    }

    private fun reportIdle(idle: Boolean) {
        val value = if (idle) ((SystemClock.elapsedRealtime() / 100).toInt() and 0x3fffffff) or 1 else 0
        if (value == 0 && lastIdle == 0) return
        try {
            Settings.Global.putInt(contentResolver, IDLE_SETTING, value)
            lastIdle = value
        } catch (_: SecurityException) {
            // Not privileged: OpenWave is simply not told.
        }
    }

    private var lastIdle = -1

    /** True if a running app's task was moved back to the front. */
    private fun returnToRunningApp(): Boolean {
        val manager = getSystemService(ActivityManager::class.java) ?: return false
        val target = RunningApps.tasks(this).firstOrNull { it.taskId != taskId } ?: return false
        try {
            manager.moveTaskToFront(target.taskId, 0)
        } catch (_: SecurityException) {
            return false
        }
        @Suppress("DEPRECATION")
        overridePendingTransition(0, 0)
        return true
    }

    private companion object {
        const val IDLE_SETTING = "openwave_android_idle"
    }
}

@Composable
fun EmptyScreen() {
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = "There is nothing here...",
            color = Color.White,
            fontSize = 18.sp,
            fontWeight = FontWeight.Normal,
            textAlign = TextAlign.Center
        )
    }
}

