//! Aggregate-only DPI shape metrics. Never record QNAMEs or payloads.

use std::collections::HashSet;
use std::fmt::Write as _;
use std::time::{SystemTime, UNIX_EPOCH};

const REPORT_INTERVAL_US: u64 = 10_000_000;

const RR_A: u16 = 1;
const RR_TXT: u16 = 16;
const RR_AAAA: u16 = 28;
const RR_HTTPS: u16 = 65;

pub(crate) struct DpiStats {
    enabled: bool,
    zone_labels: usize,
    last_report_us: u64,
    interval_start_us: u64,
    queries: u64,
    qtype_a: u64,
    qtype_aaaa: u64,
    qtype_txt: u64,
    qtype_https: u64,
    qtype_other: u64,
    qname_len: [u64; 256],
    label_len: [u64; 64],
    labels_long: u64,
    label_56_58: u64,
    unique: HashSet<u64>,
    last_id: Option<u16>,
    id_total: u64,
    id_delta1: u64,
    responses: u64,
    txt_total: u64,
    txt_binary: u64,
    resp_le128: u64,
    resp_le512: u64,
    resp_le1024: u64,
    resp_gt1024: u64,
}

impl DpiStats {
    pub(crate) fn new(enabled: bool, domain: &str) -> Self {
        Self {
            enabled,
            zone_labels: domain.trim_end_matches('.').split('.').count(),
            last_report_us: 0,
            interval_start_us: 0,
            queries: 0,
            qtype_a: 0,
            qtype_aaaa: 0,
            qtype_txt: 0,
            qtype_https: 0,
            qtype_other: 0,
            qname_len: [0; 256],
            label_len: [0; 64],
            labels_long: 0,
            label_56_58: 0,
            unique: HashSet::new(),
            last_id: None,
            id_total: 0,
            id_delta1: 0,
            responses: 0,
            txt_total: 0,
            txt_binary: 0,
            resp_le128: 0,
            resp_le512: 0,
            resp_le1024: 0,
            resp_gt1024: 0,
        }
    }

    pub(crate) fn record_query(&mut self, qname: &str, id: u16, qtype: u16, _now: u64) {
        if !self.enabled {
            return;
        }
        self.queries += 1;
        match qtype {
            RR_A => self.qtype_a += 1,
            RR_AAAA => self.qtype_aaaa += 1,
            RR_TXT => self.qtype_txt += 1,
            RR_HTTPS => self.qtype_https += 1,
            _ => self.qtype_other += 1,
        }

        let labels: Vec<&str> = qname
            .trim_end_matches('.')
            .split('.')
            .filter(|label| !label.is_empty())
            .collect();
        let wire_len: usize = labels.iter().map(|label| label.len() + 1).sum::<usize>() + 1;
        self.qname_len[wire_len.min(255)] += 1;

        let payload_labels = labels.len().saturating_sub(self.zone_labels);
        for label in labels.iter().take(payload_labels) {
            let len = label.len().min(63);
            self.label_len[len] += 1;
            if len >= 20 {
                self.labels_long += 1;
                if (56..=58).contains(&len) {
                    self.label_56_58 += 1;
                }
            }
        }

        self.unique.insert(fnv1a(qname.as_bytes()));
        if let Some(previous) = self.last_id {
            self.id_total += 1;
            if id == previous.wrapping_add(1) {
                self.id_delta1 += 1;
            }
        }
        self.last_id = Some(id);
    }

    pub(crate) fn record_response(&mut self, dns_len: usize, payload: Option<&[u8]>) {
        if !self.enabled {
            return;
        }
        self.responses += 1;
        match dns_len {
            0..=128 => self.resp_le128 += 1,
            129..=512 => self.resp_le512 += 1,
            513..=1024 => self.resp_le1024 += 1,
            _ => self.resp_gt1024 += 1,
        }
        if let Some(payload) = payload {
            self.txt_total += 1;
            if payload
                .iter()
                .any(|byte| !(0x20..=0x7e).contains(byte) && !matches!(byte, b'\t' | b'\n' | b'\r'))
            {
                self.txt_binary += 1;
            }
        }
    }

    pub(crate) fn maybe_report(&mut self, now: u64) {
        if !self.enabled {
            return;
        }
        if self.last_report_us == 0 {
            self.last_report_us = now;
            self.interval_start_us = now;
            return;
        }
        if now.saturating_sub(self.last_report_us) < REPORT_INTERVAL_US {
            return;
        }
        let elapsed = now.saturating_sub(self.interval_start_us).max(1);
        let seconds = elapsed as f64 / 1_000_000.0;
        let qps = self.queries as f64 / seconds;
        let unique_per_min = self.unique.len() as f64 * 60.0 / seconds;
        let qname_p50 = percentile(&self.qname_len, 50.0);
        let qname_p95 = percentile(&self.qname_len, 95.0);
        let label_p50 = percentile(&self.label_len, 50.0);
        let label_p95 = percentile(&self.label_len, 95.0);
        let txt_binary_share = if self.txt_total == 0 {
            0.0
        } else {
            self.txt_binary as f64 / self.txt_total as f64
        };
        let id_delta1_share = if self.id_total == 0 {
            0.0
        } else {
            self.id_delta1 as f64 / self.id_total as f64
        };
        let label_56_58_share = if self.labels_long == 0 {
            0.0
        } else {
            self.label_56_58 as f64 / self.labels_long as f64
        };

        let mut line = String::with_capacity(512);
        let _ = write!(
            line,
            "dpi-stats: source=tunnel ts_unix={} interval_ms={} queries={} qps={:.2} \
             qtype_a={} qtype_aaaa={} qtype_txt={} qtype_https={} qtype_other={} \
             qname_le64={} qname_65_128={} qname_129_192={} qname_gt192={} \
             qname_p50={} qname_p95={} labels_long={} label_56_58={} label_56_58_share={:.3} \
             label_p50={} label_p95={} unique_qnames={} unique_per_min={:.1} \
             id_delta1={} id_total={} id_delta1_share={:.3} \
             txt_total={} txt_binary={} txt_binary_share={:.3} \
             resp_le128={} resp_129_512={} resp_513_1024={} resp_gt1024={} lanes={}",
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|duration| duration.as_secs())
                .unwrap_or(0),
            elapsed / 1_000,
            self.queries,
            qps,
            self.qtype_a,
            self.qtype_aaaa,
            self.qtype_txt,
            self.qtype_https,
            self.qtype_other,
            bucket(&self.qname_len, 0..=64),
            bucket(&self.qname_len, 65..=128),
            bucket(&self.qname_len, 129..=192),
            bucket(&self.qname_len, 193..=255),
            qname_p50,
            qname_p95,
            self.labels_long,
            self.label_56_58,
            label_56_58_share,
            label_p50,
            label_p95,
            self.unique.len(),
            unique_per_min,
            self.id_delta1,
            self.id_total,
            id_delta1_share,
            self.txt_total,
            self.txt_binary,
            txt_binary_share,
            self.resp_le128,
            self.resp_le512,
            self.resp_le1024,
            self.resp_gt1024,
            crate::tcp_bridge::active_lanes(),
        );
        eprintln!("{line}");
        self.last_report_us = now;
        self.interval_start_us = now;
        self.queries = 0;
        self.qtype_a = 0;
        self.qtype_aaaa = 0;
        self.qtype_txt = 0;
        self.qtype_https = 0;
        self.qtype_other = 0;
        self.qname_len = [0; 256];
        self.label_len = [0; 64];
        self.labels_long = 0;
        self.label_56_58 = 0;
        self.unique.clear();
        self.id_total = 0;
        self.id_delta1 = 0;
        self.responses = 0;
        self.txt_total = 0;
        self.txt_binary = 0;
        self.resp_le128 = 0;
        self.resp_le512 = 0;
        self.resp_le1024 = 0;
        self.resp_gt1024 = 0;
    }
}

fn bucket(hist: &[u64; 256], range: std::ops::RangeInclusive<usize>) -> u64 {
    range.map(|index| hist[index]).sum()
}

fn percentile(hist: &[u64], pct: f64) -> usize {
    let total: u64 = hist.iter().sum();
    if total == 0 {
        return 0;
    }
    let target = ((total as f64) * pct / 100.0).ceil().max(1.0) as u64;
    let mut seen = 0u64;
    for (value, count) in hist.iter().enumerate() {
        seen += count;
        if seen >= target {
            return value;
        }
    }
    hist.len().saturating_sub(1)
}

fn fnv1a(bytes: &[u8]) -> u64 {
    let mut hash = 0xcbf2_9ce4_8422_2325u64;
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    hash
}
