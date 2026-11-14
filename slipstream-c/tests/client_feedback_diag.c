/* Test-only observation of real client gates; no state mutation. */
#include "slipstream_runtime.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
static uint64_t prepares, poll_requests, poll_empty, poll_sent, pacing_denied;
static uint64_t cwnd_blocked, inflight_max, cwin_min = UINT64_MAX, stall_max_us;
static uint64_t poll_gated, gated_stall_max_us;
static uint64_t observe(picoquic_cnx_t *c, picoquic_path_t *p, uint64_t now) {
    if (p->cwin < cwin_min) cwin_min = p->cwin;
    if (p->bytes_in_transit > inflight_max) inflight_max = p->bytes_in_transit;
    cwnd_blocked += p->bytes_in_transit >= p->cwin;
    picoquic_packet_context_t *pc = c->is_multipath_enabled ? &p->pkt_ctx :
        &c->pkt_ctx[picoquic_packet_context_application];
    uint64_t gap = p->bytes_in_transit && now >= pc->highest_acknowledged_time ?
        now - pc->highest_acknowledged_time : 0;
    if (gap > stall_max_us) stall_max_us = gap;
    return gap;
}
static void report(void) {
    printf("{\"prepares\":%" PRIu64 ",\"poll_requests\":%" PRIu64
        ",\"poll_empty\":%" PRIu64 ",\"poll_sent\":%" PRIu64
        ",\"pacing_denied\":%" PRIu64 ",\"cwnd_blocked\":%" PRIu64
        ",\"inflight_max\":%" PRIu64 ",\"cwin_min\":%" PRIu64
        ",\"ack_stall_max_us\":%" PRIu64 ",\"poll_gated\":%" PRIu64
        ",\"gated_ack_stall_max_us\":%" PRIu64 "}\n", prepares, poll_requests,
        poll_empty, poll_sent, pacing_denied, cwnd_blocked, inflight_max, cwin_min,
        stall_max_us, poll_gated, gated_stall_max_us);
}
int __real_picoquic_is_sending_authorized_by_pacing(picoquic_cnx_t *, picoquic_path_t *, uint64_t, uint64_t *);
int __wrap_picoquic_is_sending_authorized_by_pacing(picoquic_cnx_t *c,
    picoquic_path_t *p, uint64_t now, uint64_t *next) {
    int ret = __real_picoquic_is_sending_authorized_by_pacing(c, p, now, next);
    if (c->client_mode && c->cnx_state == picoquic_state_ready) {
        uint64_t gap = observe(c, p, now);
        pacing_denied += !ret;
        if (c->is_poll_requested && (!ret || p->bytes_in_transit >= p->cwin)) {
            poll_gated++;
            if (gap > gated_stall_max_us) gated_stall_max_us = gap;
        }
    }
    return ret;
}
int __real_picoquic_prepare_packet_ex(picoquic_cnx_t *, int, uint64_t, uint8_t *, size_t,
    size_t *, struct sockaddr_storage *, struct sockaddr_storage *, int *, size_t *);
int __wrap_picoquic_prepare_packet_ex(picoquic_cnx_t *c, int path, uint64_t now,
    uint8_t *buf, size_t cap, size_t *len, struct sockaddr_storage *peer,
    struct sockaddr_storage *local, int *index, size_t *segment) {
    static int registered;
    if (!registered) { atexit(report); registered = 1; }
    int ready = c->client_mode && c->cnx_state == picoquic_state_ready;
    int poll = ready && c->is_poll_requested;
    if (ready) {
        prepares++; poll_requests += poll;
        for (int i = 0; i < c->nb_paths; i++) {
            picoquic_path_t *p = c->path[i];
            observe(c, p, now);
        }
    }
    int ret = __real_picoquic_prepare_packet_ex(c, path, now, buf, cap,
        len, peer, local, index, segment);
    if (poll) { poll_empty += !*len; poll_sent += *len && !c->is_poll_requested; }
    return ret;
}
