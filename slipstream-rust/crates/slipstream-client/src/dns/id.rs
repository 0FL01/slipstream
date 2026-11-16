//! 16-bit transaction IDs for DNS queries.
//!
//! A plain `wrapping_add(1)` counter is a stable fingerprint in passive DNS
//! telemetry. This is a full-period LCG modulo 2^16 (odd increment, multiplier
//! congruent to 1 mod 4): every value appears exactly once per 65,536 queries,
//! so in-flight IDs do not collide, while consecutive deltas look random.

use openssl::rand::rand_bytes;

pub(crate) struct DnsId {
    state: u16,
    step: u16,
    add: u16,
}

impl DnsId {
    pub(crate) fn new() -> Self {
        let mut seed = [0u8; 6];
        if rand_bytes(&mut seed).is_err() {
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|duration| duration.as_nanos() as u64)
                .unwrap_or(1);
            seed.copy_from_slice(&now.to_le_bytes()[..6]);
        }
        Self {
            state: u16::from_le_bytes([seed[0], seed[1]]),
            step: (u16::from_le_bytes([seed[2], seed[3]]) & !3) | 1,
            add: u16::from_le_bytes([seed[4], seed[5]]) | 1,
        }
    }

    pub(crate) fn next(&mut self) -> u16 {
        self.state = self.state.wrapping_mul(self.step).wrapping_add(self.add);
        self.state
    }
}

#[cfg(test)]
mod tests {
    use super::DnsId;
    use std::collections::HashSet;

    #[test]
    fn covers_the_16_bit_space_without_sequential_deltas() {
        let mut ids = DnsId::new();
        let mut seen = HashSet::new();
        let mut sequential = 0usize;
        let mut previous: Option<u16> = None;
        for _ in 0..=u16::MAX {
            let id = ids.next();
            assert!(seen.insert(id), "duplicate ID {id}");
            if let Some(previous) = previous {
                if id == previous.wrapping_add(1) {
                    sequential += 1;
                }
            }
            previous = Some(id);
        }
        assert!(sequential < 100, "sequential deltas: {sequential}");
    }
}
