package io.nekohasekai.sagernet.ui.compose.screens

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.unit.dp
import io.nekohasekai.sagernet.GroupOrder
import io.nekohasekai.sagernet.ui.compose.components.OwenclaveTopAppBar
import io.nekohasekai.sagernet.ui.compose.components.PreferenceHeader
import io.nekohasekai.sagernet.ui.compose.components.PreferenceItem
import io.nekohasekai.sagernet.ui.compose.components.SectionCard

data class GroupSettingsData(val name: String, val order: Int)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GroupSettingsScreen(
    groupName: String,
    initialOrder: Int = GroupOrder.ORIGIN,
    onBack: () -> Unit,
    onSave: (GroupSettingsData) -> Unit,
) {
    var name by remember { mutableStateOf(groupName) }
    var order by remember { mutableIntStateOf(initialOrder) }
    val scroll = TopAppBarDefaults.enterAlwaysScrollBehavior(rememberTopAppBarState())
    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        containerColor = MaterialTheme.colorScheme.surfaceContainer,
        topBar = {
            OwenclaveTopAppBar(
                title = "Group Settings",
                navigationIcon = Icons.AutoMirrored.Filled.ArrowBack,
                onNavigationClick = onBack,
                scrollBehavior = scroll,
                actions = { Button(onClick = { onSave(GroupSettingsData(name, order)) }) { Text("Save") } },
            )
        },
    ) { padding ->
        Box(Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState())) {
            Column(Modifier.padding(bottom = 16.dp)) {
                PreferenceHeader("General")
                SectionCard {
                    io.nekohasekai.sagernet.ui.compose.components.ExpressiveTextField(
                        value = name,
                        onValueChange = { name = it },
                        label = "Group Name",
                        modifier = Modifier.padding(16.dp),
                        singleLine = true,
                    )
                    PreferenceItem(
                        title = "Order",
                        subtitle = when (order) {
                            GroupOrder.BY_NAME -> "By Name"
                            GroupOrder.BY_DELAY -> "By delay"
                            else -> "Original"
                        },
                        onClick = { order = (order + 1) % 3 },
                    )
                }
            }
        }
    }
}
