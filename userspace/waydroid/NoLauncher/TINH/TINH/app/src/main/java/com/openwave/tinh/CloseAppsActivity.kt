package com.openwave.tinh

import android.app.Activity
import android.app.ActivityManager
import android.os.Bundle
import android.util.Log

class CloseAppsActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        try {
            if (intent?.action == ACTION_CLOSE_APPS) closeRunningApps()
        } finally {
            finish()   // Theme.NoDisplay: must finish before onResume returns
        }
    }

    private fun closeRunningApps() {
        val manager = getSystemService(ActivityManager::class.java) ?: return
        val running = RunningApps.packages(this)
        // "package:<name>" closes that one app (its card in Ocean); no data
        // closes them all. Only apps that have a running task are touched.
        val only = intent?.data?.takeIf { it.scheme == "package" }?.schemeSpecificPart
        val packages = if (only != null) running.filter { it == only } else running
        // forceStopPackage is a system API (not in the public SDK stubs).
        val forceStop = try {
            ActivityManager::class.java.getMethod("forceStopPackage", String::class.java)
        } catch (error: ReflectiveOperationException) {
            Log.w(TAG, "force-stop is unavailable", error)
            return
        }
        for (name in packages) {
            try {
                forceStop.invoke(manager, name)
                Log.i(TAG, "closed $name")
            } catch (error: ReflectiveOperationException) {
                Log.w(TAG, "could not close $name", error.cause ?: error)
            }
        }
        Log.i(TAG, "close request handled, ${packages.size} app(s)")
    }

    private companion object {
        const val TAG = "TINH"
        const val ACTION_CLOSE_APPS = "com.openwave.tinh.action.CLOSE_APPS"
    }
}
