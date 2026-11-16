mod base32;
mod codec;
mod dots;
mod name;
mod types;
mod wire;

pub use base32::{decode as base32_decode, encode as base32_encode, Base32Error};
pub use codec::{
    decode_query, decode_query_with_domains, decode_response, encode_query, encode_response,
    is_response,
};
pub use dots::{dotify, undotify};
pub use types::{
    DecodeQueryError, DecodedQuery, DnsError, QueryParams, Question, Rcode, ResponseParams,
    CLASS_IN, EDNS_UDP_PAYLOAD, RR_A, RR_OPT, RR_TXT,
};

pub fn build_qname(payload: &[u8], domain: &str) -> Result<String, DnsError> {
    build_qname_with(payload, domain, false)
}

/// Lowercase base32 split into 20..=57 character labels. The segmentation is
/// derived from the payload, so retransmissions keep the same wire shape while
/// different packets do not all use the fixed 57-character pattern.
pub fn build_qname_variable(payload: &[u8], domain: &str) -> Result<String, DnsError> {
    build_qname_with(payload, domain, true)
}

fn build_qname_with(payload: &[u8], domain: &str, variable: bool) -> Result<String, DnsError> {
    let domain = domain.trim_end_matches('.');
    if domain.is_empty() {
        return Err(DnsError::new("domain must not be empty"));
    }
    let max_payload = if variable {
        max_payload_len_for_domain_variable(domain)?
    } else {
        max_payload_len_for_domain(domain)?
    };
    if payload.len() > max_payload {
        return Err(DnsError::new("payload too large for domain"));
    }
    let base32 = base32_encode(payload);
    let dotted = if variable {
        dots::dotify_variable(&base32, fnv1a(payload))
    } else {
        dotify(&base32)
    };
    Ok(format!("{}.{}.", dotted, domain))
}

pub fn max_payload_len_for_domain(domain: &str) -> Result<usize, DnsError> {
    max_payload_len_for_domain_with_segment(domain, 57)
}

pub fn max_payload_len_for_domain_variable(domain: &str) -> Result<usize, DnsError> {
    max_payload_len_for_domain_with_segment(domain, 20)
}

fn max_payload_len_for_domain_with_segment(
    domain: &str,
    min_segment: usize,
) -> Result<usize, DnsError> {
    let domain = domain.trim_end_matches('.');
    if domain.is_empty() {
        return Err(DnsError::new("domain must not be empty"));
    }
    if domain.len() > name::MAX_DNS_NAME_LEN {
        return Err(DnsError::new("domain too long"));
    }
    let max_name_len = name::MAX_DNS_NAME_LEN;
    let max_dotted_len = max_name_len.saturating_sub(domain.len() + 1);
    if max_dotted_len == 0 {
        return Ok(0);
    }
    let mut max_base32_len = 0usize;
    for len in 1..=max_dotted_len {
        let dots = (len - 1) / min_segment;
        if len + dots > max_dotted_len {
            break;
        }
        max_base32_len = len;
    }

    let mut max_payload = (max_base32_len * 5) / 8;
    while max_payload > 0 && base32_len(max_payload) > max_base32_len {
        max_payload -= 1;
    }
    Ok(max_payload)
}

fn fnv1a(bytes: &[u8]) -> u64 {
    let mut hash = 0xcbf2_9ce4_8422_2325u64;
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    hash
}

fn base32_len(payload_len: usize) -> usize {
    if payload_len == 0 {
        return 0;
    }
    (payload_len * 8).div_ceil(5)
}

#[cfg(test)]
mod tests {
    use super::{build_qname, max_payload_len_for_domain};

    #[test]
    fn build_qname_rejects_payload_overflow() {
        let domain = "test.com";
        let max_payload = max_payload_len_for_domain(domain).expect("max payload");
        let payload = vec![0u8; max_payload + 1];
        assert!(build_qname(&payload, domain).is_err());
    }

    #[test]
    fn variable_qname_is_lowercase_and_fits() {
        let domain = "t.test";
        let max_payload = super::max_payload_len_for_domain_variable(domain).expect("max payload");
        assert!(max_payload > 0);
        let payload: Vec<u8> = (0..max_payload).map(|i| i as u8).collect();
        let qname = super::build_qname_variable(&payload, domain).expect("qname");
        assert!(qname.ends_with(".t.test."));
        assert!(qname
            .trim_end_matches('.')
            .split('.')
            .all(|label| !label.is_empty() && label.len() <= 63));
        assert!(qname
            .chars()
            .all(|c| c == '.' || c.is_ascii_lowercase() || c.is_ascii_digit()));
        assert!(qname.len() <= 255);
    }

    #[test]
    fn build_qname_rejects_long_domain() {
        let domain = format!("{}.com", "a".repeat(260));
        let payload = vec![0u8; 1];
        assert!(build_qname(&payload, &domain).is_err());
    }
}
