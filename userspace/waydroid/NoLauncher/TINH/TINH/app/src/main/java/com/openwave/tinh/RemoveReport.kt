package com.openwave.tinh

import android.content.Context
import android.provider.Settings

object RemoveReport {
    const val SETTING = "openwave_android_remove"

    fun format(target: String, status: Int, stamp: Long): String = "$target,$status,$stamp"

    fun publish(context: Context, target: String, status: Int) {
        Settings.Global.putString(context.contentResolver, SETTING, format(target, status, System.currentTimeMillis()))
    }
}
