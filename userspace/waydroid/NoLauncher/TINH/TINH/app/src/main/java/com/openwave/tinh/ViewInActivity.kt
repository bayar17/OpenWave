package com.openwave.tinh

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.util.Log

class ViewInActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        try {
            if (intent?.action == HandlerProbe.ACTION_VIEW_IN) open()
        } finally {
            finish()
        }
    }

    private fun open() {
        val request = intent?.data ?: return
        if (request.scheme != HandlerProbe.SCHEME_VIEW_IN) return
        val target = request.authority ?: return
        val link = request.getQueryParameter("u")?.takeIf { it.length <= MAX_LINK } ?: return
        val uri = Uri.parse(link)
        val probe = HandlerProbe.probeFor(uri) ?: return
        if (!HandlerProbe.handles(this, target, probe.token)) {
            Log.w(TAG, "$target does not handle ${probe.token}")
            return
        }
        val view = probe.intent().apply {
            data = uri
            setPackage(target)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        try {
            startActivity(view)
        } catch (error: ActivityNotFoundException) {
            Log.w(TAG, "$target cannot open the link", error)
        }
    }

    private companion object {
        const val TAG = "TINH"
        const val MAX_LINK = 2048
    }
}
