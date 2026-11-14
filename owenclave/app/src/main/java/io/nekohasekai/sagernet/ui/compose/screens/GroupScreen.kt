package io.nekohasekai.sagernet.ui.compose.screens

import android.content.Intent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Link
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialShapes
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.material3.toShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import io.nekohasekai.sagernet.SagerNet
import io.nekohasekai.sagernet.database.DataStore
import io.nekohasekai.sagernet.database.GroupManager
import io.nekohasekai.sagernet.database.ProxyGroup
import io.nekohasekai.sagernet.database.SagerDatabase
import io.nekohasekai.sagernet.ui.compose.ComposeGroupSettingsActivity
import io.nekohasekai.sagernet.ui.compose.components.EmptyState
import io.nekohasekai.sagernet.ui.compose.components.GroupCard
import io.nekohasekai.sagernet.ui.compose.components.LoadingState
import io.nekohasekai.sagernet.ui.compose.components.OwenclaveTopAppBar
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class, ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun GroupScreen(onMenuClick: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val lifecycleOwner = LocalLifecycleOwner.current
    var groups by remember { mutableStateOf<List<ProxyGroup>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var selectedGroupId by remember { mutableStateOf(DataStore.selectedGroup) }
    var deleteGroup by remember { mutableStateOf<ProxyGroup?>(null) }
    var clearGroup by remember { mutableStateOf<ProxyGroup?>(null) }

    fun reload() {
        scope.launch(Dispatchers.IO) {
            groups = SagerDatabase.groupDao.allGroups()
            loading = false
        }
    }

    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event -> if (event == Lifecycle.Event.ON_RESUME) reload() }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    deleteGroup?.let { group ->
        io.nekohasekai.sagernet.ui.compose.components.ExpressiveDialog(
            onDismissRequest = { deleteGroup = null },
        ) {
            Text("Delete group", style = MaterialTheme.typography.headlineSmall)
            Spacer(Modifier.height(16.dp))
            Text("Delete ${group.displayName()} and all its profiles?")
            Spacer(Modifier.height(16.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                TextButton(onClick = { deleteGroup = null }) { Text("Cancel") }
                TextButton(onClick = {
                    deleteGroup = null
                    scope.launch(Dispatchers.IO) { GroupManager.deleteGroup(group.id); reload() }
                }) { Text("Delete") }
            }
        }
    }

    clearGroup?.let { group ->
        io.nekohasekai.sagernet.ui.compose.components.ExpressiveDialog(
            onDismissRequest = { clearGroup = null },
        ) {
            Text("Clear profiles", style = MaterialTheme.typography.headlineSmall)
            Spacer(Modifier.height(16.dp))
            Text("Remove all profiles from ${group.displayName()}?")
            Spacer(Modifier.height(16.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                TextButton(onClick = { clearGroup = null }) { Text("Cancel") }
                TextButton(onClick = {
                    clearGroup = null
                    scope.launch(Dispatchers.IO) { GroupManager.clearGroup(group.id); reload() }
                }) { Text("Clear") }
            }
        }
    }

    val scrollBehavior = TopAppBarDefaults.enterAlwaysScrollBehavior(rememberTopAppBarState())
    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        containerColor = MaterialTheme.colorScheme.surfaceContainer,
        topBar = {
            OwenclaveTopAppBar(
                title = "Groups",
                scrollBehavior = scrollBehavior,
                actions = {
                    Surface(
                        onClick = {
                            context.startActivity(Intent(context, ComposeGroupSettingsActivity::class.java))
                        },
                        shape = MaterialShapes.Cookie9Sided.toShape(),
                        color = MaterialTheme.colorScheme.primaryContainer,
                        modifier = Modifier.size(40.dp),
                    ) { Box(contentAlignment = Alignment.Center) { Icon(Icons.Filled.Add, "Add") } }
                    Spacer(Modifier.width(8.dp))
                },
            )
        },
    ) { padding ->
        Box(Modifier.fillMaxSize().padding(padding)) {
            when {
                loading -> LoadingState()
                groups.isEmpty() -> EmptyState(
                    message = "No groups. Tap + to create one.",
                    icon = Icons.Filled.Add,
                )
                else -> LazyColumn(contentPadding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                    items(groups, key = { it.id }) { group ->
                        GroupCard(
                            group = group,
                            selected = group.id == selectedGroupId,
                            profileCount = SagerDatabase.proxyDao.countByGroup(group.id).toInt(),
                            onClick = { selectedGroupId = group.id; DataStore.selectedGroup = group.id },
                            onEdit = {
                                context.startActivity(Intent(context, ComposeGroupSettingsActivity::class.java).apply {
                                    putExtra("groupId", group.id)
                                })
                            },
                            menuItems = { menu ->
                                DropdownMenuItem(
                                    text = { Text("Copy profile links") },
                                    onClick = {
                                        menu.dismiss()
                                        scope.launch(Dispatchers.IO) {
                                            val links = SagerDatabase.proxyDao.getByGroup(group.id)
                                                .mapNotNull { it.toLink() }.joinToString("\n")
                                            withContext(Dispatchers.Main) { SagerNet.trySetPrimaryClip(links) }
                                        }
                                    },
                                    leadingIcon = { Icon(Icons.Filled.Link, null) },
                                )
                                DropdownMenuItem(
                                    text = { Text("Clear profiles") },
                                    onClick = { menu.dismiss(); clearGroup = group },
                                    leadingIcon = { Icon(Icons.Filled.Delete, null) },
                                )
                                DropdownMenuItem(
                                    text = { Text("Delete group") },
                                    onClick = { menu.dismiss(); deleteGroup = group },
                                    leadingIcon = { Icon(Icons.Filled.Delete, null) },
                                )
                            },
                        )
                    }
                }
            }
        }
    }
}
