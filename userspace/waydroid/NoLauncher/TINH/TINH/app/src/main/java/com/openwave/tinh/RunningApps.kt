package com.openwave.tinh

import android.app.ActivityManager
import android.content.Context
import android.content.Intent

/** Android's running application tasks, as OpenWave sees them. */
object RunningApps {
    private const val MAX_TASKS = 32

    // Android's launcher draws the navigation bar and "android" is the
    // system's own dialogs; neither is an application to show or close.
    private val NOT_APPS = setOf("com.android.launcher3", "com.android.systemui", "android")

    /** Tasks that belong to applications, most recent first. Empty without REAL_GET_TASKS. */
    @Suppress("DEPRECATION")
    fun tasks(context: Context): List<ActivityManager.RunningTaskInfo> {
        val manager = context.getSystemService(ActivityManager::class.java) ?: return emptyList()
        val tasks = try {
            manager.getRunningTasks(MAX_TASKS)
        } catch (_: SecurityException) {
            return emptyList()
        }
        return tasks.filter { task ->
            val base = task.baseActivity ?: return@filter false
            task.numActivities > 0
                && base.packageName != context.packageName
                && base.packageName !in NOT_APPS
                && !base.className.endsWith("FallbackHome")
                && task.baseIntent.categories?.contains(Intent.CATEGORY_HOME) != true
        }
    }

    /** Packages with a running task, most recent first, each once. */
    fun packages(context: Context): List<String> =
        tasks(context).mapNotNull { it.baseActivity?.packageName }.distinct()
}
