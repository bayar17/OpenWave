package com.openwave.tinh

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.provider.Settings

class HandlerReporter(private val context: Context) {
    private var thread: HandlerThread? = null
    private var handler: Handler? = null
    private var reported: String? = null
    private var reportedRemovable: String? = null

    fun start() {
        val worker = HandlerThread("openwave-handlers").apply { start() }
        val queue = Handler(worker.looper)
        thread = worker
        handler = queue
        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_PACKAGE_ADDED)
            addAction(Intent.ACTION_PACKAGE_REMOVED)
            addAction(Intent.ACTION_PACKAGE_REPLACED)
            addAction(Intent.ACTION_PACKAGE_CHANGED)
            addDataScheme("package")
        }
        context.registerReceiver(object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) = schedule(DEBOUNCE_MS)
        }, filter, null, queue, Context.RECEIVER_NOT_EXPORTED)
        schedule(STARTUP_MS)
    }

    private fun schedule(delay: Long) {
        val queue = handler ?: return
        queue.removeCallbacks(publish)
        queue.postDelayed(publish, delay)
    }

    private val publish = Runnable {
        val text = HandlerProbe.encode(HandlerProbe.handlers(context))
        val removable = Removable.encode(Removable.packages(context))
        if (text != reported || removable != reportedRemovable) {
            try {
                Settings.Global.putString(context.contentResolver, HANDLERS_SETTING, text)
                Settings.Global.putString(context.contentResolver, REMOVABLE_SETTING, removable)
                val stamp = ((SystemClock.elapsedRealtime() / 100).toInt() and 0x3fffffff) or 1
                Settings.Global.putInt(context.contentResolver, HANDLERS_SEQ_SETTING, stamp)
                reported = text
                reportedRemovable = removable
            } catch (_: SecurityException) {
            }
        }
    }

    private companion object {
        const val DEBOUNCE_MS = 2000L
        const val STARTUP_MS = 5000L
        const val HANDLERS_SETTING = "openwave_android_handlers"
        const val REMOVABLE_SETTING = "openwave_android_removable"
        const val HANDLERS_SEQ_SETTING = "openwave_android_handlers_seq"
    }
}
