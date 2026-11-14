package io.nekohasekai.sagernet.ui.compose.components

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import io.nekohasekai.sagernet.bg.test.DnsttBenchmarkResult
import io.nekohasekai.sagernet.bg.test.sortedDnsttBenchmarkResults
import java.util.Locale

@Composable
internal fun DnsttBenchmarkDialog(
    results: List<DnsttBenchmarkResult>,
    running: Boolean,
    error: String?,
    benchmarkHost: String?,
    currentResolver: String,
    onSelect: (DnsttBenchmarkResult) -> Unit,
    onAutomatic: () -> Unit,
    onRetest: () -> Unit,
    onDismiss: () -> Unit,
) {
    val sorted = results.sortedDnsttBenchmarkResults()
    var selectionMade by remember { mutableStateOf(false) }
    fun selectOnce(action: () -> Unit) {
        if (selectionMade) return
        selectionMade = true
        action()
    }
    ExpressiveDialog(onDismissRequest = onDismiss) {
        Text("DNS benchmark", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(6.dp))
        Text(
            buildString {
                append("TCP transport · One run per resolver, up to 512 KiB or 3 seconds. ")
                append(when {
                    benchmarkHost != null -> "Test server: $benchmarkHost."
                    running -> "Selecting benchmark server…"
                    else -> "No benchmark server selected."
                })
            },
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.height(16.dp))

        if (running && results.isEmpty()) {
            CircularProgressIndicator(modifier = Modifier.align(Alignment.CenterHorizontally))
        }
        Column(
            modifier = Modifier.heightIn(max = 420.dp).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            sorted.forEach { result ->
                val selectable = !selectionMade && result.complete
                Surface(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable(enabled = selectable) {
                            selectOnce { onSelect(result) }
                        },
                    shape = MaterialTheme.shapes.large,
                    color = if (result.resolver.toString() == currentResolver) {
                        MaterialTheme.colorScheme.secondaryContainer
                    } else {
                        MaterialTheme.colorScheme.surfaceContainerHighest
                    },
                ) {
                    Column(Modifier.padding(horizontal = 14.dp, vertical = 10.dp)) {
                        Text(
                            buildString {
                                append(result.label)
                                if (result.experimental) append(" · Experimental")
                                append("\n")
                                append(result.resolver)
                            },
                            style = MaterialTheme.typography.titleSmall,
                        )
                        Text(
                            result.summary(),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }

        if (error != null) {
            Spacer(Modifier.height(10.dp))
            Text(error, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
        }
        Spacer(Modifier.height(12.dp))
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
            if (!running && results.isNotEmpty()) {
                TextButton(onClick = onRetest) { Text("Retest") }
            }
            TextButton(
                enabled = !selectionMade,
                onClick = { selectOnce(onAutomatic) },
            ) { Text("Automatic TCP") }
            TextButton(onClick = onDismiss) { Text(if (running) "Cancel" else "Close") }
        }
    }
}

private fun DnsttBenchmarkResult.summary() = when {
    failure != null -> failure
    !complete -> "Connecting…"
    else -> String.format(Locale.US, "%.2f Mbit/s · %d ms", speedMbps, latencyMs)
}
