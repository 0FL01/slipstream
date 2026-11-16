//! Keyed obfuscation of the DNS payload.
//!
//! QUIC already protects the tunnel content. This layer only removes the
//! recognisable QUIC header from QNAME/TXT bytes and lets the server answer
//! unauthenticated probes like ordinary nonexistent names. It is not a
//! replacement for QUIC's cryptography.
//!
//! Wire format: 4-byte random nonce, then SHAKE128(key || dir || nonce) XOR
//! (2-byte magic || payload). The magic stays inside the ciphertext. A
//! plaintext counter would put a stable base32 prefix into every QNAME and TXT
//! response, so an unpredictable nonce is used instead; a nonce collision only
//! mixes two already-encrypted QUIC packets.

use openssl::hash::{Hasher, MessageDigest};
use openssl::rand::rand_bytes;

pub(crate) const MAGIC: [u8; 2] = [0x53, 0x4c];
const NONCE_LEN: usize = 4;
const OVERHEAD: usize = NONCE_LEN + MAGIC.len();
const DIR_QUERY: u8 = 1;
const DIR_ANSWER: u8 = 2;

pub(crate) fn derive_key(token: &[u8; 16]) -> [u8; 32] {
    let mut hasher = Hasher::new(MessageDigest::sha256()).expect("sha256");
    hasher.update(b"slipstream-dns-v1").expect("hash update");
    hasher.update(token).expect("hash update");
    let digest = hasher.finish().expect("hash finish");
    let mut key = [0u8; 32];
    key.copy_from_slice(&digest);
    key
}

fn stream(key: &[u8; 32], dir: u8, nonce: &[u8], len: usize) -> Vec<u8> {
    let mut hasher = Hasher::new(MessageDigest::shake_128()).expect("shake128");
    hasher.update(key).expect("hash update");
    hasher.update(&[dir]).expect("hash update");
    hasher.update(nonce).expect("hash update");
    let mut out = vec![0u8; len];
    hasher.finish_xof(&mut out).expect("shake output");
    out
}

pub(crate) struct DnsCipher {
    key: Option<[u8; 32]>,
    fallback_nonce: u32,
}

impl DnsCipher {
    pub(crate) fn new(key: Option<[u8; 32]>) -> Self {
        Self {
            key,
            fallback_nonce: 0,
        }
    }

    pub(crate) fn enabled(&self) -> bool {
        self.key.is_some()
    }

    pub(crate) fn overhead(&self) -> usize {
        if self.enabled() {
            OVERHEAD
        } else {
            0
        }
    }

    fn nonce(&mut self) -> [u8; NONCE_LEN] {
        let mut nonce = [0u8; NONCE_LEN];
        if rand_bytes(&mut nonce).is_ok() {
            return nonce;
        }
        self.fallback_nonce = self.fallback_nonce.wrapping_add(1);
        nonce.copy_from_slice(&self.fallback_nonce.to_be_bytes());
        nonce
    }

    pub(crate) fn encrypt_query(&mut self, payload: &[u8]) -> Vec<u8> {
        if self.key.is_none() {
            return payload.to_vec();
        }
        let nonce = self.nonce();
        let key = self.key.as_ref().expect("key checked above");
        let mut out = Vec::with_capacity(payload.len() + OVERHEAD);
        out.extend_from_slice(&nonce);
        let keystream = stream(key, DIR_QUERY, &nonce, payload.len() + MAGIC.len());
        out.push(MAGIC[0] ^ keystream[0]);
        out.push(MAGIC[1] ^ keystream[1]);
        for (byte, mask) in payload.iter().zip(&keystream[MAGIC.len()..]) {
            out.push(byte ^ mask);
        }
        out
    }

    pub(crate) fn decrypt_response(&self, payload: &[u8]) -> Option<Vec<u8>> {
        let Some(key) = &self.key else {
            return Some(payload.to_vec());
        };
        if payload.len() < OVERHEAD {
            return None;
        }
        let nonce = &payload[..NONCE_LEN];
        let cipher = &payload[NONCE_LEN..];
        let keystream = stream(key, DIR_ANSWER, nonce, cipher.len());
        let plain: Vec<u8> = cipher
            .iter()
            .zip(keystream.iter())
            .map(|(byte, mask)| byte ^ mask)
            .collect();
        if plain.len() < MAGIC.len() || plain[..MAGIC.len()] != MAGIC {
            return None;
        }
        Some(plain[MAGIC.len()..].to_vec())
    }
}

#[cfg(test)]
mod tests {
    use super::{derive_key, stream, DnsCipher, DIR_ANSWER, MAGIC, NONCE_LEN, OVERHEAD};

    fn answer_frame(key: &[u8; 32], nonce: [u8; 4], payload: &[u8]) -> Vec<u8> {
        let mut frame = nonce.to_vec();
        let keystream = stream(key, DIR_ANSWER, &nonce, payload.len() + MAGIC.len());
        frame.push(MAGIC[0] ^ keystream[0]);
        frame.push(MAGIC[1] ^ keystream[1]);
        for (byte, mask) in payload.iter().zip(&keystream[MAGIC.len()..]) {
            frame.push(byte ^ mask);
        }
        frame
    }

    #[test]
    fn round_trips_answers_and_rejects_foreign_traffic() {
        let key = derive_key(&[7u8; 16]);
        let mut cipher = DnsCipher::new(Some(key));
        let query = cipher.encrypt_query(b"quic-bytes");
        assert_eq!(query.len(), b"quic-bytes".len() + OVERHEAD);
        assert!(cipher.decrypt_response(&query).is_none());

        let frame = answer_frame(&key, [1, 2, 3, 4], b"reply");
        assert_eq!(
            cipher.decrypt_response(&frame).as_deref(),
            Some(&b"reply"[..])
        );
        assert!(DnsCipher::new(Some(derive_key(&[8u8; 16])))
            .decrypt_response(&frame)
            .is_none());
        let mut corrupt = frame.clone();
        corrupt[NONCE_LEN] ^= 0xff;
        assert!(cipher.decrypt_response(&corrupt).is_none());
        assert!(cipher.decrypt_response(&frame[..NONCE_LEN]).is_none());
    }

    #[test]
    fn disabled_cipher_passes_bytes_through() {
        let mut cipher = DnsCipher::new(None);
        assert_eq!(cipher.overhead(), 0);
        assert_eq!(cipher.encrypt_query(b"x"), b"x");
        assert_eq!(cipher.decrypt_response(b"y").as_deref(), Some(&b"y"[..]));
    }
}
