package io.nekohasekai.sagernet.ui.compose.screens

import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentPaste
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialShapes
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.material3.toShape
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import io.nekohasekai.sagernet.aidl.TrafficStats
import io.nekohasekai.sagernet.GroupOrder
import io.nekohasekai.sagernet.SagerNet
import io.nekohasekai.sagernet.bg.test.DnsttBenchmark
import io.nekohasekai.sagernet.bg.test.DnsttBenchmarkResult
import io.nekohasekai.sagernet.bg.test.decodeDnsttBenchmarkSnapshot
import io.nekohasekai.sagernet.bg.test.encode
import io.nekohasekai.sagernet.database.DataStore
import io.nekohasekai.sagernet.database.GroupManager
import io.nekohasekai.sagernet.database.ProfileManager
import io.nekohasekai.sagernet.database.ProxyEntity
import io.nekohasekai.sagernet.database.SagerDatabase
import io.nekohasekai.sagernet.fmt.dnstt.DnsttBean
import io.nekohasekai.sagernet.fmt.dnstt.parseDnsttProvisioning
import io.nekohasekai.sagernet.ktx.parseShareLinks
import io.nekohasekai.sagernet.ui.compose.ComposeProfileSettingsActivity
import io.nekohasekai.sagernet.ui.compose.components.DnsttBenchmarkDialog
import io.nekohasekai.sagernet.ui.compose.components.EmptyState
import io.nekohasekai.sagernet.ui.compose.components.OwenclaveTopAppBar
import io.nekohasekai.sagernet.ui.compose.components.ProfileCard
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class, ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun ConfigurationScreen(
    onMenuClick: () -> Unit,
    serviceRunning: Boolean = false,
    serviceConnected: Boolean = false,
    batchTestProgress: Pair<Int, Int>? = null,
    onBatchTestProgress: (Pair<Int, Int>?) -> Unit = {},
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    val profiles = remember { mutableStateListOf<ProxyEntity>() }
    var selectedProfileId by remember { mutableStateOf(DataStore.selectedProxy) }
    var showProtocolPicker by remember { mutableStateOf(false) }
    var pingingIds by remember { mutableStateOf<Set<Long>>(emptySet()) }
    var showDinoGame by remember { mutableStateOf(false) }
    val reloadAccess = remember { kotlinx.coroutines.sync.Mutex() }
    var batchTestJob by remember { mutableStateOf<kotlinx.coroutines.Job?>(null) }
    var benchmarkProfile by remember { mutableStateOf<ProxyEntity?>(null) }
    val benchmarkResults = remember { mutableStateListOf<DnsttBenchmarkResult>() }
    var benchmarkRunning by remember { mutableStateOf(false) }
    var benchmarkError by remember { mutableStateOf<String?>(null) }
    var benchmarkHost by remember { mutableStateOf<String?>(null) }
    var benchmarkJob by remember { mutableStateOf<Job?>(null) }

    fun loadProfiles() {
        val groupId = DataStore.currentGroupId()
        val raw = SagerDatabase.proxyDao.getByGroup(groupId)
        val group = SagerDatabase.groupDao.getById(groupId)
        val sorted = when (group?.order) {
            GroupOrder.BY_NAME -> raw.sortedBy { it.displayName() }
            GroupOrder.BY_DELAY -> raw.sortedBy { if (it.status == 1) it.ping else 114514 }
            else -> raw
        }
        profiles.clear()
        profiles.addAll(sorted)
    }

    val listener = remember {
        object : ProfileManager.Listener {
            override suspend fun onAdd(profile: ProxyEntity) {
                withContext(Dispatchers.Main) { profiles.add(profile) }
            }
            override suspend fun onUpdated(profileId: Long, trafficStats: TrafficStats) {}
            override suspend fun onUpdated(profile: ProxyEntity) {
                withContext(Dispatchers.Main) {
                    val index = profiles.indexOfFirst { it.id == profile.id }
                    if (index >= 0) profiles[index] = profile
                    val group = SagerDatabase.groupDao.getById(DataStore.currentGroupId())
                    if (group?.order == GroupOrder.BY_DELAY) {
                        val sorted = profiles.sortedBy { if (it.status == 1) it.ping else 114514 }
                        profiles.clear()
                        profiles.addAll(sorted)
                    }
                }
            }
            override suspend fun onRemoved(groupId: Long, profileId: Long) {
                withContext(Dispatchers.Main) {
                    profiles.removeAll { it.id == profileId }
                }
            }
        }
    }

    DisposableEffect(Unit) {
        loadProfiles()
        ProfileManager.addListener(listener)
        onDispose { ProfileManager.removeListener(listener) }
    }

    val lifecycleOwner = androidx.compose.ui.platform.LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = androidx.lifecycle.LifecycleEventObserver { _, event ->
            if (event == androidx.lifecycle.Lifecycle.Event.ON_RESUME) {
                loadProfiles()
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    fun batchUrlTest() {
        if (serviceRunning) {
            android.widget.Toast.makeText(
                context,
                "Stop the connection before testing profiles",
                android.widget.Toast.LENGTH_LONG,
            ).show()
            return
        }
        batchTestJob?.cancel()
        batchTestJob = scope.launch(Dispatchers.IO) {
            val groupId = DataStore.currentGroupId()
            val toTest = profiles.toList()
            if (toTest.isEmpty()) return@launch
            val total = toTest.size
            var done = 0
            var anyWorking = false
            val link = DataStore.connectionTestURL
            val timeout = DataStore.connectionTestTimeout
            withContext(Dispatchers.Main) { onBatchTestProgress(Pair(0, total)) }
            for (entity in toTest) {
                if (!isActive) break
                withContext(Dispatchers.Main) { pingingIds = pingingIds + entity.id }
                try {
                    val instance = io.nekohasekai.sagernet.bg.test.V2RayTestInstance(entity, link, timeout)
                    val result = instance.doTest()
                    entity.ping = result
                    entity.status = 1
                    entity.error = null
                    if (result > 0) anyWorking = true
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    entity.ping = -1
                    entity.status = 3
                    entity.error = e.message
                }
                ProfileManager.updateProfile(entity)
                done++
                withContext(Dispatchers.Main) {
                    pingingIds = pingingIds - entity.id
                    onBatchTestProgress(Pair(done, total))
                }
            }
            withContext(Dispatchers.Main) {
                onBatchTestProgress(null)
                batchTestJob = null
                if (!anyWorking) showDinoGame = true
            }
        }
    }

    fun cancelBatchTest() {
        batchTestJob?.cancel()
        batchTestJob = null
        pingingIds = emptySet()
        onBatchTestProgress(null)
    }

    fun closeDnsttBenchmark() {
        val runningJob = benchmarkJob
        if (runningJob?.isActive == true) {
            benchmarkRunning = true
            scope.launch(Dispatchers.IO) {
                withContext(NonCancellable) { runningJob.cancelAndJoin() }
                withContext(Dispatchers.Main) {
                    benchmarkProfile = null
                    benchmarkResults.clear()
                    benchmarkRunning = false
                    benchmarkError = null
                    benchmarkHost = null
                }
            }
            return
        }
        benchmarkJob = null
        benchmarkProfile = null
        benchmarkResults.clear()
        benchmarkRunning = false
        benchmarkError = null
        benchmarkHost = null
    }

    fun saveDnsttResolver(value: String) {
        val profileId = benchmarkProfile?.id ?: return
        val runningJob = benchmarkJob
        benchmarkJob = null
        scope.launch(Dispatchers.IO) {
            withContext(NonCancellable) { runningJob?.cancelAndJoin() }
            try {
                val profile = ProfileManager.getProfile(profileId) ?: return@launch
                val bean = profile.dnsttBean ?: return@launch
                bean.resolver = value
                ProfileManager.updateProfile(profile)
            } finally {
                withContext(Dispatchers.Main) {
                    if (benchmarkProfile?.id == profileId) closeDnsttBenchmark()
                }
            }
        }
    }

    fun runDnsttBenchmark(profile: ProxyEntity) {
        if (serviceRunning || batchTestProgress != null) {
            android.widget.Toast.makeText(
                context,
                "Stop the connection and other tests before benchmarking DNS",
                android.widget.Toast.LENGTH_LONG,
            ).show()
            return
        }
        benchmarkJob?.cancel()
        benchmarkProfile = profile
        benchmarkResults.clear()
        benchmarkRunning = true
        benchmarkError = null
        benchmarkHost = null
        val benchmarkDomain = profile.dnsttBean?.serverAddress
        val benchmarkToken = profile.dnsttBean?.token
        benchmarkJob = scope.launch(Dispatchers.IO) {
            try {
                val snapshot = DnsttBenchmark(profile).run(
                    onUpdate = { update ->
                        withContext(Dispatchers.Main) {
                            val index = benchmarkResults.indexOfFirst { it.resolver == update.resolver }
                            if (index < 0) benchmarkResults.add(update) else benchmarkResults[index] = update
                        }
                    },
                    onHostSelected = { host ->
                        withContext(Dispatchers.Main) { benchmarkHost = host }
                    },
                )
                val storedProfile = ProfileManager.getProfile(profile.id) ?: return@launch
                val bean = storedProfile.dnsttBean ?: return@launch
                if (bean.serverAddress != benchmarkDomain || bean.token != benchmarkToken) return@launch
                bean.benchmarkSnapshot = snapshot.encode()
                ProfileManager.updateProfile(storedProfile)
            } catch (error: CancellationException) {
                throw error
            } catch (error: Exception) {
                withContext(Dispatchers.Main) {
                    benchmarkError = error.message ?: "DNS benchmark failed"
                }
            } finally {
                withContext(Dispatchers.Main) {
                    if (benchmarkProfile?.id == profile.id) {
                        benchmarkRunning = false
                        benchmarkJob = null
                    }
                }
            }
        }
    }

    fun openDnsttBenchmark(profile: ProxyEntity) {
        val snapshot = decodeDnsttBenchmarkSnapshot(profile.dnsttBean?.benchmarkSnapshot.orEmpty())
        if (snapshot == null) {
            profile.dnsttBean?.benchmarkSnapshot = ""
            runDnsttBenchmark(profile)
            return
        }
        benchmarkProfile = profile
        benchmarkResults.clear()
        benchmarkResults.addAll(snapshot.results)
        benchmarkRunning = false
        benchmarkError = null
        benchmarkHost = snapshot.benchmarkHost
    }

    fun importFromClipboard() {
        val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val text = clipboard.primaryClip?.getItemAt(0)?.text?.toString() ?: return
        scope.launch(Dispatchers.IO) {
            try {
                val singleProvisioning = runCatching { parseDnsttProvisioning(text) }.getOrNull()
                val beans = if (singleProvisioning != null) listOf(DnsttBean().apply {
                    serverAddress = singleProvisioning.domain
                    token = singleProvisioning.token
                }) else parseShareLinks(text)
                if (beans.isNotEmpty()) {
                    val groupId = DataStore.selectedGroupForImport()
                    var lastId = 0L
                    beans.forEach { bean ->
                        val profile = ProfileManager.createProfile(groupId, bean)
                        lastId = profile.id
                    }
                    if (lastId > 0) DataStore.selectedProxy = lastId
                    withContext(Dispatchers.Main) {
                        android.widget.Toast.makeText(context, "Imported ${beans.size} profile(s)", android.widget.Toast.LENGTH_SHORT).show()
                    }
                } else {
                    withContext(Dispatchers.Main) {
                        android.widget.Toast.makeText(context, "No valid carrier profiles found", android.widget.Toast.LENGTH_SHORT).show()
                    }
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    android.widget.Toast.makeText(context, e.message ?: "Import failed", android.widget.Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    if (showProtocolPicker) {
        ProtocolPickerDialog(
            onSelect = { type ->
                showProtocolPicker = false
                context.startActivity(
                    Intent(context, ComposeProfileSettingsActivity::class.java).apply {
                        putExtra(ComposeProfileSettingsActivity.EXTRA_PROFILE_TYPE, type)
                    }
                )
            },
            onDismiss = { showProtocolPicker = false }
        )
    }

    if (showDinoGame) {
        io.nekohasekai.sagernet.ui.compose.components.DinoGameDialog(
            onDismiss = { showDinoGame = false }
        )
    }

    benchmarkProfile?.let { profile ->
        DnsttBenchmarkDialog(
            results = benchmarkResults,
            running = benchmarkRunning,
            error = benchmarkError,
            benchmarkHost = benchmarkHost,
            currentResolver = profile.dnsttBean?.resolver.orEmpty(),
            onSelect = { saveDnsttResolver(it.resolver.toString()) },
            onAutomatic = { saveDnsttResolver("") },
            onRetest = { runDnsttBenchmark(profile) },
            onDismiss = { closeDnsttBenchmark() },
        )
    }

    val scrollBehavior = TopAppBarDefaults.enterAlwaysScrollBehavior(rememberTopAppBarState())

    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        containerColor = MaterialTheme.colorScheme.surfaceContainer,
        topBar = {
            OwenclaveTopAppBar(
                title = "Configuration",
                scrollBehavior = scrollBehavior,
                actions = {
                    // Only offer "test all" when there is at least one profile to test.
                    if (profiles.isNotEmpty()) {
                        if (batchTestProgress != null) {
                            IconButton(
                                onClick = { cancelBatchTest() },
                            ) {
                                Icon(Icons.Filled.Close, contentDescription = "Cancel test")
                            }
                        } else {
                            IconButton(
                                enabled = !serviceRunning,
                                onClick = { batchUrlTest() },
                            ) {
                                Icon(Icons.Filled.Speed, contentDescription = "Test all")
                            }
                        }
                        Spacer(Modifier.width(6.dp))
                    }
                    IconButton(
                        onClick = { importFromClipboard() },
                    ) {
                        Icon(Icons.Filled.ContentPaste, contentDescription = "Import profile from clipboard")
                    }
                    Spacer(Modifier.width(6.dp))
                    Surface(
                        onClick = { showProtocolPicker = true },
                        shape = MaterialShapes.Cookie9Sided.toShape(),
                        color = MaterialTheme.colorScheme.primaryContainer,
                        contentColor = MaterialTheme.colorScheme.onPrimaryContainer,
                        modifier = Modifier.size(40.dp),
                    ) {
                        Box(contentAlignment = Alignment.Center) {
                            Icon(Icons.Filled.Add, contentDescription = "Add profile", modifier = Modifier.size(22.dp))
                        }
                    }
                    Spacer(Modifier.width(8.dp))
                },
            )
        },
        ) { paddingValues ->
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(paddingValues)
            ) {
                when {
                    // Keep the LazyColumn always composed (even when empty) so that
                    // adding/removing the only profile animates the card in/out via
                    // animateItem() instead of hard-swapping the whole screen.
                    else -> LazyColumn(
                        modifier = Modifier.fillMaxSize(),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(
                            top = 8.dp,
                            bottom = io.nekohasekai.sagernet.ui.compose.components.StatsBarBottomInset,
                        ),
                    ) {
                        if (profiles.isEmpty()) {
                            item(key = "__empty__") {
                                Box(
                                    modifier = Modifier
                                        .fillParentMaxSize()
                                        .animateItem(),
                                    contentAlignment = androidx.compose.ui.Alignment.Center,
                                ) {
                                    EmptyState(
                                        message = "No profiles. Tap + to add one.",
                                        icon = Icons.Filled.Add,
                                    )
                                }
                            }
                        }
                        items(profiles, key = { "${it.id}_${it.displayName()}" }) { entity ->
                            ProfileCard(
                                entity = entity,
                                selected = entity.id == selectedProfileId,
                                connected = entity.id == selectedProfileId && serviceConnected,
                                connectionStart = if (entity.id == selectedProfileId && serviceConnected) DataStore.connectionStart else 0L,
                                pinging = entity.id in pingingIds,
                                modifier = Modifier.animateItem(),
                                onClick = {
                                    val entity = entity
                                    io.nekohasekai.sagernet.ktx.runOnDefaultDispatcher {
                                        val update = selectedProfileId != entity.id
                                        if (!update) return@runOnDefaultDispatcher
                                        DataStore.selectedProxy = entity.id
                                        kotlinx.coroutines.withContext(Dispatchers.Main) {
                                            selectedProfileId = entity.id
                                        }
                                        if (serviceRunning && reloadAccess.tryLock()) {
                                            try {
                                                SagerNet.reloadService()
                                            } finally {
                                                reloadAccess.unlock()
                                            }
                                        }
                                    }
                                },
                                onEdit = {
                                    context.startActivity(
                                        Intent(context, ComposeProfileSettingsActivity::class.java).apply {
                                            putExtra(ComposeProfileSettingsActivity.EXTRA_PROFILE_ID, entity.id)
                                            putExtra(ComposeProfileSettingsActivity.EXTRA_PROFILE_TYPE, entity.type)
                                        }
                                    )
                                },
                                onShare = if (entity.hasShareLink()) {
                                    {
                                        scope.launch(Dispatchers.IO) {
                                            val link = entity.toLink()
                                            if (link != null) {
                                                val shareIntent = Intent(Intent.ACTION_SEND).apply {
                                                    type = "text/plain"
                                                    putExtra(Intent.EXTRA_TEXT, link)
                                                }
                                                withContext(Dispatchers.Main) {
                                                    context.startActivity(Intent.createChooser(shareIntent, "Share"))
                                                }
                                            }
                                        }
                                    }
                                } else null,
                                onDelete = {
                                    scope.launch(Dispatchers.IO) {
                                        val groupId = DataStore.currentGroupId()
                                        ProfileManager.deleteProfile(groupId, entity.id)
                                    }
                                },
                                onPing = if (entity.type == ProxyEntity.TYPE_DNSTT) null else {
                                    {
                                        scope.launch(Dispatchers.IO) {
                                            pingingIds = pingingIds + entity.id
                                            try {
                                                val link = DataStore.connectionTestURL
                                                val timeout = DataStore.connectionTestTimeout
                                                val instance = io.nekohasekai.sagernet.bg.test.V2RayTestInstance(
                                                    entity, link, timeout
                                                )
                                                val result = instance.doTest()
                                                entity.ping = result
                                                entity.status = 1
                                                entity.error = null
                                                ProfileManager.updateProfile(entity)
                                            } catch (e: CancellationException) {
                                                throw e
                                            } catch (e: Exception) {
                                                entity.ping = -1
                                                entity.status = 3
                                                entity.error = e.message
                                                ProfileManager.updateProfile(entity)
                                            } finally {
                                                pingingIds = pingingIds - entity.id
                                            }
                                        }
                                    }
                                },
                                onBenchmark = if (entity.type == ProxyEntity.TYPE_DNSTT) {
                                    { openDnsttBenchmark(entity) }
                                } else null,
                            )
                        }
                        item(key = "__sponsored__") {
                            Text(
                                text = "sponsored by openlibrecommunity",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f),
                                modifier = Modifier.fillMaxWidth().padding(top = 12.dp, bottom = 8.dp),
                                textAlign = androidx.compose.ui.text.style.TextAlign.Center,
                            )
                        }
                    }
                }
            }
    }
}

@Composable
private fun ProtocolPickerDialog(
    onSelect: (Int) -> Unit,
    onDismiss: () -> Unit,
) {
    val protocols = listOf(
        "DNS Tunnel" to ProxyEntity.TYPE_DNSTT,
        "OLCRTC" to ProxyEntity.TYPE_OLCRTC,
    )

    io.nekohasekai.sagernet.ui.compose.components.ExpressiveDialog(onDismissRequest = onDismiss) {
        Text(
            text = "New profile",
            style = MaterialTheme.typography.headlineSmall,
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.padding(bottom = 4.dp),
        )
        Text(
            text = "Choose a protocol",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(bottom = 16.dp),
        )
        LazyColumn(
            modifier = Modifier
                .heightIn(max = 420.dp)
                .clip(androidx.compose.foundation.shape.RoundedCornerShape(24.dp)),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            items(protocols.size) { index ->
                val (name, type) = protocols[index]
                // Uniform small radius: the outer LazyColumn clip (24dp) defines the
                // overall rounded group, so the first/last items no longer get their
                // corners harshly squared off by the scroll clip.
                androidx.compose.material3.Surface(
                    onClick = { onSelect(type) },
                    shape = androidx.compose.foundation.shape.RoundedCornerShape(6.dp),
                    color = MaterialTheme.colorScheme.surface,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                        verticalAlignment = androidx.compose.ui.Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(14.dp),
                    ) {
                        io.nekohasekai.sagernet.ui.compose.components.ShapedIconStatic(
                            icon = Icons.Filled.Add,
                            containerColor = MaterialTheme.colorScheme.secondaryContainer,
                            contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
                            size = 40.dp,
                            shape = io.nekohasekai.sagernet.ui.compose.components.shapeForSeed(name),
                        )
                        Text(
                            text = name,
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSurface,
                        )
                    }
                }
            }
        }
        Spacer(Modifier.height(12.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.End,
        ) {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        }
    }
}
