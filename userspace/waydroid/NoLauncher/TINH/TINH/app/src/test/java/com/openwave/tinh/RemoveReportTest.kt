package com.openwave.tinh

import org.junit.Assert.assertEquals
import org.junit.Test

class RemoveReportTest {
    @Test
    fun reportNamesThePackageStatusAndStamp() {
        assertEquals("org.example.app,3,1700", RemoveReport.format("org.example.app", 3, 1700))
    }
}
