package com.openwave.tinh

import android.app.Application
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.provider.Settings

/**
 * Reports Android's running applications to OpenWave, so Ocean can show one
 * card per running Android app. Android offers an ordinary app no way to
 * call the host; like MainActivity.reportIdle this writes Settings.Global
 * values that OpenWave reads through Waydroid's platform service:
 *
 *   openwave_android_tasks      package names with a running task, most
 *                               recent first, comma separated
 *   openwave_android_tasks_seq  a non-zero stamp that changes with the list
 *
 * The list is read once a second. Nothing here grants or changes anything.
 */
class TinhApplication : Application() {
    private var reported: String? = null
    private val handlers by lazy { HandlerReporter(this) }

    override fun onCreate() {
        super.onCreate()
        handlers.start()
        val thread = HandlerThread("openwave-tasks").apply { start() }
        val handler = Handler(thread.looper)
        handler.post(object : Runnable {
            override fun run() {
                report()
                handler.postDelayed(this, POLL_MS)
            }
        })
    }

    private fun report() {
        val list = RunningApps.packages(this).joinToString(",")
        if (list == reported) return
        try {
            Settings.Global.putString(contentResolver, TASKS_SETTING, list)
            val stamp = ((SystemClock.elapsedRealtime() / 100).toInt() and 0x3fffffff) or 1
            Settings.Global.putInt(contentResolver, TASKS_SEQ_SETTING, stamp)
            reported = list
        } catch (_: SecurityException) {
            // Not privileged: OpenWave is simply not told.
        }
    }

    private companion object {
        const val POLL_MS = 1000L
        const val TASKS_SETTING = "openwave_android_tasks"
        const val TASKS_SEQ_SETTING = "openwave_android_tasks_seq"
    }
}
