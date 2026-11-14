package io.nekohasekai.sagernet.ui.compose

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.lifecycleScope
import io.nekohasekai.sagernet.BuildConfig
import io.nekohasekai.sagernet.SagerNet
import io.nekohasekai.sagernet.database.DataStore
import io.nekohasekai.sagernet.database.GroupManager
import io.nekohasekai.sagernet.database.ProfileManager
import io.nekohasekai.sagernet.database.ProxyEntity
import io.nekohasekai.sagernet.database.ProxyGroup
import io.nekohasekai.sagernet.database.RuleEntity
import io.nekohasekai.sagernet.database.SagerDatabase
import io.nekohasekai.sagernet.fmt.dnstt.DnsttBean
import io.nekohasekai.sagernet.fmt.dnstt.parseDnsttDomain
import io.nekohasekai.sagernet.fmt.dnstt.parseDnsttResolver
import io.nekohasekai.sagernet.ktx.applyDefaultValues
import io.nekohasekai.sagernet.fmt.olcrtc.OLCRTCBean
import io.nekohasekai.sagernet.ui.compose.screens.AssetsScreen
import io.nekohasekai.sagernet.ui.compose.screens.AppItem
import io.nekohasekai.sagernet.ui.compose.screens.AppListScreen
import io.nekohasekai.sagernet.ui.compose.screens.ConfigEditScreen
import io.nekohasekai.sagernet.ui.compose.screens.GroupSettingsScreen
import io.nekohasekai.sagernet.ui.compose.screens.GroupSettingsData
import io.nekohasekai.sagernet.ui.compose.screens.ProbeCertScreen
import io.nekohasekai.sagernet.ui.compose.screens.ProfileFieldState
import io.nekohasekai.sagernet.ui.compose.screens.ProfileSelectScreen
import io.nekohasekai.sagernet.ui.compose.screens.QuickSwitchScreen
import io.nekohasekai.sagernet.ui.compose.screens.RouteSettingsScreen
import io.nekohasekai.sagernet.ui.compose.screens.ScannerScreen
import io.nekohasekai.sagernet.ui.compose.screens.StunScreen
import io.nekohasekai.sagernet.ui.compose.screens.UniversalProfileSettingsScreen
import io.nekohasekai.sagernet.utils.PackageCache
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext

class ComposeAssetsActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            OwenclaveTheme {
                AssetsScreen(
                    assets = emptyList(),
                    onBack = { finish() },
                    onAdd = {},
                    onEdit = {},
                    onDelete = {},
                )
            }
        }
    }
}

class ComposeAppListActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        if (!DataStore.proxyApps) {
            DataStore.proxyApps = true
        }

        val pm = packageManager
        var apps by mutableStateOf<List<AppItem>>(emptyList())
        var loading by mutableStateOf(true)
        var bypass by mutableStateOf(DataStore.bypass)

        fun loadApps() {
            lifecycleScope.launch {
                loading = true
                val loaded = withContext(Dispatchers.IO) {
                    PackageCache.awaitLoadSync()
                    val individual = DataStore.individual
                        .split("\n").filter { it.isNotBlank() }.toSet()
                    PackageCache.installedPackages.toMutableMap().apply {
                        remove(BuildConfig.APPLICATION_ID)
                    }.map { (packageName, packageInfo) ->
                        val appInfo = packageInfo.applicationInfo!!
                        AppItem(
                            packageName = packageName,
                            label = appInfo.loadLabel(pm).toString(),
                            icon = appInfo.loadIcon(pm),
                            enabled = individual.contains(packageName),
                        )
                    }.sortedWith(compareBy({ !it.enabled }, { it.label }))
                }
                apps = loaded
                loading = false
            }
        }

        fun saveApps(updated: List<AppItem>) {
            apps = updated
            DataStore.individual = updated.filter { it.enabled }
                .joinToString("\n") { it.packageName }
        }

        loadApps()

        setContent {
            OwenclaveTheme {
                AppListScreen(
                    apps = apps,
                    loading = loading,
                    bypass = bypass,
                    onBypassChange = { bypass = it; DataStore.bypass = it },
                    onBack = { finish() },
                    onToggle = { app ->
                        saveApps(apps.map {
                            if (it.packageName == app.packageName) it.copy(enabled = !it.enabled)
                            else it
                        }.sortedWith(compareBy({ !it.enabled }, { it.label })))
                    },
                    onInvert = {
                        saveApps(apps.map { it.copy(enabled = !it.enabled) }
                            .sortedWith(compareBy({ !it.enabled }, { it.label })))
                    },
                    onClear = {
                        saveApps(apps.map { it.copy(enabled = false) }
                            .sortedWith(compareBy({ !it.enabled }, { it.label })))
                    },
                    onCopy = {
                        val text = "${DataStore.bypass}\n${DataStore.individual}"
                        SagerNet.trySetPrimaryClip(text)
                    },
                    onDisable = {
                        DataStore.proxyApps = false
                        finish()
                    },
                )
            }
        }
    }
}

class ComposeScannerActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            OwenclaveTheme {
                ScannerScreen(
                    onBack = { finish() },
                    onImportFile = {},
                    onToggleFlash = {},
                    onSwitchCamera = {},
                )
            }
        }
    }
}

class ComposeGroupSettingsActivity : ComponentActivity() {

    private var editingGroupId: Long = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        editingGroupId = intent.getLongExtra("groupId", 0L)

        var initialName = ""
        var initialOrder = 0

        if (editingGroupId > 0L) {
            runBlocking {
                val group = SagerDatabase.groupDao.getById(editingGroupId)
                if (group != null) {
                    initialName = group.name ?: ""
                    initialOrder = group.order
                }
            }
        }

        setContent {
            OwenclaveTheme {
                GroupSettingsScreen(
                    groupName = initialName,
                    initialOrder = initialOrder,
                    onBack = { finish() },
                    onSave = { data ->
                        runBlocking {
                            if (editingGroupId > 0L) {
                                val group = SagerDatabase.groupDao.getById(editingGroupId)
                                if (group != null) {
                                    group.name = data.name.ifEmpty { "Group" }
                                    group.order = data.order
                                    GroupManager.updateGroup(group)
                                }
                            } else {
                                val group = ProxyGroup(
                                    name = data.name.ifEmpty { "Group" },
                                    order = data.order,
                                )
                                GroupManager.createGroup(group)
                            }
                        }
                        finish()
                    },
                )
            }
        }
    }
}

class ComposeRouteSettingsActivity : ComponentActivity() {

    private var editingRuleId: Long = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        editingRuleId = intent.getLongExtra("ruleId", 0L)

        var initialName = ""
        var initialDomains = ""
        var initialIP = ""
        var initialPort = ""
        var initialSourcePort = ""
        var initialNetwork = ""
        var initialSource = ""
        var initialProtocol = ""
        var initialOutbound = 0
        var initialPackages = ""

        if (editingRuleId > 0L) {
            runBlocking {
                val rule = SagerDatabase.rulesDao.getById(editingRuleId)
                if (rule != null) {
                    initialName = rule.name
                    initialDomains = rule.domains ?: ""
                    initialIP = rule.ip ?: ""
                    initialPort = rule.port ?: ""
                    initialSourcePort = rule.sourcePort ?: ""
                    initialNetwork = rule.network ?: ""
                    initialSource = rule.source ?: ""
                    initialProtocol = rule.protocol ?: ""
                    initialOutbound = when (rule.outbound) {
                        0L -> 0
                        -1L -> 1
                        -2L -> 2
                        else -> 3
                    }
                    initialPackages = rule.packages.joinToString("\n")
                }
            }
        }

        setContent {
            OwenclaveTheme {
                RouteSettingsScreen(
                    routeName = initialName,
                    routeDomain = initialDomains,
                    routeIP = initialIP,
                    routePort = initialPort,
                    routeSourcePort = initialSourcePort,
                    routeNetwork = initialNetwork,
                    routeSource = initialSource,
                    routeProtocol = initialProtocol,
                    routeOutbound = initialOutbound,
                    routePackages = initialPackages,
                    onBack = { finish() },
                    onSave = { name, domains, ip, port, sourcePort, network, source, protocol, outbound, packages ->
                        runBlocking {
                            val rule = if (editingRuleId > 0L) {
                                SagerDatabase.rulesDao.getById(editingRuleId) ?: RuleEntity()
                            } else {
                                RuleEntity()
                            }
                            rule.name = name
                            rule.domains = domains
                            rule.ip = ip
                            rule.port = port
                            rule.sourcePort = sourcePort
                            rule.network = network
                            rule.source = source
                            rule.protocol = protocol
                            rule.outbound = when (outbound) {
                                0 -> 0L
                                1 -> -1L
                                2 -> -2L
                                else -> 0L
                            }
                            rule.packages = packages.split("\n").filter { it.isNotEmpty() }
                            if (editingRuleId == 0L) {
                                rule.enabled = true
                                ProfileManager.createRule(rule)
                            } else {
                                ProfileManager.updateRule(rule)
                            }
                        }
                        finish()
                    },
                )
            }
        }
    }
}

class ComposeConfigEditActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val content = intent.getStringExtra("content") ?: ""
        setContent {
            OwenclaveTheme {
                ConfigEditScreen(
                    initialContent = content,
                    onBack = { finish() },
                    onSave = { finish() },
                )
            }
        }
    }
}

class ComposeStunActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            OwenclaveTheme {
                StunScreen(onBack = { finish() })
            }
        }
    }
}

class ComposeProbeCertActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            OwenclaveTheme {
                ProbeCertScreen(onBack = { finish() })
            }
        }
    }
}

class ComposeProfileSelectActivity : ComponentActivity() {
    companion object {
        const val EXTRA_SELECTED = "selected"
        const val EXTRA_PROFILE_ID = "profileId"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val selectedId = intent.getLongExtra(EXTRA_SELECTED, 0L)
        setContent {
            OwenclaveTheme {
                ProfileSelectScreen(
                    profiles = emptyList(),
                    selectedId = selectedId,
                    onSelect = { profile ->
                        setResult(RESULT_OK, intent.putExtra(EXTRA_PROFILE_ID, profile.id))
                        finish()
                    },
                    onBack = { finish() },
                )
            }
        }
    }
}

class ComposeQuickToggleActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            OwenclaveTheme {
                QuickSwitchScreen(
                    profiles = emptyList(),
                    currentProfileId = DataStore.selectedProxy,
                    onSelect = { profile ->
                        DataStore.selectedProxy = profile.id
                        io.nekohasekai.sagernet.SagerNet.reloadService()
                        finish()
                    },
                )
            }
        }
    }
}

class ComposeProfileSettingsActivity : ComponentActivity() {
    companion object {
        const val EXTRA_PROFILE_ID = "profileId"
        const val EXTRA_PROFILE_TYPE = "profileType"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val profileId = intent.getLongExtra(EXTRA_PROFILE_ID, 0L)
        val profileType = intent.getIntExtra(EXTRA_PROFILE_TYPE, -1)
        require(profileType == ProxyEntity.TYPE_DNSTT || profileType == ProxyEntity.TYPE_OLCRTC)

        var entity: ProxyEntity? = null
        var initialState = ProfileFieldState()

        if (profileId > 0L) {
            runBlocking {
                entity = SagerDatabase.proxyDao.getById(profileId)
                if (entity != null) {
                    initialState = beanToState(entity!!, profileType)
                }
            }
        }

        setContent {
            OwenclaveTheme {
                UniversalProfileSettingsScreen(
                    profileType = profileType,
                    initialState = initialState,
                    onBack = { finish() },
                    onSave = { state ->
                        lifecycleScope.launch(Dispatchers.IO) {
                            saveProfile(entity, profileId, profileType, state)
                            withContext(Dispatchers.Main) { finish() }
                        }
                    },
                )
            }
        }
    }

    private fun beanToState(entity: ProxyEntity, type: Int): ProfileFieldState {
        val bean = entity.requireBean()
        val s = ProfileFieldState(name = bean.name ?: "", iconIndex = entity.iconIndex)
        return when (type) {
            ProxyEntity.TYPE_DNSTT -> {
                val b = entity.dnsttBean ?: return s
                s.copy(
                    token = b.token ?: "",
                    dnsttDomain = b.serverAddress ?: DnsttBean.LEGACY_DOMAIN,
                    dnsttManual = b.resolver?.isNotEmpty() == true,
                    dnsttResolver = b.resolver ?: "",
                )
            }
            ProxyEntity.TYPE_OLCRTC -> {
                val b = entity.olcrtcBean ?: return s
                s.copy(authProvider = b.authProvider ?: "jitsi", transport = b.transport ?: "datachannel",
                    roomId = b.roomId ?: "", encryptionKey = b.encryptionKey ?: "",
                    dnsServer = b.dnsServer ?: "8.8.8.8:53", socksHost = b.socksHost ?: "127.0.0.1",
                    socksPort = b.socksPort?.toString() ?: "8808")
            }
            else -> error("Unsupported profile type $type")
        }
    }

    private suspend fun saveProfile(existing: ProxyEntity?, profileId: Long, type: Int, state: ProfileFieldState) {
        val entity = existing ?: ProxyEntity(type = type)
        entity.type = type

        when (type) {
            ProxyEntity.TYPE_DNSTT -> {
                val b = entity.dnsttBean ?: DnsttBean().applyDefaultValues()
                val oldDomain = b.serverAddress
                val oldToken = b.token
                b.name = state.name
                b.token = state.token
                b.serverAddress = parseDnsttDomain(state.dnsttDomain)
                b.resolver = if (state.dnsttManual) parseDnsttResolver(state.dnsttResolver).toString() else ""
                if (b.serverAddress != oldDomain || b.token != oldToken) b.benchmarkSnapshot = ""
                entity.dnsttBean = b
            }
            ProxyEntity.TYPE_OLCRTC -> {
                val b = entity.olcrtcBean ?: OLCRTCBean().applyDefaultValues()
                b.name = state.name
                b.authProvider = state.authProvider
                b.transport = state.transport
                b.roomId = state.roomId
                b.encryptionKey = state.encryptionKey
                b.dnsServer = state.dnsServer
                b.socksHost = state.socksHost
                b.socksPort = state.socksPort.toIntOrNull() ?: 8808
                entity.olcrtcBean = b
            }
            else -> error("Unsupported profile type $type")
        }

        entity.iconIndex = state.iconIndex

        if (profileId > 0L) {
            ProfileManager.updateProfile(entity)
        } else {
            val groupId = DataStore.currentGroupId()
            entity.groupId = groupId
            val created = ProfileManager.createProfile(groupId, entity.requireBean())
            created.iconIndex = state.iconIndex
            ProfileManager.updateProfile(created)
        }
    }
}
