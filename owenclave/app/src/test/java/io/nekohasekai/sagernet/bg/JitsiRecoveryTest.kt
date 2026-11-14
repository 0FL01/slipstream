package io.nekohasekai.sagernet.bg

import kotlinx.coroutines.*
import org.junit.Assert.*
import org.junit.Test

class JitsiRecoveryTest {
    @Test
    fun sameNetworkReturnIsAnEventEvenWhenLossAndReturnAreConflated() = runBlocking {
        val recovery = JitsiRecovery("wifi")
        val revision = recovery.current.revision
        recovery.update(null)
        recovery.update("wifi")
        val returned = withTimeout(1000) { recovery.awaitChange(revision) }
        assertEquals("wifi", returned.underlay)
        assertFalse(returned.failure)
    }

    @Test
    fun duplicateCallbacksDoNotRestartButLinkChangesDo() = runBlocking {
        val recovery = JitsiRecovery(listOf("wifi", "address1", "dns1"))
        recovery.update(listOf("wifi", "address1", "dns1"))
        assertEquals(0L, recovery.current.revision)
        recovery.update(listOf("wifi", "address2", "dns1"))
        assertEquals(1L, recovery.current.revision)
    }

    @Test
    fun nativeFailureKeepsIntentButDoesNotLaunchOffline() = runBlocking {
        val recovery = JitsiRecovery(null)
        recovery.failed()
        val waiting = async { recovery.awaitOnline() }
        yield()
        assertFalse(waiting.isCompleted)
        recovery.update("lte")
        assertEquals("lte", withTimeout(1000) { waiting.await() }.underlay)
    }

    @Test
    fun stopCancelsOfflineWaitAndConnectedEventWait() = runBlocking {
        val recovery = JitsiRecovery(null)
        val offline = launch { recovery.awaitOnline(); fail("cancelled offline wait resumed") }
        val connected = launch { recovery.awaitChange(0); fail("cancelled event wait resumed") }
        yield()
        offline.cancelAndJoin()
        connected.cancelAndJoin()
        recovery.update("wifi")
        assertTrue(offline.isCancelled)
        assertTrue(connected.isCancelled)
    }

    @Test
    fun repeatedFailuresUseCappedPacing() {
        assertEquals(listOf(5000L, 10000L, 20000L, 40000L, 60000L, 60000L),
            (0..5).map(JitsiRecovery::retryDelay))
        assertEquals(60000L, JitsiRecovery.retryDelay(Int.MAX_VALUE))
    }
}
