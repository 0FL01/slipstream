pub fn dotify(input: &str) -> String {
    if input.is_empty() {
        return String::new();
    }

    let bytes = input.as_bytes();
    let len = bytes.len();
    let dots = (len - 1) / 57;
    let new_len = len + dots;

    let mut buf = Vec::with_capacity(new_len);
    buf.extend_from_slice(bytes);
    buf.resize(new_len, 0);

    let mut src = len as isize - 1;
    let mut dst = new_len as isize - 1;
    let mut next_dot = len - (len % 57);
    if len.is_multiple_of(57) {
        next_dot = len - 57;
    }
    let mut current_pos = len;

    while current_pos > 0 {
        if current_pos == next_dot {
            buf[dst as usize] = b'.';
            dst -= 1;
            next_dot = next_dot.saturating_sub(57);
            current_pos -= 1;
            continue;
        }

        buf[dst as usize] = buf[src as usize];
        dst -= 1;
        src -= 1;
        current_pos -= 1;
    }

    String::from_utf8(buf).unwrap_or_default()
}

const MIN_SEGMENT: usize = 20;
const MAX_SEGMENT: usize = 57;

/// Split base32 text into labels of 20..=57 characters. The segmentation is
/// derived from `seed` so the wire shape is not a fixed 57-character pattern;
/// `max_payload_len_for_domain` reserves the worst case (all 20-character
/// segments). Short final segments are allowed.
pub fn dotify_variable(input: &str, seed: u64) -> String {
    if input.is_empty() {
        return String::new();
    }
    let mut state = seed | 1;
    let mut out = String::with_capacity(input.len() + input.len() / MIN_SEGMENT);
    let mut at = 0;
    while at < input.len() {
        let remaining = input.len() - at;
        let segment = if remaining <= MAX_SEGMENT {
            remaining
        } else {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            MIN_SEGMENT + (state as usize % (MAX_SEGMENT - MIN_SEGMENT + 1))
        };
        if at > 0 {
            out.push('.');
        }
        out.push_str(&input[at..at + segment]);
        at += segment;
    }
    out
}

pub fn undotify(input: &str) -> String {
    let mut out = Vec::with_capacity(input.len());
    for &b in input.as_bytes() {
        if b != b'.' {
            out.push(b);
        }
    }
    String::from_utf8(out).unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::{dotify, dotify_variable, undotify};

    #[test]
    fn dotify_skips_trailing_dot_for_exact_segments() {
        let input = "A".repeat(57);
        let dotted = dotify(&input);
        assert_eq!(dotted, input);
        assert!(!dotted.ends_with('.'));
    }

    #[test]
    fn variable_segments_are_bounded_and_reversible() {
        let input = "A".repeat(227);
        let dotted = dotify_variable(&input, 0x1234_5678_9abc_def0);
        assert_eq!(undotify(&dotted), input);
        let labels: Vec<&str> = dotted.split('.').collect();
        assert!(labels.len() > 1);
        for (index, label) in labels.iter().enumerate() {
            if index + 1 == labels.len() {
                assert!(!label.is_empty());
            } else {
                assert!((20..=57).contains(&label.len()), "label {}", label.len());
            }
        }
        assert_ne!(dotted, dotify(&input), "fixed 57-char pattern");
    }

    #[test]
    fn dotify_inserts_between_segments() {
        let input = "A".repeat(114);
        let dotted = dotify(&input);
        let expected = format!("{}.{}", "A".repeat(57), "A".repeat(57));
        assert_eq!(dotted, expected);
        assert!(!dotted.ends_with('.'));
    }
}
