package io.nekohasekai.sagernet.ui.compose.screens

import android.app.Activity
import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import io.nekohasekai.sagernet.database.ProxyEntity
import io.nekohasekai.sagernet.fmt.dnstt.DnsttBean
import io.nekohasekai.sagernet.fmt.dnstt.isValidDnsttDomain
import io.nekohasekai.sagernet.fmt.dnstt.isValidDnsttResolver
import io.nekohasekai.sagernet.fmt.dnstt.isValidDnsttToken
import io.nekohasekai.sagernet.ui.ScannerActivity
import io.nekohasekai.sagernet.ui.compose.components.DividerItem
import io.nekohasekai.sagernet.ui.compose.components.OwenclaveTopAppBar
import io.nekohasekai.sagernet.ui.compose.components.PreferenceHeader
import io.nekohasekai.sagernet.ui.compose.components.SectionCard

data class ProfileFieldState(
    val name: String = "",
    val iconIndex: Int = -1,
    val token: String = "",
    val dnsttDomain: String = DnsttBean.LEGACY_DOMAIN,
    val dnsttManual: Boolean = false,
    val dnsttResolver: String = "",
    val authProvider: String = "jitsi",
    val transport: String = "datachannel",
    val roomId: String = "",
    val encryptionKey: String = "",
    val dnsServer: String = "8.8.8.8:53",
    val socksHost: String = "127.0.0.1",
    val socksPort: String = "8808",
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun UniversalProfileSettingsScreen(
    profileType: Int,
    initialState: ProfileFieldState,
    onBack: () -> Unit,
    onSave: (ProfileFieldState) -> Unit,
) {
    require(profileType == ProxyEntity.TYPE_DNSTT || profileType == ProxyEntity.TYPE_OLCRTC)
    var state by remember { mutableStateOf(initialState) }
    val canSave = profileType != ProxyEntity.TYPE_DNSTT ||
        isValidDnsttToken(state.token) && isValidDnsttDomain(state.dnsttDomain) &&
        (!state.dnsttManual || isValidDnsttResolver(state.dnsttResolver))
    val title = if (profileType == ProxyEntity.TYPE_DNSTT) "DNS Tunnel" else "OLCRTC"
    val scrollBehavior = TopAppBarDefaults.enterAlwaysScrollBehavior(rememberTopAppBarState())

    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        containerColor = MaterialTheme.colorScheme.surfaceContainer,
        topBar = {
            OwenclaveTopAppBar(
                title = "$title Settings",
                navigationIcon = Icons.AutoMirrored.Filled.ArrowBack,
                onNavigationClick = onBack,
                scrollBehavior = scrollBehavior,
                actions = {
                    Button(onClick = { onSave(state) }, enabled = canSave) { Text("Save") }
                },
            )
        },
    ) { paddingValues ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .verticalScroll(rememberScrollState()),
        ) {
            Column(modifier = Modifier.padding(bottom = 16.dp)) {
                SectionCard {
                    ProfileTextField("Profile Name", state.name) { state = state.copy(name = it) }
                    DividerItem()
                    ProfileIconPicker(state.iconIndex, state.name) {
                        state = state.copy(iconIndex = it)
                    }
                }
                if (profileType == ProxyEntity.TYPE_DNSTT) {
                    DnsttFields(state) { state = it }
                } else {
                    OlcrtcFields(state) { state = it }
                }
            }
        }
    }
}

@Composable
private fun ProfileTextField(
    label: String,
    value: String,
    keyboardType: KeyboardType = KeyboardType.Text,
    password: Boolean = false,
    onValueChange: (String) -> Unit,
) {
    io.nekohasekai.sagernet.ui.compose.components.ExpressiveTextField(
        value = value,
        onValueChange = onValueChange,
        label = label,
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        singleLine = true,
        keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(
            keyboardType = if (password) KeyboardType.Password else keyboardType,
        ),
        visualTransformation = if (password) PasswordVisualTransformation()
        else androidx.compose.ui.text.input.VisualTransformation.None,
    )
}

@Composable
private fun OlcrtcFields(state: ProfileFieldState, update: (ProfileFieldState) -> Unit) {
    PreferenceHeader("OLCRTC Settings")
    SectionCard {
        ProfileTextField("Auth Provider", state.authProvider) { update(state.copy(authProvider = it)) }
        DividerItem()
        ProfileTextField("Transport", state.transport) { update(state.copy(transport = it)) }
        DividerItem()
        ProfileTextField("Room ID", state.roomId) { update(state.copy(roomId = it)) }
        DividerItem()
        ProfileTextField("Encryption Key", state.encryptionKey, password = true) {
            update(state.copy(encryptionKey = it))
        }
        DividerItem()
        ProfileTextField("DNS Server", state.dnsServer) { update(state.copy(dnsServer = it)) }
        DividerItem()
        ProfileTextField("SOCKS Host", state.socksHost) { update(state.copy(socksHost = it)) }
        DividerItem()
        ProfileTextField("SOCKS Port", state.socksPort, keyboardType = KeyboardType.Number) {
            update(state.copy(socksPort = it.filter(Char::isDigit)))
        }
    }
}

@Composable
private fun DnsttFields(state: ProfileFieldState, update: (ProfileFieldState) -> Unit) {
    val context = LocalContext.current
    val scanner = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        val token = result.data?.getStringExtra(ScannerActivity.EXTRA_TOKEN)
        val domain = result.data?.getStringExtra(ScannerActivity.EXTRA_DOMAIN)
        if (result.resultCode == Activity.RESULT_OK && token != null && domain != null) {
            update(state.copy(token = token, dnsttDomain = domain))
        }
    }
    PreferenceHeader("DNS Tunnel")
    SectionCard {
        ProfileTextField("Tunnel Domain", state.dnsttDomain) {
            update(state.copy(dnsttDomain = it))
        }
        DividerItem()
        ProfileTextField("Flow Token (32 lowercase hex)", state.token, password = true) {
            update(state.copy(token = it))
        }
        DividerItem()
        Button(onClick = {
            scanner.launch(Intent(context, ScannerActivity::class.java).apply {
                putExtra(ScannerActivity.EXTRA_TOKEN_ONLY, true)
                putExtra(ScannerActivity.EXTRA_CURRENT_DOMAIN, state.dnsttDomain)
            })
        }) { Text("Scan provisioning key or token") }
        DividerItem()
        io.nekohasekai.sagernet.ui.compose.components.SwitchPreferenceItem(
            title = "Manual DNS resolver",
            checked = state.dnsttManual,
            subtitle = "Automatic uses TCP",
            onCheckedChange = {
                update(state.copy(dnsttManual = it, dnsttResolver = if (it) state.dnsttResolver else ""))
            },
        )
        if (state.dnsttManual) {
            DividerItem()
            ProfileTextField("DNS Resolver (udp:// or tcp://)", state.dnsttResolver) {
                update(state.copy(dnsttResolver = it))
            }
            Text("Experimental Yandex Safe .88 + Basic .1: tcp+mp://77.88.8.88:53")
        }
    }
}

@Composable
private fun ProfileIconPicker(selected: Int, name: String, onSelect: (Int) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    val currentIcon = io.nekohasekai.sagernet.ui.compose.components.profileIconFor(selected, name)
    Box {
        Row(
            modifier = Modifier.fillMaxWidth().clickable { expanded = true }
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            io.nekohasekai.sagernet.ui.compose.components.ShapedIconStatic(
                icon = currentIcon,
                containerColor = MaterialTheme.colorScheme.secondaryContainer,
                contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
                size = 40.dp,
            )
            Spacer(Modifier.padding(start = 16.dp))
            Text(
                if (selected == -1) "Icon (Auto)" else "Icon",
                style = MaterialTheme.typography.bodyLarge,
                modifier = Modifier.weight(1f),
            )
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
        }
        androidx.compose.material3.DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
            shape = androidx.compose.foundation.shape.RoundedCornerShape(24.dp),
            containerColor = MaterialTheme.colorScheme.surfaceContainerHigh,
        ) {
            androidx.compose.foundation.layout.FlowRow(
                modifier = Modifier.padding(12.dp).width(220.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                val icons = listOf(null) + io.nekohasekai.sagernet.ui.compose.components.ProfileIconSet
                icons.forEachIndexed { index, icon ->
                    val value = index - 1
                    val isSelected = value == selected
                    Box(
                        modifier = Modifier.size(44.dp)
                            .clip(if (isSelected) androidx.compose.foundation.shape.RoundedCornerShape(12.dp) else CircleShape)
                            .background(if (isSelected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceContainerHighest)
                            .clickable { onSelect(value); expanded = false },
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            icon ?: Icons.Filled.AutoAwesome,
                            contentDescription = null,
                            tint = if (isSelected) MaterialTheme.colorScheme.onPrimaryContainer else MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.size(22.dp),
                        )
                    }
                }
            }
        }
    }
}
