package com.openwave.tinh

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.util.Log

class RemoveResultReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, Int.MIN_VALUE)
        val message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)
        val target = intent.getStringExtra(PackageInstaller.EXTRA_PACKAGE_NAME) ?: return
        Log.i(TAG, "uninstall of $target reported status $status: $message")
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            val confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
            val host = RemoveAppActivity.current
            if (confirm != null && host != null) {
                host.markConfirming()
                host.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                return
            }
            RemoveReport.publish(context, target, PackageInstaller.STATUS_FAILURE)
        } else {
            RemoveReport.publish(context, target, status)
        }
        RemoveAppActivity.current?.finish()
    }

    private companion object {
        const val TAG = "TINH"
    }
}
