package com.openwave.tinh

import android.content.Context
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager

object Removable {
    private const val MAX_TEXT = 60000

    fun packages(context: Context): List<String> {
        val flags = PackageManager.ApplicationInfoFlags.of(0L)
        return context.packageManager.getInstalledApplications(flags)
            .filter { it.flags and ApplicationInfo.FLAG_SYSTEM == 0 && it.packageName != context.packageName }
            .map { it.packageName }
            .sorted()
    }

    fun encode(packages: List<String>): String {
        val text = StringBuilder()
        for (name in packages.sorted()) {
            if (text.length + name.length + 1 > MAX_TEXT) break
            if (text.isNotEmpty()) text.append(',')
            text.append(name)
        }
        return text.toString()
    }
}
