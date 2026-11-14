/******************************************************************************
 *                                                                            *
 * Copyright (C) 2021 by nekohasekai <contact-sagernet@sekai.icu>             *
 *                                                                            *
 * This program is free software: you can redistribute it and/or modify       *
 * it under the terms of the GNU General Public License as published by       *
 * the Free Software Foundation, either version 3 of the License, or          *
 *  (at your option) any later version.                                       *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program. If not, see <http://www.gnu.org/licenses/>.       *
 *                                                                            *
 ******************************************************************************/

package io.nekohasekai.sagernet.bg.test

import android.net.Network
import io.nekohasekai.sagernet.bg.GuardedProcessPool
import io.nekohasekai.sagernet.bg.LocalResolver
import io.nekohasekai.sagernet.bg.proto.V2RayInstance
import io.nekohasekai.sagernet.database.ProxyEntity
import io.nekohasekai.sagernet.fmt.buildV2RayConfig
import io.nekohasekai.sagernet.ktx.Logs
import io.nekohasekai.sagernet.ktx.runOnDefaultDispatcher
import io.nekohasekai.sagernet.utils.DefaultNetworkListener
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.async
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.selects.select
import libexclavecore.Libexclavecore
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean

class V2RayTestInstance(
    profile: ProxyEntity,
    val link: String = "",
    val timeout: Int = 5000,
    val protectPath: String = "",
    private val testHttpPort: Int? = null,
) : V2RayInstance(
    profile,
), LocalResolver {
    private companion object {
        const val DNS_TEST_READY_TIMEOUT_MS = 5_000L
        val dnsTestMutex = Mutex()
    }

    override val dnsTunnelReadyTimeoutMs = DNS_TEST_READY_TIMEOUT_MS
    private val closed = AtomicBoolean()

    suspend fun doTest() = runTest {
        Libexclavecore.urlTest(v2rayPoint, "", link, timeout)
    }

    suspend fun <T> runTest(block: suspend V2RayTestInstance.() -> T): T = coroutineScope {
        val fatal = CompletableDeferred<IOException>()
        processes = GuardedProcessPool {
            Logs.w(it)
            fatal.complete(it)
        }
        val test = async(Dispatchers.Default) {
            try {
                init()
                val run = suspend {
                    try {
                        launch()
                        awaitReady()
                        block()
                    } finally {
                        close()
                    }
                }
                if (config.dnsttClients.isNotEmpty()) {
                    dnsTestMutex.withLock { run() }
                } else {
                    run()
                }
            } finally {
                close()
            }
        }
        try {
            select<T> {
                test.onAwait { it }
                fatal.onAwait { throw it }
            }
        } finally {
            close()
            withContext(NonCancellable) {
                test.cancelAndJoin()
            }
        }
    }

    @Volatile
    override var underlyingNetwork: Network? = null

    override suspend fun init() {
        super.init()
        v2rayPoint.withLocalResolver(this)
        v2rayPoint.withAlternativeSystemDialer(protectPath)
        DefaultNetworkListener.start(this) {
            underlyingNetwork = it
        }
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        runOnDefaultDispatcher { DefaultNetworkListener.stop(this@V2RayTestInstance) }
        super.close()
    }

    override fun buildConfig() {
        config = buildV2RayConfig(profile, forTest = true, testHttpPort = testHttpPort)
    }
}
