package com.openwave.tinh

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri

object HandlerProbe {
    const val ACTION_VIEW_IN = "com.openwave.tinh.action.VIEW_IN"
    const val SCHEME_VIEW_IN = "openwave-view"
    private const val MAX_TEXT = 60000

    class Probe(val token: String, val action: String, val uri: String, val browsable: Boolean = false) {
        fun intent(): Intent {
            val intent = Intent(action, Uri.parse(uri))
            if (browsable) intent.addCategory(Intent.CATEGORY_BROWSABLE)
            return intent
        }
    }

    val probes = listOf(
        Probe("http", Intent.ACTION_VIEW, "http://example.com/", true),
        Probe("https", Intent.ACTION_VIEW, "https://example.com/", true),
        Probe("mailto", Intent.ACTION_SENDTO, "mailto:user@example.com"),
        Probe("tel", Intent.ACTION_DIAL, "tel:+10000000000"),
        Probe("sms", Intent.ACTION_SENDTO, "smsto:+10000000000"),
        Probe("sms", Intent.ACTION_SENDTO, "sms:+10000000000"),
    )

    fun probeFor(uri: Uri): Probe? {
        val scheme = uri.scheme?.lowercase() ?: return null
        return probes.firstOrNull { it.uri.substringBefore(':') == scheme }
    }

    fun handlers(context: Context): Map<String, Set<String>> {
        val manager = context.packageManager
        val result = sortedMapOf<String, MutableSet<String>>()
        val flags = PackageManager.ResolveInfoFlags.of(PackageManager.MATCH_ALL.toLong())
        for (probe in probes) {
            for (info in manager.queryIntentActivities(probe.intent(), flags)) {
                val activity = info.activityInfo ?: continue
                if (!activity.exported || !activity.enabled) continue
                if (activity.packageName == context.packageName || activity.packageName == "android") continue
                result.getOrPut(activity.packageName) { sortedSetOf() }.add(probe.token)
            }
        }
        return result
    }

    fun encode(handlers: Map<String, Set<String>>): String {
        val text = StringBuilder()
        for ((name, tokens) in handlers.toSortedMap()) {
            if (tokens.isEmpty()) continue
            val entry = name + "=" + tokens.sorted().joinToString(",")
            if (text.length + entry.length + 1 > MAX_TEXT) break
            if (text.isNotEmpty()) text.append(';')
            text.append(entry)
        }
        return text.toString()
    }

    fun handles(context: Context, packageName: String, token: String): Boolean =
        handlers(context)[packageName]?.contains(token) == true
}
