#include "slipstream_runtime.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <picoquic_config.h>
#include <arpa/inet.h>
#include <stdio.h>

/* Existing sender helpers, not exposed by picoquic_internal.h. */
size_t picoquic_pad_to_policy(picoquic_cnx_t *cnx, uint8_t *bytes, size_t length, uint32_t max_length);
void picoquic_insert_hole_in_send_sequence_if_needed(picoquic_cnx_t *cnx, picoquic_path_t *path,
    picoquic_packet_context_t *context, uint64_t now, uint64_t *next_wake);

static void credentials(void) {
    ss_credentials c;
    uint8_t input[81]; memset(input, 'a', sizeof(input));
    for (size_t len = 79; len <= 81; len++) {
        int fds[2]; assert(pipe(fds) == 0);
        assert(write(fds[1], input, len) == (ssize_t)len); close(fds[1]);
        assert((ss_read_credentials(fds[0], &c) == 0) == (len == 80)); close(fds[0]);
    }
    input[30] = 'A';
    int fds[2]; assert(pipe(fds) == 0);
    assert(write(fds[1], input, 80) == 80); close(fds[1]);
    assert(ss_read_credentials(fds[0], &c) != 0); close(fds[0]);
}

static void framing(void) {
    ss_credentials c; memset(&c, 'a', sizeof(c));
    uint8_t input[200] = {5, 1, 2, 1, 32};
    memset(input + 5, 'a', 32); input[37] = 32; memset(input + 38, 'a', 32);
    const uint8_t address[] = {5, 1, 0, 3, 11, 'e','x','a','m','p','l','e','.','c','o','m', 0, 53};
    memcpy(input + 70, address, sizeof(address));
    ss_stream s = {0};
    uint8_t open[300], reply[10]; size_t olen = 0, rlen = 0;
    int result = 0;
    for (size_t i = 0; i < 70 + sizeof(address); i++) {
        s.setup[s.setup_len++] = input[i];
        result = ss_socks_step(&s, &c, open, &olen, reply, &rlen);
        assert(result >= 0);
        if (i + 1 < 70 + sizeof(address)) assert(result == 0);
    }
    assert(result == 1 && olen == 31 && open[0] == 0x24);
    assert(!memcmp(open + 1, c.bytes, 16));
    assert(open[17] == 11 && !memcmp(open + 18, "example.com", 11));
    assert(open[29] == 0 && open[30] == 53);

    s = (ss_stream){0}; s.stage = 1;
    s.setup[0] = 1; s.setup[1] = 32; memset(s.setup + 2, 'a', 32);
    s.setup[34] = 32; memset(s.setup + 35, 'b', 32); s.setup_len = 67;
    assert(ss_socks_step(&s, &c, open, &olen, reply, &rlen) == -1);
    assert(rlen == 2 && reply[1] == 1);

    s = (ss_stream){0}; s.stage = 2;
    const char magic[] = "sp.v2.udp-over-tcp.arpa";
    uint8_t request[] = {5, 1, 0, 3, sizeof(magic)-1};
    memcpy(s.setup, request, 5); memcpy(s.setup + 5, magic, sizeof(magic)-1);
    s.setup_len = 5 + sizeof(magic)-1 + 2;
    assert(ss_socks_step(&s, &c, open, &olen, reply, &rlen) == 0);
    assert(s.stage == 3 && rlen == 10 && reply[1] == 0);
    uint8_t uot[] = {0, 1, 127, 0, 0, 1, 0, 53, 0, 0, 1, 2, 3};
    for (size_t i = 0; i < 7; i++) {
        s.setup[s.setup_len++] = uot[i];
        assert(ss_socks_step(&s, &c, open, &olen, reply, &rlen) == 0);
        assert(!olen && s.stage == 3);
    }
    memcpy(s.setup + s.setup_len, uot + 7, sizeof(uot) - 7); s.setup_len = sizeof(uot);
    assert(ss_socks_step(&s, &c, open, &olen, reply, &rlen) == 1);
    assert(open[0] == 0x28 && olen == 23 && s.setup_len == 5);
    assert(!memcmp(s.setup, uot + 8, 5));
}

static void scheduler(void) {
    assert(ss_scheduler_phase(0, 0, 0, true, true) == SS_EMPTY);
    assert(ss_scheduler_phase(1, 10, 400009, false, false) == SS_BUSY);
    assert(ss_scheduler_phase(1, 10, 400010, false, false) == SS_WARM);
    assert(ss_scheduler_phase(1, 10, 1000010, false, false) == SS_QUIET);
    assert(ss_scheduler_phase(1, 0, 2000000, false, true) == SS_BUSY);
    assert(ss_poll_budget(SS_EMPTY, 32, true, false, false) == 0);
    assert(ss_poll_budget(SS_BUSY, 32, false, true, false) == 0);
    assert(ss_poll_budget(SS_BUSY, 32, false, true, true) == 32);
    assert(ss_poll_budget(SS_WARM, 32, true, false, false) == 1);
    assert(ss_poll_budget(SS_QUIET, 32, false, false, false) == 0);
    assert(ss_sparse_poll_deadline(SS_WARM, 1000000, false) == 1050000);
    assert(ss_sparse_poll_deadline(SS_WARM, 1000000, true) == 1400000);
    assert(ss_sparse_poll_deadline(SS_QUIET, 1000000, true) == 3000000);
    assert(ss_sparse_poll_deadline(SS_QUIET, UINT64_MAX - 1, true) == UINT64_MAX);
    assert(ss_scheduler_timeout(SS_WARM, 5000000, 900000, 0, 1200000, false) == 100000);
    assert(ss_scheduler_timeout(SS_QUIET, 5000000, 1000000, 0, 1050000, false) == 50000);
    assert(ss_scheduler_timeout(SS_QUIET, 5000000, 1000000, 0, 1000000, false) == 1);
    assert(ss_scheduler_timeout(SS_EMPTY, 5000000, 1000000, 0, 0, false) == 5000000);
    assert(ss_scheduler_timeout(SS_BUSY, 5000000, 1000000, 0, 0, false) == 50000);
    assert(ss_scheduler_timeout(SS_EMPTY, 5000000, 1000000, 0, 0, true) == 50000);
}

static void receive_credit(void) {
    uint64_t window = SS_RECEIVE_WINDOW;
    assert(ss_receive_credit_target(window / 8 - 1, 0, window) == 0);
    assert(ss_receive_credit_target(window / 8, 0, window) == window + window / 8);
    assert(ss_receive_credit_target(window, window, window) == 0);
    assert(ss_receive_credit_target(window, window * 2, window) == 0);
    uint64_t received = 3 * window;
    size_t pending = (size_t)window / 2;
    uint64_t target = ss_receive_credit_target(received, pending, received);
    assert(target == received + window - pending);
    assert(target - (received - pending) == window);
}

static picoquic_packet_context_t *poll_context(picoquic_cnx_t *cnx, picoquic_path_t *path) {
    return cnx->is_multipath_enabled ? &path->pkt_ctx :
        &cnx->pkt_ctx[picoquic_packet_context_application];
}

static picoquic_packet_t *queue_padded_poll(picoquic_cnx_t *cnx, picoquic_path_t *path, uint64_t now) {
    picoquic_packet_context_t *context = poll_context(cnx, path);
    picoquic_packet_t *packet = picoquic_create_packet(cnx->quic); assert(packet);
    packet->ptype = picoquic_packet_1rtt_protected;
    packet->pc = picoquic_packet_context_application;
    size_t header_length = picoquic_predict_packet_header_length(cnx, packet->ptype, context);
    size_t pn_offset = 0, pn_length = 0;
    packet->offset = picoquic_create_packet_header(cnx, packet->ptype, context->send_sequence,
        path, header_length, packet->bytes, &pn_offset, &pn_length);
    assert(packet->offset == header_length && pn_length > 0);
    packet->sequence_number = context->send_sequence++;
    packet->send_path = path; packet->send_time = now;
    packet->checksum_overhead = 16;
    packet->bytes[packet->offset] = picoquic_frame_type_poll;
    /* Use the production padding policy and real retransmit/transit accounting. */
    packet->length = picoquic_pad_to_policy(cnx, packet->bytes, packet->offset + 1,
        (uint32_t)(path->send_mtu - packet->checksum_overhead));
    assert(packet->length == 39 && packet->length + packet->checksum_overhead == 55);
    picoquic_queue_for_retransmit(cnx, path, packet, packet->length + packet->checksum_overhead, now);
    return packet;
}

static void queue_poll_hole(picoquic_cnx_t *cnx, picoquic_path_t *path, uint64_t now) {
    picoquic_packet_context_t *context = poll_context(cnx, path);
    uint64_t next_wake = UINT64_MAX, transit = path->bytes_in_transit;
    cnx->quic->sequence_hole_pseudo_period = 1;
    context->next_sequence_hole = context->send_sequence;
    picoquic_insert_hole_in_send_sequence_if_needed(cnx, path, context, now, &next_wake);
    assert(context->pending_last->is_ack_trap && context->pending_last->send_path == NULL);
    assert(next_wake == now && path->bytes_in_transit == transit);
}

static void clear_poll_context(picoquic_cnx_t *cnx, picoquic_packet_context_t *context) {
    while (context->pending_first)
        picoquic_dequeue_retransmit_packet(cnx, context, context->pending_first, 1, 0);
}

static void poll_count_policy(bool multipath) {
    uint64_t now = 1000000;
    picoquic_quic_config_t config; picoquic_config_init(&config);
    config.nb_connections = 1; config.alpn = "test"; config.enable_sslkeylog = 0;
    picoquic_quic_t *quic = picoquic_create_and_configure(&config, NULL, NULL, now, &now);
    assert(quic);
    struct sockaddr_in address = {0}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(53);
    ss_stream stream = {.id = UINT64_MAX};
    ss_conn c = {.client = true, .ready = true, .streams = &stream, .count = 1,
        .last_activity = now, .next_poll = now + 1, .phase = SS_BUSY};
    c.cnx = picoquic_create_cnx(quic, picoquic_null_connection_id, picoquic_null_connection_id,
        (struct sockaddr *)&address, now, 0, "test", "test", 1);
    assert(c.cnx);
    c.cnx->cnx_state = picoquic_state_ready; c.cnx->is_multipath_enabled = multipath;
    assert(c.cnx->padding_minsize == 39 && c.cnx->padding_multiple == 128);
    picoquic_path_t *path = c.cnx->path[0]; path->challenge_verified = 1;
    path->p_remote_cnxid->cnx_id.id_len = 8;
    memset(path->p_remote_cnxid->cnx_id.id, 0xa5, 8);
    path->send_mtu = 128; path->smoothed_rtt = 1000000; path->cwin = 8192;
    picoquic_update_pacing_rate(c.cnx, path, 256, 128);
    c.last_pacing_rate[0] = path->pacing.rate;
    assert(path->pacing.rate == 256);
    picoquic_packet_context_t *context = poll_context(c.cnx, path);
    assert(ss_conn_poll_budget(&c, 0, now) == 2);
    picoquic_packet_t *first = queue_padded_poll(c.cnx, path, now);
    assert(path->bytes_in_transit == 55 && ss_conn_poll_budget(&c, 0, now) == 1);
    queue_padded_poll(c.cnx, path, now);
    assert(path->bytes_in_transit == 110 && (path->bytes_in_transit + 127) / 128 == 1);
    /* Policy comparison: the unchanged byte-window producer returns 1 here.
     * The experimental outstanding-packet cap deliberately chooses 0. */
    size_t budget = ss_conn_poll_budget(&c, 0, now);
    if (budget != 0) fprintf(stderr, "poll-count policy (%s): wanted=2, pending=2, transit=110, "
        "MTU=128, byte occupancy=1, observed budget=%zu, experimental expectation=0\n",
        multipath ? "multipath" : "legacy", budget);
    assert(budget == 0);

    /* A full count must not suppress due sparse polls. */
    now += 400000;
    assert(ss_conn_poll_budget(&c, 0, now) == 1 && c.phase == SS_WARM);
    c.next_poll = now + 1; assert(ss_conn_poll_budget(&c, 0, now) == 0);
    now += 600000; c.next_poll = now;
    assert(ss_conn_poll_budget(&c, 0, now) == 1 && c.phase == SS_QUIET);
    c.next_poll = now + 1; assert(ss_conn_poll_budget(&c, 0, now) == 0);
    c.count = 0; assert(ss_conn_poll_budget(&c, 0, now) == 0);
    c.count = 1; c.last_activity = now;
    picoquic_dequeue_retransmit_packet(c.cnx, context, first, 1, 0);
    assert(path->bytes_in_transit == 55 && ss_conn_poll_budget(&c, 0, now) == 1);
    queue_poll_hole(c.cnx, path, now);
    assert(ss_conn_poll_budget(&c, 0, now) == 1);
    queue_padded_poll(c.cnx, path, now);
    assert(path->bytes_in_transit == 110 && ss_conn_poll_budget(&c, 0, now) == 0);
    clear_poll_context(c.cnx, context);

    uint8_t cid[8], reset_secret[16] = {0}; memset(cid, 0xa6, sizeof(cid));
    assert(picoquic_stash_remote_cnxid(c.cnx, 0, multipath ? 1 : 0,
        1, sizeof(cid), cid, reset_secret, NULL) == 0);
    assert(picoquic_create_path(c.cnx, now, NULL, (struct sockaddr *)&address, UINT64_MAX) == 1);
    picoquic_path_t *other = c.cnx->path[1]; other->send_mtu = 128;
    assert(picoquic_assign_peer_cnxid_to_path(c.cnx, 1) == 0);
    queue_padded_poll(c.cnx, other, now);
    assert(path->bytes_in_transit == 0 && ss_conn_poll_budget(&c, 0, now) == 2);
    first = queue_padded_poll(c.cnx, path, now);
    assert(ss_conn_poll_budget(&c, 0, now) == 1);
    picoquic_dequeue_retransmit_packet(c.cnx, context, first, 1, 0);

    if (multipath) {
        picoquic_update_pacing_rate(c.cnx, path, 8192, 128);
        c.last_pacing_rate[0] = path->pacing.rate;
        /* Exactly 64 nodes, 32 transmitted POLLs and 32 real unsent traps. */
        for (int i = 0; i < 32; i++) {
            queue_padded_poll(c.cnx, path, now); queue_poll_hole(c.cnx, path, now);
        }
        assert(path->bytes_in_transit == 32 * 55 && ss_conn_poll_budget(&c, 0, now) == 16);
        first = queue_padded_poll(c.cnx, path, now);
    } else {
        /* Exactly 64 foreign-path packets are fully resolved, so none count. */
        for (int i = 1; i < 64; i++) queue_padded_poll(c.cnx, other, now);
        assert(path->bytes_in_transit == 0 && ss_conn_poll_budget(&c, 0, now) == 2);
        first = queue_padded_poll(c.cnx, other, now);
    }
    /* A 65th node leaves an unresolved tail: saturate wanted without scanning it. */
    assert(ss_conn_poll_budget(&c, 0, now) == 0);
    picoquic_dequeue_retransmit_packet(c.cnx, multipath ? context : poll_context(c.cnx, other), first, 1, 0);
    assert(ss_conn_poll_budget(&c, 0, now) == (multipath ? 16 : 2));
    clear_poll_context(c.cnx, context);
    clear_poll_context(c.cnx, poll_context(c.cnx, other));
    assert(path->bytes_in_transit == 0 && other->bytes_in_transit == 0);
    picoquic_free(quic);
}

static void partial_output(void) {
    uint64_t now = picoquic_current_time();
    picoquic_quic_config_t config; picoquic_config_init(&config);
    config.nb_connections = 1; config.alpn = "test"; config.enable_sslkeylog = 0;
    picoquic_quic_t *quic = picoquic_create_and_configure(&config, NULL, NULL, now, NULL);
    assert(quic);
    struct sockaddr_in addr = {0}; addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = htons(53);
    ss_conn c = {0}; c.client = true;
    c.cnx = picoquic_create_cnx(quic, picoquic_null_connection_id, picoquic_null_connection_id,
        (struct sockaddr *)&addr, now, 0, "test", "test", 1);
    assert(c.cnx);
    int fds[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    int size = 1024;
    assert(setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == 0);
    assert(ss_nonblock(fds[0]) == 0 && ss_nonblock(fds[1]) == 0);
    ss_stream *s = ss_stream_create(&c, fds[0], 0); assert(s);
    assert(picoquic_set_app_stream_ctx(c.cnx, 0, s) == 0);
    uint8_t *data = malloc(SS_RECEIVE_WINDOW), *received = malloc(SS_RECEIVE_WINDOW);
    assert(data && received);
    for (size_t i = 0; i < SS_RECEIVE_WINDOW; i++) data[i] = (uint8_t)i;
    assert(ss_stream_event(&c, 0, data, SS_RECEIVE_WINDOW, picoquic_callback_stream_fin, s) == 0);
    assert(s->out_len > s->out_off && !s->write_closed);
    s->local_fin = true;
    size_t offset = 0;
    while (c.count) {
        ssize_t n = recv(fds[1], received + offset, SS_RECEIVE_WINDOW - offset, 0);
        if (n > 0) offset += (size_t)n;
        else assert(n < 0 && errno == EAGAIN);
        ss_conn_service(&c, now);
    }
    while (offset < SS_RECEIVE_WINDOW) {
        ssize_t n = recv(fds[1], received + offset, SS_RECEIVE_WINDOW - offset, 0);
        assert(n > 0); offset += (size_t)n;
    }
    uint8_t extra;
    assert(recv(fds[1], &extra, 1, 0) == 0);
    assert(!memcmp(data, received, SS_RECEIVE_WINDOW));
    close(fds[1]); free(data); free(received); picoquic_free(quic);
}

int main(void) {
    credentials(); framing(); scheduler(); receive_credit();
    poll_count_policy(true); poll_count_policy(false); partial_output(); return 0;
}
