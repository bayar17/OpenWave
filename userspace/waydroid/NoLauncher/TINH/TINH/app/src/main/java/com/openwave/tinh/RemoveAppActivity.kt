package com.openwave.tinh

import android.app.Activity
import android.app.PendingIntent
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log

class RemoveAppActivity : Activity() {
    private val handler = Handler(Looper.getMainLooper())
    private var target: String? = null
    private var confirming = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val name = intent?.takeIf { it.action == ACTION_REMOVE_APP }
            ?.data?.takeIf { it.scheme == "package" }?.schemeSpecificPart
        if (name == null || name !in Removable.packages(this)) {
            Log.w(TAG, "$name is not removable")
            finish()
            return
        }
        current = this
        target = name
        handler.postDelayed({
            RemoveReport.publish(this, name, PackageInstaller.STATUS_FAILURE_ABORTED)
            finish()
        }, WAIT_MILLIS)
        request(name)
    }

    override fun onResume() {
        super.onResume()
        val name = target ?: return
        if (confirming) {
            handler.postDelayed({
                RemoveReport.publish(this, name, PackageInstaller.STATUS_FAILURE_ABORTED)
                finish()
            }, DISMISS_GRACE_MILLIS)
        }
    }

    fun markConfirming() {
        confirming = true
        handler.removeCallbacksAndMessages(null)
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        if (current === this) current = null
        super.onDestroy()
    }

    private fun request(name: String) {
        val result = PendingIntent.getBroadcast(
            this, 0, Intent(this, RemoveResultReceiver::class.java),
            PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        try {
            packageManager.packageInstaller.uninstall(name, result.intentSender)
            Log.i(TAG, "uninstall requested for $name")
        } catch (error: SecurityException) {
            Log.w(TAG, "uninstall of $name was refused", error)
            RemoveReport.publish(this, name, PackageInstaller.STATUS_FAILURE_BLOCKED)
            finish()
        }
    }

    companion object {
        @Volatile
        var current: RemoveAppActivity? = null

        private const val TAG = "TINH"
        private const val ACTION_REMOVE_APP = "com.openwave.tinh.action.REMOVE_APP"
        private const val WAIT_MILLIS = 100_000L
        private const val DISMISS_GRACE_MILLIS = 10_000L
    }
}
