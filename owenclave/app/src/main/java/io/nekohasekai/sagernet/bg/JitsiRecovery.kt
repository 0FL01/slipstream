package io.nekohasekai.sagernet.bg

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first

/** Event-driven underlay intent, owned and cancelled by the service's connecting job. */
class JitsiRecovery(initialUnderlay: Any?) {
    data class Event(val underlay: Any?, val revision: Long, val failure: Boolean = false)
    private val events = MutableStateFlow(Event(initialUnderlay, 0))
    val current get() = events.value

    fun update(underlay: Any?) {
        if (underlay != current.underlay) events.value = Event(underlay, current.revision + 1)
    }

    fun failed() {
        events.value = current.copy(revision = current.revision + 1, failure = true)
    }

    suspend fun awaitOnline() = events.first { it.underlay != null }
    suspend fun awaitChange(revision: Long) = events.first { it.revision != revision }

    companion object {
        fun retryDelay(failures: Int): Long = (5_000L shl failures.coerceIn(0, 4)).coerceAtMost(60_000L)
    }
}
