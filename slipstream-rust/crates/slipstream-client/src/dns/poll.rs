use crate::error::ClientError;
use slipstream_core::net::is_transient_udp_error;
use slipstream_dns::{build_qname_variable, encode_query, QueryParams, CLASS_IN, RR_TXT};
use slipstream_ffi::picoquic::{
    picoquic_cnx_t, picoquic_current_time, picoquic_prepare_packet_ex, slipstream_request_poll,
};
use slipstream_ffi::{ClientConfig, ResolverMode};
use std::collections::HashMap;
use tokio::net::UdpSocket as TokioUdpSocket;

use super::cipher::DnsCipher;
use super::id::DnsId;
use super::path::refresh_resolver_path;
use super::resolver::{sockaddr_storage_to_socket_addr, ResolverState};
use super::stats::DpiStats;
use slipstream_core::normalize_dual_stack_addr;

const AUTHORITATIVE_POLL_TIMEOUT_US: u64 = 5_000_000;

pub(crate) fn expire_inflight_polls(inflight_poll_ids: &mut HashMap<u16, u64>, now: u64) {
    if inflight_poll_ids.is_empty() {
        return;
    }
    let expire_before = now.saturating_sub(AUTHORITATIVE_POLL_TIMEOUT_US);
    let mut expired = Vec::new();
    for (id, sent_at) in inflight_poll_ids.iter() {
        if *sent_at <= expire_before {
            expired.push(*id);
        }
    }
    for id in expired {
        inflight_poll_ids.remove(&id);
    }
}

#[allow(clippy::too_many_arguments)]
pub(crate) async fn send_poll_queries(
    cnx: *mut picoquic_cnx_t,
    udp: &TokioUdpSocket,
    config: &ClientConfig<'_>,
    local_addr_storage: &mut libc::sockaddr_storage,
    dns_id: &mut DnsId,
    resolver: &mut ResolverState,
    remaining: &mut usize,
    send_buf: &mut [u8],
    cipher: &mut DnsCipher,
    stats: &mut DpiStats,
) -> Result<(), ClientError> {
    if !refresh_resolver_path(cnx, resolver) {
        return Ok(());
    }
    let mut remaining_count = *remaining;
    *remaining = 0;

    while remaining_count > 0 {
        let current_time = unsafe { picoquic_current_time() };
        unsafe {
            slipstream_request_poll(cnx);
        }

        let mut send_length: libc::size_t = 0;
        let mut addr_to: libc::sockaddr_storage = unsafe { std::mem::zeroed() };
        let mut addr_from: libc::sockaddr_storage = unsafe { std::mem::zeroed() };
        let mut if_index: libc::c_int = 0;
        let ret = unsafe {
            picoquic_prepare_packet_ex(
                cnx,
                resolver.path_id,
                current_time,
                send_buf.as_mut_ptr(),
                send_buf.len(),
                &mut send_length,
                &mut addr_to,
                &mut addr_from,
                &mut if_index,
                std::ptr::null_mut(),
            )
        };
        if ret < 0 {
            return Err(ClientError::new("Failed preparing poll packet"));
        }
        if send_length == 0 || addr_to.ss_family == 0 {
            *remaining = remaining_count;
            break;
        }

        remaining_count -= 1;
        *local_addr_storage = addr_from;
        resolver.local_addr_storage = Some(unsafe { std::ptr::read(local_addr_storage) });
        resolver.debug.send_packets = resolver.debug.send_packets.saturating_add(1);
        resolver.debug.send_bytes = resolver.debug.send_bytes.saturating_add(send_length as u64);
        resolver.debug.polls_sent = resolver.debug.polls_sent.saturating_add(1);

        let poll_id = dns_id.next();
        let encrypted = cipher.encrypt_query(&send_buf[..send_length]);
        let qname = build_qname_variable(&encrypted, config.domain)
            .map_err(|err| ClientError::new(err.to_string()))?;
        stats.record_query(&qname, poll_id, RR_TXT, current_time);
        let params = QueryParams {
            id: poll_id,
            qname: &qname,
            qtype: RR_TXT,
            qclass: CLASS_IN,
            rd: true,
            cd: false,
            qdcount: 1,
            is_query: true,
        };
        let packet = encode_query(&params).map_err(|err| ClientError::new(err.to_string()))?;

        let dest = sockaddr_storage_to_socket_addr(&addr_to)?;
        let dest = normalize_dual_stack_addr(dest);
        if let Err(err) = udp.send_to(&packet, dest).await {
            if is_transient_udp_error(&err) {
                remaining_count = remaining_count.saturating_add(1);
                *remaining = remaining_count;
                break;
            }
            return Err(ClientError::new(err.to_string()));
        }
        if resolver.mode == ResolverMode::Authoritative {
            resolver.inflight_poll_ids.insert(poll_id, current_time);
        }
    }

    Ok(())
}
