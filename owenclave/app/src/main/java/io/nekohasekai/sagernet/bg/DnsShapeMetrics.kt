package io.nekohasekai.sagernet.bg

import android.os.SystemClock
import io.nekohasekai.sagernet.ktx.Logs

/** Aggregate-only legit DNS shape. Never records names or payloads. */
internal class DnsShapeMetrics(
    private val elapsedRealtime: () -> Long = SystemClock::elapsedRealtime,
) {
    private val lock = Any()
    private var queries = 0L
    private var qtypeA = 0L
    private var qtypeAaaa = 0L
    private var qtypeTxt = 0L
    private var qtypeHttps = 0L
    private var qtypeOther = 0L
    private val qnameLen = LongArray(256)
    private val labelLen = LongArray(64)
    private var labelsLong = 0L
    private var label5658 = 0L
    private val unique = HashSet<Long>()
    private var lastId: Int? = null
    private var idTotal = 0L
    private var idDelta1 = 0L
    private var intervalStart = elapsedRealtime()

    fun record(data: ByteArray) {
        if (data.size < 16) return
        val qdcount = ((data[4].toInt() and 0xff) shl 8) or (data[5].toInt() and 0xff)
        if (qdcount < 1) return
        var offset = 12
        var wireLen = 1
        var labels = 0
        while (offset < data.size) {
            val length = data[offset].toInt() and 0xff
            if (length == 0) { offset++; break }
            if (length and 0xc0 != 0) { offset += 2; break }
            labels++
            wireLen += length + 1
            offset += length + 1
        }
        if (labels == 0 || offset + 4 > data.size) return
        val qtype = ((data[offset].toInt() and 0xff) shl 8) or (data[offset + 1].toInt() and 0xff)
        val id = ((data[0].toInt() and 0xff) shl 8) or (data[1].toInt() and 0xff)
        synchronized(lock) {
            queries++
            when (qtype) {
                1 -> qtypeA++
                28 -> qtypeAaaa++
                16 -> qtypeTxt++
                65 -> qtypeHttps++
                else -> qtypeOther++
            }
            qnameLen[wireLen.coerceAtMost(255)]++
            if (labels > 0) {
                // Re-walk labels only for the length histogram.
                var at = 12
                var seen = 0
                while (at < data.size && seen < labels) {
                    val length = data[at].toInt() and 0xff
                    if (length == 0 || length and 0xc0 != 0) break
                    labelLen[length.coerceAtMost(63)]++
                    if (length >= 20) {
                        labelsLong++
                        if (length in 56..58) label5658++
                    }
                    at += length + 1
                    seen++
                }
            }
            unique.add(fnv1a(data, offset))
            lastId?.let { previous ->
                idTotal++
                if (id == ((previous + 1) and 0xffff)) idDelta1++
            }
            lastId = id
        }
        maybeReport()
    }

    fun recordLookup(domain: String, network: String) {
        if (domain.isEmpty()) return
        val labels = domain.trimEnd('.').split('.').filter { it.isNotEmpty() }
        if (labels.isEmpty()) return
        val wireLen = labels.sumOf { it.length + 1 } + 1
        synchronized(lock) {
            queries++
            if (network.endsWith("6")) qtypeAaaa++ else qtypeA++
            qnameLen[wireLen.coerceAtMost(255)]++
            for (label in labels) {
                val length = label.length.coerceAtMost(63)
                labelLen[length]++
                if (length >= 20) {
                    labelsLong++
                    if (length in 56..58) label5658++
                }
            }
            unique.add(fnv1aString(domain))
        }
        maybeReport()
    }

    private fun maybeReport() {
        val due = synchronized(lock) { elapsedRealtime() - intervalStart >= 60_000 }
        if (due) report()
    }

    fun report() {
        val now = elapsedRealtime()
        synchronized(lock) {
            val elapsed = (now - intervalStart).coerceAtLeast(1)
            if (queries == 0L) {
                intervalStart = now
                return
            }
            val seconds = elapsed / 1000.0
            val qps = queries / seconds
            val uniquePerMin = unique.size * 60.0 / seconds
            fun pct(hist: LongArray, pct: Double): Int {
                val total = hist.sum()
                if (total == 0L) return 0
                val target = (total * pct / 100.0).toLong().coerceAtLeast(1)
                var seen = 0L
                for (index in hist.indices) {
                    seen += hist[index]
                    if (seen >= target) return index
                }
                return hist.size - 1
            }
            fun bucket(hist: LongArray, from: Int, to: Int) = (from..to).sumOf { hist[it] }
            val labelShare = if (labelsLong == 0L) 0.0 else label5658.toDouble() / labelsLong
            val idShare = if (idTotal == 0L) 0.0 else idDelta1.toDouble() / idTotal
            Logs.i(
                "dpi-stats: source=legit ts_unix=${System.currentTimeMillis() / 1000} " +
                    "interval_ms=$elapsed queries=$queries qps=" + "%.2f".format(qps) +
                    " qtype_a=$qtypeA qtype_aaaa=$qtypeAaaa qtype_txt=$qtypeTxt" +
                    " qtype_https=$qtypeHttps qtype_other=$qtypeOther" +
                    " qname_le64=${bucket(qnameLen, 0, 64)}" +
                    " qname_65_128=${bucket(qnameLen, 65, 128)}" +
                    " qname_129_192=${bucket(qnameLen, 129, 192)}" +
                    " qname_gt192=${bucket(qnameLen, 193, 255)}" +
                    " qname_p50=${pct(qnameLen, 50.0)} qname_p95=${pct(qnameLen, 95.0)}" +
                    " labels_long=$labelsLong label_56_58=$label5658" +
                    " label_56_58_share=" + "%.3f".format(labelShare) +
                    " label_p50=${pct(labelLen, 50.0)} label_p95=${pct(labelLen, 95.0)}" +
                    " unique_qnames=${unique.size} unique_per_min=" + "%.1f".format(uniquePerMin) +
                    " id_delta1=$idDelta1 id_total=$idTotal id_delta1_share=" + "%.3f".format(idShare),
            )
            intervalStart = now
            queries = 0
            qtypeA = 0; qtypeAaaa = 0; qtypeTxt = 0; qtypeHttps = 0; qtypeOther = 0
            qnameLen.fill(0L); labelLen.fill(0L)
            labelsLong = 0; label5658 = 0
            unique.clear()
            idTotal = 0; idDelta1 = 0
        }
    }

    private fun fnv1a(data: ByteArray, end: Int): Long {
        var hash = -0x340d631b7bdddcdbL
        var index = 12
        while (index < end) {
            hash = hash xor (data[index].toLong() and 0xff)
            hash *= 0x100000001b3L
            index++
        }
        return hash
    }

    private fun fnv1aString(value: String): Long {
        var hash = -0x340d631b7bdddcdbL
        for (byte in value.toByteArray()) {
            hash = hash xor (byte.toLong() and 0xff)
            hash *= 0x100000001b3L
        }
        return hash
    }
}
