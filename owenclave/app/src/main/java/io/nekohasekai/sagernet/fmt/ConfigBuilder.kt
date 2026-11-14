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

package io.nekohasekai.sagernet.fmt

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.provider.Settings
import io.nekohasekai.sagernet.Key
import io.nekohasekai.sagernet.LogLevel
import io.nekohasekai.sagernet.RouteMode
import io.nekohasekai.sagernet.SagerNet
import io.nekohasekai.sagernet.TLS_FRAGMENTATION_METHOD
import io.nekohasekai.sagernet.bg.VpnService
import io.nekohasekai.sagernet.database.DataStore
import io.nekohasekai.sagernet.database.ProxyEntity
import io.nekohasekai.sagernet.database.SagerDatabase
import io.nekohasekai.sagernet.fmt.V2rayBuildResult.IndexEntity
import io.nekohasekai.sagernet.fmt.dnstt.DnsttResolver
import io.nekohasekai.sagernet.fmt.dnstt.isValidDnsttToken
import io.nekohasekai.sagernet.fmt.dnstt.parseDnsttDomain
import io.nekohasekai.sagernet.fmt.dnstt.parseDnsttResolver
import io.nekohasekai.sagernet.fmt.gson.gson
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.DNSOutboundConfigurationObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.DnsObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.DokodemoDoorInboundConfigurationObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.FakeDnsObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.FreedomOutboundConfigurationObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.HTTPInboundConfigurationObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.InboundObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.LazyInboundConfigurationObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.LazyOutboundConfigurationObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.LogObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.OutboundObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.PolicyObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.RoutingObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.SocksInboundConfigurationObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.SocksOutboundConfigurationObject
import io.nekohasekai.sagernet.fmt.v2ray.V2RayConfig.StreamSettingsObject
import io.nekohasekai.sagernet.ktx.app
import io.nekohasekai.sagernet.ktx.getBooleanProperty
import io.nekohasekai.sagernet.ktx.listByLine
import io.nekohasekai.sagernet.ktx.listByLineOrComma
import io.nekohasekai.sagernet.ktx.mkPort
import io.nekohasekai.sagernet.ktx.unescapeLineFeed
import io.nekohasekai.sagernet.utils.PackageCache
import libexclavecore.Libexclavecore
import java.io.File
import kotlin.uuid.ExperimentalUuidApi
import kotlin.uuid.Uuid

const val TAG_SOCKS = "socks"
const val TAG_HTTP = "http"
const val TAG_TRANS = "trans"
const val TAG_TRANS6 = "trans6"

const val TAG_AGENT = "proxy"
const val TAG_DIRECT = "direct"
const val TAG_BYPASS = "bypass"
const val TAG_BLOCK = "block"

const val TAG_DNS_IN = "dns-in"
const val TAG_DNS_OUT = "dns-out"
const val TAG_DNS_DIRECT = "dns-direct"

const val LOCALHOST = "127.0.0.1"
const val LOCALHOST6 = "::1"

class V2rayBuildResult(
    var config: String,
    var index: List<IndexEntity>,
    var requireWs: Boolean,
    var wsPort: Int,
    var requireSh: Boolean,
    var shPort: Int,
    var outboundTags: List<String>,
    var outboundTagsCurrent: List<String>,
    var outboundTagsAll: Map<String, ProxyEntity>,
    var bypassTag: String,
    var observerTag: String,
    var observatoryTags: Set<String>,
    val dumpUID: Boolean,
    val alerts: List<Pair<Int, String>>,
    val useFakeDNS: Boolean,
    val dnsttClients: List<DnsttClientConfig>,
) {
    data class IndexEntity(
        var isBalancer: Boolean,
        var chain: LinkedHashMap<Triple<Int, String, String>, ProxyEntity>,
    )
}

data class DnsttClientConfig(
    val localPort: Int,
    val domain: String,
    val resolvers: List<DnsttResolver>,
    val token: String,
    val socksUsername: String,
    val socksPassword: String,
)

@OptIn(ExperimentalUuidApi::class)
fun buildV2RayConfig(
    proxy: ProxyEntity,
    forTest: Boolean = false,
    forExport: Boolean = false,
    testHttpPort: Int? = null,
): V2rayBuildResult {
    require(proxy.type == ProxyEntity.TYPE_DNSTT || proxy.type == ProxyEntity.TYPE_OLCRTC) {
        "Unsupported profile type ${proxy.type}"
    }

    val routeMode = DataStore.routeMode
    val extraRules = if (forTest || routeMode != RouteMode.RULE) {
        emptyList()
    } else {
        SagerDatabase.rulesDao.enabledRules().filter { rule ->
            rule.domains.isNotEmpty() || rule.ip.isNotEmpty() || rule.port.isNotEmpty() ||
                rule.sourcePort.isNotEmpty() || rule.network.isNotEmpty() || rule.source.isNotEmpty() ||
                rule.protocol.isNotEmpty() || rule.attrs.isNotEmpty() ||
                rule.packages.isNotEmpty() || rule.customPackageNames.isNotEmpty() ||
                rule.ssid.isNotEmpty() || rule.networkType.isNotEmpty()
        }
    }
    val extraProxies = if (forTest) {
        emptyMap()
    } else {
        SagerDatabase.proxyDao.getEntities(extraRules.mapNotNull { rule ->
            rule.outbound.takeIf { it > 0L && it != proxy.id }
        }.distinct()).associateBy { it.id }
    }

    val allowAccess = DataStore.allowAccess
    val bind = if (!forTest && allowAccess) "0.0.0.0" else LOCALHOST
    var directDNS = DataStore.directDns.listByLineOrComma().filter { !it.startsWith("#") }
    if (DataStore.useLocalDnsAsDirectDns) directDNS = listOf("localhost")
    val remoteDNS = if (routeMode == RouteMode.DIRECT) {
        directDNS
    } else {
        DataStore.remoteDns.listByLineOrComma().filter { !it.startsWith("#") }
    }
    var bootstrapDNS = DataStore.bootstrapDns.listByLineOrComma().filter { !it.startsWith("#") }
    if (DataStore.useLocalDnsAsBootstrapDns) bootstrapDNS = listOf("localhost")

    val useFakeDns = DataStore.enableFakeDns
    val remoteDnsQueryStrategy = DataStore.remoteDnsQueryStrategy
    val directDnsQueryStrategy = DataStore.directDnsQueryStrategy
    val trafficSniffing = DataStore.trafficSniffing
    val destinationOverride = DataStore.destinationOverride
    val trafficStatistics = !forTest && DataStore.profileTrafficStatistics
    val shouldDumpUID = extraRules.any {
        it.packages.isNotEmpty() || it.customPackageNames.isNotEmpty()
    }
    val alerts = mutableListOf<Pair<Int, String>>()
    val indexMap = ArrayList<IndexEntity>()
    val outboundTags = ArrayList<String>()
    val outboundTagsCurrent = ArrayList<String>()
    val outboundTagsAll = HashMap<String, ProxyEntity>()
    val dnsttClients = linkedMapOf<Triple<String, String, String>, DnsttClientConfig>()

    fun InboundObject.configureSniffing(includeQuic: Boolean) {
        if (!trafficSniffing && !useFakeDns) return
        sniffing = InboundObject.SniffingObject().apply {
            enabled = true
            destOverride = buildList {
                if (useFakeDns) add("fakedns")
                if (trafficSniffing) {
                    add("http")
                    add("tls")
                    if (includeQuic) add("quic")
                }
            }
            metadataOnly = useFakeDns && !trafficSniffing
            routeOnly = !destinationOverride
        }
    }

    lateinit var result: V2rayBuildResult
    V2RayConfig().apply {
        dns = DnsObject().apply {
            if (DataStore.hosts.isNotEmpty()) {
                hosts = mutableMapOf()
                for (singleLine in DataStore.hosts.listByLine()) {
                    val key = singleLine.substringBefore(" ")
                    val values = singleLine.substringAfter(" ").split("\\s+".toRegex()).toMutableList()
                    hosts[key]?.let { existing ->
                        if (!existing.valueX.isNullOrEmpty()) {
                            values.add(existing.valueX)
                        } else if (!existing.valueY.isNullOrEmpty()) {
                            values.addAll(existing.valueY)
                        }
                    }
                    hosts[key] = DnsObject.StringOrListObject().apply {
                        if (values.size > 1) {
                            valueY = values
                        } else if (values.size == 1) {
                            valueX = values[0]
                        }
                    }
                }
            }
            servers = mutableListOf()
            fallbackStrategy = "disabledIfAnyMatch"
        }

        log = LogObject().apply {
            loglevel = when (DataStore.logLevel) {
                LogLevel.DEBUG -> "debug"
                LogLevel.INFO -> "info"
                LogLevel.WARNING -> "warning"
                LogLevel.ERROR -> "error"
                else -> "none"
            }
            if (DataStore.logLevel == LogLevel.NONE) access = "none"
        }

        policy = PolicyObject().apply {
            levels = mapOf(
                "1" to PolicyObject.LevelPolicyObject().apply { connIdle = 30 },
            )
            if (trafficStatistics) {
                system = PolicyObject.SystemPolicyObject().apply {
                    statsOutboundDownlink = true
                    statsOutboundUplink = true
                }
            }
        }

        inbounds = mutableListOf()
        if (forTest && testHttpPort != null) {
            inbounds.add(InboundObject().apply {
                tag = "benchmark"
                listen = LOCALHOST
                port = testHttpPort
                protocol = "http"
                settings = LazyInboundConfigurationObject(
                    this,
                    HTTPInboundConfigurationObject().apply { allowTransparent = true },
                )
            })
        }

        if (!forTest) {
            if (!forExport) {
                inbounds.add(InboundObject().apply {
                    tag = "ipc-in"
                    protocol = "ipc"
                    val path = SagerNet.deviceStorage.noBackupFilesDir.toString() + "/ipc.sock"
                    val udsFile = File(path)
                    if (udsFile.exists()) udsFile.delete()
                    listen = path
                    configureSniffing(includeQuic = true)
                })
            }

            if (DataStore.requireSocks) {
                inbounds.add(InboundObject().apply {
                    tag = TAG_SOCKS
                    listen = bind
                    port = DataStore.socksPort
                    protocol = "socks"
                    settings = LazyInboundConfigurationObject(
                        this,
                        SocksInboundConfigurationObject().apply {
                            if (DataStore.socksUsername.isEmpty() && DataStore.socksPassword.isEmpty()) {
                                auth = "noauth"
                            } else if (DataStore.socksUsername.isEmpty()) {
                                error("username is empty but password is not empty for SOCKS5 inbound")
                            } else if (DataStore.socksPassword.isEmpty()) {
                                error("username is not empty but password is empty for SOCKS5 inbound")
                            } else {
                                auth = "password"
                                accounts = listOf(SocksInboundConfigurationObject.AccountObject().apply {
                                    user = DataStore.socksUsername
                                    pass = DataStore.socksPassword
                                })
                            }
                            udp = DataStore.socksUDP
                        },
                    )
                    configureSniffing(includeQuic = true)
                    if (shouldDumpUID) dumpUID = true
                })
            }

            if (DataStore.requireHttp) {
                inbounds.add(InboundObject().apply {
                    tag = TAG_HTTP
                    listen = bind
                    port = DataStore.httpPort
                    protocol = "http"
                    settings = LazyInboundConfigurationObject(
                        this,
                        HTTPInboundConfigurationObject().apply {
                            allowTransparent = true
                            if (DataStore.httpUsername.isNotEmpty() || DataStore.httpPassword.isNotEmpty()) {
                                accounts = listOf(HTTPInboundConfigurationObject.AccountObject().apply {
                                    user = DataStore.httpUsername
                                    pass = DataStore.httpPassword
                                })
                            }
                        },
                    )
                    configureSniffing(includeQuic = false)
                    if (shouldDumpUID) dumpUID = true
                })
            }

            if (DataStore.requireTransproxy) {
                fun addTransproxyInbound(tagValue: String, listenAddress: String) {
                    inbounds.add(InboundObject().apply {
                        tag = tagValue
                        listen = listenAddress
                        port = DataStore.transproxyPort
                        protocol = "dokodemo-door"
                        settings = LazyInboundConfigurationObject(
                            this,
                            DokodemoDoorInboundConfigurationObject().apply {
                                network = "tcp"
                                followRedirect = true
                            },
                        )
                        configureSniffing(includeQuic = false)
                        if (shouldDumpUID) dumpUID = true
                    })
                }
                addTransproxyInbound(TAG_TRANS, bind)
                if (bind == LOCALHOST) addTransproxyInbound(TAG_TRANS6, LOCALHOST6)
            }
        }

        outbounds = mutableListOf()
        routing = RoutingObject().apply {
            domainStrategy = DataStore.domainStrategy
            rules = mutableListOf()
        }

        fun addProfileOutbound(entity: ProxyEntity, tagValue: String, current: Boolean) {
            require(entity.type == ProxyEntity.TYPE_DNSTT || entity.type == ProxyEntity.TYPE_OLCRTC) {
                "Unsupported profile type ${entity.type}"
            }

            val chainMap = linkedMapOf<Triple<Int, String, String>, ProxyEntity>()
            indexMap.add(IndexEntity(false, chainMap))
            val outbound = OutboundObject().apply {
                tag = tagValue
                protocol = "socks"
                settings = when (entity.type) {
                    ProxyEntity.TYPE_OLCRTC -> {
                        val localPort = mkPort()
                        val username = Uuid.generateV4().toHexString()
                        val password = Uuid.generateV4().toHexString()
                        chainMap[Triple(localPort, username, password)] = entity
                        LazyOutboundConfigurationObject(
                            this,
                            SocksOutboundConfigurationObject().apply {
                                servers = listOf(SocksOutboundConfigurationObject.ServerObject().apply {
                                    address = LOCALHOST
                                    port = localPort
                                    users = listOf(SocksOutboundConfigurationObject.ServerObject.UserObject().apply {
                                        user = username
                                        pass = password
                                    })
                                })
                            },
                        )
                    }

                    ProxyEntity.TYPE_DNSTT -> {
                        val bean = entity.dnsttBean ?: error("Missing DNS Tunnel bean")
                        require(isValidDnsttToken(bean.token)) { "DNS Tunnel Flow token is required" }
                        val domain = parseDnsttDomain(bean.serverAddress)
                        val resolvers = bean.resolver.takeIf { it.isNotEmpty() }
                            ?.let { listOf(parseDnsttResolver(it)) }.orEmpty()
                        val client = dnsttClients.getOrPut(
                            Triple(domain, resolvers.joinToString(","), bean.token),
                        ) {
                            DnsttClientConfig(
                                mkPort(),
                                domain,
                                resolvers,
                                bean.token,
                                Uuid.generateV4().toHexString(),
                                Uuid.generateV4().toHexString(),
                            )
                        }
                        LazyOutboundConfigurationObject(
                            this,
                            SocksOutboundConfigurationObject().apply {
                                servers = listOf(SocksOutboundConfigurationObject.ServerObject().apply {
                                    address = LOCALHOST
                                    port = client.localPort
                                    users = listOf(SocksOutboundConfigurationObject.ServerObject.UserObject().apply {
                                        user = client.socksUsername
                                        pass = client.socksPassword
                                    })
                                })
                                version = "5"
                                uot = true
                            },
                        )
                    }

                    else -> error("Unsupported profile type ${entity.type}")
                }

                if (DataStore.outboundDomainStrategy != "AsIs") {
                    domainStrategy = DataStore.outboundDomainStrategy
                }
                if (!(domainStrategy == null && DataStore.outboundDomainStrategyForServer == "AsIs") &&
                    !(domainStrategy == "AsIs" && DataStore.outboundDomainStrategyForServer == "AsIs") &&
                    domainStrategy != DataStore.outboundDomainStrategyForServer
                ) {
                    dialDomainStrategy = DataStore.outboundDomainStrategyForServer
                }
            }
            outbounds.add(outbound)
            outboundTags.add(tagValue)
            if (current) outboundTagsCurrent.add(tagValue)
            outboundTagsAll[tagValue] = entity
        }

        addProfileOutbound(proxy, TAG_AGENT, current = true)
        val extraProxyTags = mutableMapOf<Long, String>()
        extraProxies.forEach { (id, entity) ->
            val tagValue = "$TAG_AGENT-$id"
            addProfileOutbound(entity, tagValue, current = false)
            extraProxyTags[id] = tagValue
        }

        val isVpn = DataStore.serviceMode == Key.MODE_VPN
        for (rule in extraRules) {
            val uidList = mutableListOf<Int>()
            if (rule.packages.isNotEmpty() || rule.customPackageNames.isNotEmpty()) {
                if (!isVpn) {
                    alerts.add(Alerts.ROUTE_ALERT_NOT_VPN to rule.displayName())
                    continue
                }
                PackageCache.awaitLoadSync()
                if (rule.customPackageNames.isNotEmpty()) {
                    rule.customPackageNames.forEach { packageNameOrUid ->
                        packageNameOrUid.toIntOrNull()?.let(uidList::add)
                            ?: PackageCache[packageNameOrUid]?.let(uidList::add)
                    }
                } else {
                    rule.packages.forEach { packageName ->
                        PackageCache[packageName]?.let(uidList::add)
                    }
                }
                if (uidList.isEmpty()) {
                    alerts.add(Alerts.ROUTE_ALERT_ALL_PACKAGES_UNINSTALLED to rule.displayName())
                    continue
                }
            }

            routing.rules.add(RoutingObject.RuleObject().apply {
                type = "field"
                if (uidList.isNotEmpty()) uid = uidList

                if (!forExport && !forTest && rule.ssid.isNotEmpty() &&
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1
                ) {
                    val isLocationPermissionGranted = app.checkSelfPermission(
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                            Manifest.permission.ACCESS_FINE_LOCATION
                        } else {
                            Manifest.permission.ACCESS_COARSE_LOCATION
                        },
                    ) == PackageManager.PERMISSION_GRANTED
                    if (!isLocationPermissionGranted) {
                        throw Alerts.RouteAlertException(
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                                Alerts.ROUTE_ALERT_NEED_FINE_LOCATION_ACCESS
                            } else {
                                Alerts.ROUTE_ALERT_NEED_COARSE_LOCATION_ACCESS
                            },
                            rule.displayName(),
                        )
                    }
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q &&
                        app.checkSelfPermission(Manifest.permission.ACCESS_BACKGROUND_LOCATION) !=
                        PackageManager.PERMISSION_GRANTED
                    ) {
                        throw Alerts.RouteAlertException(
                            Alerts.ROUTE_ALERT_NEED_BACKGROUND_LOCATION_ACCESS,
                            rule.displayName(),
                        )
                    }
                    val isLocationServiceEnabled = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                        SagerNet.location.isLocationEnabled
                    } else {
                        try {
                            @Suppress("DEPRECATION")
                            Settings.Secure.getInt(app.contentResolver, Settings.Secure.LOCATION_MODE) !=
                                Settings.Secure.LOCATION_MODE_OFF
                        } catch (_: Settings.SettingNotFoundException) {
                            false
                        }
                    }
                    if (!isLocationServiceEnabled) {
                        throw Alerts.RouteAlertException(
                            Alerts.ROUTE_ALERT_LOCATION_DISABLED,
                            rule.displayName(),
                        )
                    }
                }

                if (rule.domains.isNotEmpty()) domains = rule.domains.listByLineOrComma()
                if (rule.ip.isNotEmpty()) ip = rule.ip.listByLineOrComma()
                if (rule.port.isNotEmpty()) port = rule.port
                if (rule.sourcePort.isNotEmpty()) sourcePort = rule.sourcePort
                if (rule.network.isNotEmpty()) network = rule.network
                if (rule.source.isNotEmpty()) source = rule.source.listByLineOrComma()
                if (rule.protocol.isNotEmpty()) protocol = rule.protocol.listByLineOrComma()
                if (rule.attrs.isNotEmpty()) attrs = rule.attrs
                if (rule.ssid.isNotEmpty()) ssid = rule.ssid.split("\n").map { it.unescapeLineFeed() }
                if (rule.networkType.isNotEmpty()) networkType = rule.networkType.toMutableList()
                outboundTag = when (val outboundId = rule.outbound) {
                    0L -> TAG_AGENT
                    -1L -> TAG_BYPASS
                    -2L -> TAG_BLOCK
                    proxy.id -> TAG_AGENT
                    else -> extraProxyTags[outboundId]
                        ?: error("outbound not found in rule ${rule.displayName()}")
                }
            })
        }

        outbounds.add(OutboundObject().apply {
            tag = TAG_DIRECT
            protocol = "freedom"
        })
        outbounds.add(OutboundObject().apply {
            tag = TAG_BYPASS
            protocol = "freedom"
            if (DataStore.enableFragment && DataStore.enableFragmentForDirect) {
                streamSettings = StreamSettingsObject().apply {
                    sockopt = StreamSettingsObject.SockoptObject().apply {
                        tlsFragmentation = StreamSettingsObject.SockoptObject.TLSFragmentationObject().apply {
                            when (DataStore.fragmentMethod) {
                                TLS_FRAGMENTATION_METHOD.TLS_RECORD_FRAGMENTATION -> tlsRecordFragmentation = true
                                TLS_FRAGMENTATION_METHOD.TCP_SEGMENTATION -> tcpSegmentation = true
                                TLS_FRAGMENTATION_METHOD.TLS_RECORD_FRAGMENTATION_AND_TCP_SEGMENTATION -> {
                                    tlsRecordFragmentation = true
                                    tcpSegmentation = true
                                }
                            }
                        }
                    }
                }
            }
            if (DataStore.outboundDomainStrategyForDirect != "AsIs") {
                settings = LazyOutboundConfigurationObject(
                    this,
                    FreedomOutboundConfigurationObject().apply {
                        domainStrategy = DataStore.outboundDomainStrategyForDirect
                    },
                )
            }
        })
        outbounds.add(OutboundObject().apply {
            tag = TAG_BLOCK
            protocol = "blackhole"
        })

        if (!forTest && !forExport) {
            inbounds.add(InboundObject().apply {
                tag = TAG_DNS_IN
                val path = SagerNet.deviceStorage.noBackupFilesDir.toString() + "/ipc_dns.sock"
                val udsFile = File(path)
                if (udsFile.exists()) udsFile.delete()
                listen = path
                protocol = "dokodemo-door"
                settings = LazyInboundConfigurationObject(
                    this,
                    DokodemoDoorInboundConfigurationObject().apply {
                        address = "/ipc_dns.sock"
                        network = "unix"
                    },
                )
            })
        }

        if (!forTest && DataStore.requireDnsInbound && DataStore.localDNSPort > 0) {
            inbounds.add(InboundObject().apply {
                tag = TAG_DNS_IN
                listen = bind
                port = DataStore.localDNSPort
                protocol = "dokodemo-door"
                settings = LazyInboundConfigurationObject(
                    this,
                    DokodemoDoorInboundConfigurationObject().apply {
                        address = LOCALHOST
                        network = "tcp,udp"
                        port = 53
                    },
                )
                if (shouldDumpUID) dumpUID = true
            })
        }

        outbounds.add(OutboundObject().apply {
            protocol = "dns"
            tag = TAG_DNS_OUT
            settings = LazyOutboundConfigurationObject(
                this,
                DNSOutboundConfigurationObject().apply {
                    userLevel = 1
                    if (DataStore.experimentalFlagsProperties.getBooleanProperty("lookupAsExchange")) {
                        lookupAsExchange = true
                    }
                },
            )
        })

        val bypassDomain = HashSet<String>()
        val bypassDomainSkipFakeDns = HashSet<String>()
        val proxyDomain = HashSet<String>()
        val bootstrapDomain = HashSet<String>()

        (listOf(proxy) + extraProxies.values).forEach { entity ->
            val serverAddress = entity.requireBean().serverAddress
            if (serverAddress.isNotEmpty() && !Libexclavecore.isIP(serverAddress)) {
                bypassDomainSkipFakeDns.add("full:$serverAddress")
            }
        }

        if (DataStore.enableDnsRouting) {
            val directDNSDomainList = DataStore.experimentalFlagsProperties
                .getProperty("directDNSDomainList")
            if (directDNSDomainList != null) {
                if (!forTest && routeMode == RouteMode.RULE) {
                    bypassDomain.addAll(directDNSDomainList.split(","))
                }
            } else {
                extraRules.filter { it.isBypassRule() }.forEach { bypassRule ->
                    if (bypassRule.domains.isNotEmpty()) {
                        bypassDomain.addAll(bypassRule.domains.listByLineOrComma())
                    }
                }
            }

            val remoteDNSDomainList = DataStore.experimentalFlagsProperties
                .getProperty("remoteDNSDomainList")
            if (remoteDNSDomainList != null) {
                if (!forTest && routeMode == RouteMode.RULE) {
                    proxyDomain.addAll(remoteDNSDomainList.split(","))
                }
            } else {
                extraRules.filter { it.isProxyRule() }.forEach { proxyRule ->
                    if (proxyRule.domains.isNotEmpty()) {
                        proxyDomain.addAll(proxyRule.domains.listByLineOrComma())
                    }
                }
            }
        }

        remoteDNS.forEach { address ->
            try {
                if (address.lowercase() != "localhost" && address.lowercase() != "fakedns") {
                    if (address.contains("://")) {
                        val url = Libexclavecore.parseURL(address)
                        if (!Libexclavecore.isIP(url.host)) {
                            bypassDomainSkipFakeDns.add("full:${url.host}")
                        }
                    } else if (!Libexclavecore.isIP(address)) {
                        bypassDomainSkipFakeDns.add("full:$address")
                    }
                }
            } catch (_: Exception) {
            }
        }

        directDNS.forEach { address ->
            try {
                if (address.lowercase() != "localhost" && address.lowercase() != "fakedns") {
                    if (address.contains("://")) {
                        val url = Libexclavecore.parseURL(address)
                        if (!Libexclavecore.isIP(url.host)) {
                            bootstrapDomain.add("full:${url.host}")
                        }
                    } else if (!Libexclavecore.isIP(address)) {
                        bootstrapDomain.add("full:$address")
                    }
                }
            } catch (_: Exception) {
            }
        }

        fun DnsObject.ServerObject.addFakeDnsPools() {
            if (!useFakeDns) return
            fakedns = mutableListOf()
            if (queryStrategy != "UseIPv6") {
                fakedns.add(DnsObject.ServerObject.StringOrFakeDnsObject().apply {
                    valueY = FakeDnsObject().apply {
                        ipPool = "${VpnService.FAKEDNS_VLAN4_CLIENT}/${VpnService.FAKEDNS_VLAN4_CLIENT_PREFIX}"
                        poolSize = VpnService.FAKEDNS_VLAN4_CLIENT_POOL_SIZE
                    }
                })
            }
            if (queryStrategy != "UseIPv4") {
                fakedns.add(DnsObject.ServerObject.StringOrFakeDnsObject().apply {
                    valueY = FakeDnsObject().apply {
                        ipPool = "${VpnService.FAKEDNS_VLAN6_CLIENT}/${VpnService.FAKEDNS_VLAN6_CLIENT_PREFIX}"
                        poolSize = VpnService.FAKEDNS_VLAN6_CLIENT_POOL_SIZE
                    }
                })
            }
        }

        fun addDnsServers(
            addresses: List<String>,
            domains: Collection<String>,
            queryStrategyValue: String,
            direct: Boolean,
            fakeDns: Boolean,
            fallbackStrategyValue: String? = null,
        ): Boolean {
            var taggedDirect = false
            dns.servers.addAll(addresses.map { address ->
                DnsObject.StringOrServerObject().apply {
                    valueY = DnsObject.ServerObject().apply {
                        this.address = address
                        this.domains = domains.toList()
                        queryStrategy = queryStrategyValue
                        if (!direct && DataStore.ednsClientIp.isNotEmpty()) {
                            clientIp = DataStore.ednsClientIp
                        }
                        if (direct && !address.lowercase().contains("+local://") &&
                            address.lowercase() != "localhost"
                        ) {
                            tag = TAG_DNS_DIRECT
                            taggedDirect = true
                        }
                        if (fakeDns) addFakeDnsPools()
                        fallbackStrategy = fallbackStrategyValue
                    }
                }
            })
            return taggedDirect
        }

        var hasDnsTagDirect = false
        if (bypassDomain.isNotEmpty() || bypassDomainSkipFakeDns.isNotEmpty() ||
            bootstrapDomain.isNotEmpty()
        ) {
            addDnsServers(
                remoteDNS,
                proxyDomain,
                remoteDnsQueryStrategy,
                direct = false,
                fakeDns = useFakeDns,
            )
            if (bootstrapDomain.isNotEmpty()) {
                hasDnsTagDirect = addDnsServers(
                    bootstrapDNS,
                    bootstrapDomain,
                    directDnsQueryStrategy,
                    direct = true,
                    fakeDns = false,
                    fallbackStrategyValue = "disabled",
                ) || hasDnsTagDirect
            }
            if (bypassDomainSkipFakeDns.isNotEmpty()) {
                hasDnsTagDirect = addDnsServers(
                    directDNS,
                    bypassDomainSkipFakeDns,
                    directDnsQueryStrategy,
                    direct = true,
                    fakeDns = false,
                    fallbackStrategyValue = "disabled",
                ) || hasDnsTagDirect
            }
            if (bypassDomain.isNotEmpty()) {
                hasDnsTagDirect = addDnsServers(
                    directDNS,
                    bypassDomain,
                    directDnsQueryStrategy,
                    direct = true,
                    fakeDns = useFakeDns,
                    fallbackStrategyValue = "disabled",
                ) || hasDnsTagDirect
            }
        } else {
            addDnsServers(
                remoteDNS,
                emptyList(),
                remoteDnsQueryStrategy,
                direct = false,
                fakeDns = useFakeDns,
            )
        }

        if (routeMode == RouteMode.DIRECT && proxy.type != ProxyEntity.TYPE_DNSTT) {
            routing.rules.add(0, RoutingObject.RuleObject().apply {
                type = "field"
                port = "0-65535"
                outboundTag = TAG_BYPASS
            })
        }
        if (hasDnsTagDirect) {
            routing.rules.add(0, RoutingObject.RuleObject().apply {
                type = "field"
                inboundTag = listOf(TAG_DNS_DIRECT)
                outboundTag = TAG_BYPASS
            })
        }
        if (!forTest && trafficSniffing && DataStore.hijackDns) {
            routing.rules.add(0, RoutingObject.RuleObject().apply {
                type = "field"
                protocol = listOf("dns")
                outboundTag = TAG_DNS_OUT
            })
        }
        if (!forTest) {
            routing.rules.add(0, RoutingObject.RuleObject().apply {
                type = "field"
                inboundTag = listOf(TAG_DNS_IN)
                outboundTag = TAG_DNS_OUT
            })
        }

        if (trafficStatistics) stats = emptyMap()

        require(dnsttClients.size <= 1) { "Only one DNS Tunnel is supported per connection" }
        result = V2rayBuildResult(
            config = gson.toJson(this),
            index = indexMap,
            requireWs = false,
            wsPort = 0,
            requireSh = false,
            shPort = 0,
            outboundTags = outboundTags,
            outboundTagsCurrent = outboundTagsCurrent,
            outboundTagsAll = outboundTagsAll,
            bypassTag = TAG_BYPASS,
            observerTag = "",
            observatoryTags = emptySet(),
            dumpUID = shouldDumpUID,
            alerts = alerts,
            useFakeDNS = useFakeDns,
            dnsttClients = dnsttClients.values.toList(),
        )
    }

    return result
}
