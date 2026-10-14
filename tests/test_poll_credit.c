#include "slipstream_runtime.h"
#include <picoquic_config.h>
#include <tls_api.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Existing internal sender helpers; no transport API is added. */
void *picoquic_setup_test_aead_context(int, const uint8_t *, const char *);
void *picoquic_pn_enc_create_for_test(const uint8_t *, const char *);
int picoquic_prepare_packet_ready(picoquic_cnx_t *, picoquic_path_t *,
    picoquic_packet_t *, uint64_t, uint8_t *, size_t, size_t *, uint64_t *, int *);

static int callback(picoquic_cnx_t *cnx, uint64_t id, uint8_t *bytes, size_t length,
    picoquic_call_back_event_t event, void *ctx, void *stream_ctx) {
    (void)cnx;
    return ss_stream_event(ctx, id, bytes, length, event, stream_ctx);
}

static int setup(ss_conn *c, uint64_t *now, bool client, int multipath) {
    picoquic_quic_config_t config; picoquic_config_init(&config);
    config.nb_connections = 1; config.alpn = "test"; config.enable_sslkeylog = 0;
    config.do_preemptive_repeat = 1; config.multipath_option = multipath;
    picoquic_quic_t *quic = picoquic_create_and_configure(&config, NULL, NULL, *now, now);
    assert(quic);
    struct sockaddr_in address = {0}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(53);
    memset(c, 0, sizeof(*c)); c->client = client; c->ready = true; c->last_activity = *now;
    c->cnx = picoquic_create_cnx(quic, picoquic_null_connection_id,
        picoquic_null_connection_id, (struct sockaddr *)&address, *now, 0, "test", "test", 1);
    picoquic_cnx_t *cnx = c->cnx; assert(cnx);
    cnx->cnx_state = picoquic_state_ready; cnx->is_multipath_enabled = multipath != 0;
    cnx->remote_parameters = cnx->local_parameters;
    cnx->maxdata_remote = cnx->remote_parameters.initial_max_data;
    cnx->max_stream_id_bidir_remote = STREAM_ID_FROM_RANK(
        cnx->remote_parameters.initial_max_stream_id_bidir, 1, 0);
    cnx->latest_progress_time = cnx->latest_receive_time = *now;
    cnx->is_ack_frequency_updated = 0; cnx->keep_alive_interval = 0; cnx->no_ack_delay = 1;
    cnx->local_parameters.active_connection_id_limit = 1;
    cnx->remote_parameters.active_connection_id_limit = 1;
    cnx->local_parameters.initial_max_path_id = 0;
    static const uint8_t secret[32] = {1, 2, 3, 4};
    cnx->crypto_context[3].aead_encrypt = picoquic_setup_test_aead_context(1,
        secret, PICOQUIC_LABEL_QUIC_V1_KEY_BASE);
    cnx->crypto_context[3].pn_enc = picoquic_pn_enc_create_for_test(secret,
        PICOQUIC_LABEL_QUIC_V1_KEY_BASE);
    assert(cnx->crypto_context[3].aead_encrypt && cnx->crypto_context[3].pn_enc);
    picoquic_path_t *path = cnx->path[0]; assert(path->p_remote_cnxid);
    path->p_remote_cnxid->cnx_id.id_len = 8;
    memset(path->p_remote_cnxid->cnx_id.id, 0xa5, 8);
    path->challenge_verified = 1; path->send_mtu = 143;
    path->latest_sent_time = *now; path->rtt_min = path->smoothed_rtt = 140000;
    path->rtt_is_initialized = 1; path->retransmit_timer = 1000000;
    cnx->quic->cwin_max = UINT64_MAX;
    picoquic_set_congestion_algorithm(cnx, picoquic_dcubic_algorithm);
    path->cwin = 5211; path->pacing.rate = 1000000;
    int sockets[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(ss_nonblock(sockets[0]) == 0 && ss_nonblock(sockets[1]) == 0);
    picoquic_set_callback(cnx, callback, c);
    ss_stream *stream = ss_stream_create(c, sockets[0], 0); assert(stream);
    stream->connected = true;
    assert(picoquic_set_app_stream_ctx(cnx, 0, stream) == 0);
    picoquic_find_stream(cnx, 0)->maxdata_remote = SS_BUFFER_LIMIT;
    return sockets[1];
}

static void cleanup(ss_conn *c, int peer) {
    picoquic_quic_t *quic = c->cnx->quic;
    ss_conn_clear(c); close(peer); picoquic_free(quic);
}

static picoquic_packet_t *prepare(ss_conn *c, uint64_t now, bool poll) {
    picoquic_cnx_t *cnx = c->cnx;
    cnx->path[0]->pacing.bucket_nanosec = 1000000000;
    cnx->is_poll_requested = poll;
    uint8_t wire[143]; size_t sent = 0; uint64_t wake = UINT64_MAX; int initial = 0;
    picoquic_packet_t *packet = picoquic_create_packet(cnx->quic); assert(packet);
    assert(picoquic_prepare_packet_ready(cnx, cnx->path[0], packet, now,
        wire, sizeof(wire), &sent, &wake, &initial) == 0);
    cnx->is_poll_requested = 0;
    if (!sent) { picoquic_recycle_packet(cnx->quic, packet); return NULL; }
    assert(sent <= sizeof(wire) && packet->is_queued_for_retransmit);
    return packet;
}

static void interleave(bool client, int multipath, bool revisit_copy) {
    uint64_t now = 1000000; ss_conn c; int peer = setup(&c, &now, client, multipath);
    picoquic_cnx_t *cnx = c.cnx; picoquic_path_t *path = cnx->path[0];
    if (revisit_copy) path->cwin = 686;
    const uint8_t old[45] = {1, 2, 3};
    assert(picoquic_add_to_stream(cnx, 0, old, sizeof(old), 0) == 0);
    picoquic_packet_t *original = prepare(&c, now, false); assert(original);
    for (unsigned pulls = 0; ss_conn_poll_budget(&c, 0, now); pulls++) {
        assert(pulls < 200);
        picoquic_packet_t *packet = prepare(&c, now, true);
        if (!packet) break;
        assert(!packet->is_preemptive_repeat);
    }
    assert(path->cwin > path->bytes_in_transit);
    assert(path->cwin - path->bytes_in_transit >= 2 * path->send_mtu);
    picoquic_packet_t *copy = prepare(&c, now + 35001, false);
    assert(copy && copy->is_preemptive_repeat && original->was_preemptively_repeated);
    uint64_t copies = cnx->nb_preemptive_repeat;
    picoquic_packet_t *nested = NULL;
    if (revisit_copy) nested = prepare(&c, now + 70002, false);
    picoquic_stream_head_t *stream = picoquic_find_stream(cnx, 0);
    uint64_t before = stream->sent_offset; const uint8_t fresh[132] = {4, 5, 6};
    assert(picoquic_add_to_stream(cnx, 0, fresh, sizeof(fresh), 0) == 0);
    for (unsigned tries = 0; tries < 4 && stream->send_queue; tries++) {
        picoquic_packet_t *packet = prepare(&c, now + (revisit_copy ? 80000 : 40000) + tries, false);
        assert(packet && !packet->is_preemptive_repeat);
    }
    assert(stream->sent_offset - before == sizeof(fresh) && !stream->send_queue);
    if (revisit_copy) {
        assert(!nested && cnx->nb_preemptive_repeat == copies);
        fprintf(stderr, "POLL_CREDIT bounded early copy client=%d mp=%d fresh=132\n", client, multipath);
    }
    cleanup(&c, peer);
}

static void edges(bool client) {
    uint64_t now = 1000000; ss_conn c; int peer = setup(&c, &now, client, 1);
    picoquic_path_t *path = c.cnx->path[0];
    const struct { uint64_t cwin, flight; size_t budget; } cases[] = {
        {0, 0, 0}, {142, 0, 0}, {143, 0, 1}, {285, 0, 1}, {286, 0, 1},
        {286, 55, 0}, {286, 286, 0}, {286, 287, 0}, {429, 0, 1},
        {1000, 715, 0}, {1000, 714, 0}, {1000, 571, 1}, {1000, 0, 4},
        {UINT64_MAX, 0, 16}, {UINT64_MAX, UINT64_MAX, 0}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        path->cwin = cases[i].cwin; path->bytes_in_transit = cases[i].flight;
        assert(ss_conn_poll_budget(&c, 0, now) == cases[i].budget);
        assert(path->cwin == cases[i].cwin && path->bytes_in_transit == cases[i].flight);
    }
    path->cwin = 1000; path->bytes_in_transit = 0;
    uint64_t later = now + 500000; c.phase = SS_WARM; c.next_poll = later + 1;
    assert(ss_conn_poll_budget(&c, 0, later) == 0);
    assert(ss_conn_poll_budget(&c, 0, later + 1) == 1);
    later = now + 2000000; c.phase = SS_QUIET; c.next_poll = later;
    assert(ss_conn_poll_budget(&c, 0, later) == 1);
    path->bytes_in_transit = path->cwin;
    assert(ss_conn_poll_budget(&c, 0, later) == 0);
    path->bytes_in_transit = 1; path->cwin = 4; path->send_mtu = 0;
    c.last_activity = later;
    assert(ss_conn_poll_budget(&c, 0, later) == 1);
    path->send_mtu = 143; path->cwin = 1000; path->bytes_in_transit = 0;
    const uint8_t payload[3] = {1, 2, 3};
    assert(picoquic_add_to_stream(c.cnx, 0, payload, sizeof(payload), 0) == 0);
    assert(ss_conn_poll_budget(&c, 0, later) == 0);
    picoquic_stream_head_t *stream = picoquic_find_stream(c.cnx, 0);
    stream->maxdata_remote = stream->sent_offset;
    assert(ss_conn_poll_budget(&c, 0, later) == 4);
    cleanup(&c, peer);
}

static void small_window_response(int multipath) {
    uint64_t now = 1000000; ss_conn sender, receiver;
    int send_peer = setup(&sender, &now, true, multipath);
    int recv_peer = setup(&receiver, &now, false, multipath);
    picoquic_cnx_t *sc = sender.cnx, *rc = receiver.cnx;
    picoquic_path_t *path = sc->path[0]; path->cwin = 2 * path->send_mtu;
    picoquic_packet_context_t *context = multipath ? &path->pkt_ctx :
        &sc->pkt_ctx[picoquic_packet_context_application];
    picoquic_ack_context_t *received = multipath ? &rc->path[0]->ack_ctx :
        &rc->ack_ctx[picoquic_packet_context_application];
    assert(!picoquic_record_pn_received(rc, picoquic_packet_context_application, NULL, 0, now));
    received->act[0].highest_ack_sent = 0;
    received->act[0].highest_ack_sent_time = now;
    context->send_sequence = 1; context->highest_acknowledged = 0;
    assert(ss_conn_poll_budget(&sender, 0, now) == 1);
    picoquic_packet_t *packet = prepare(&sender, now, true); assert(packet);
    bool poll = false;
    for (size_t i = packet->offset; i < packet->length;) {
        size_t used = 0; int pure = 0; uint8_t type = packet->bytes[i];
        assert(!picoquic_skip_frame(packet->bytes + i, packet->length - i, &used, &pure));
        assert(used && pure && (type == picoquic_frame_type_poll || type == picoquic_frame_type_padding));
        poll |= type == picoquic_frame_type_poll; i += used;
    }
    assert(poll && path->bytes_in_transit > 0);
    assert(ss_conn_poll_budget(&sender, 0, now) == 0);
    struct sockaddr_in address = {0}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(53);
    now += 10;
    assert(!picoquic_decode_frames(rc, rc->path[0], packet->bytes + packet->offset,
        packet->length - packet->offset, NULL, picoquic_epoch_1rtt,
        (struct sockaddr *)&address, (struct sockaddr *)&address, packet->sequence_number, 0, now));
    assert(!picoquic_record_pn_received(rc, picoquic_packet_context_application,
        NULL, packet->sequence_number, now));
    uint64_t wake = UINT64_MAX;
    assert(picoquic_is_ack_needed(rc, now, &wake, picoquic_packet_context_application, 0));
    uint8_t ack[256]; int more = 0;
    uint8_t *end = picoquic_format_ack_frame(rc, ack, ack + sizeof(ack), &more,
        now, picoquic_packet_context_application, 0);
    assert(end > ack && !more);
    assert(!picoquic_decode_frames(sc, path, ack, end - ack, NULL, picoquic_epoch_1rtt,
        (struct sockaddr *)&address, (struct sockaddr *)&address, 1, 0, now + 10000));
    assert(!context->pending_first && !path->bytes_in_transit);
    assert(ss_conn_poll_budget(&sender, 0, now + 10000) > 0);
    fprintf(stderr, "POLL_CREDIT minimum window mp=%d ACK releases pending pull\n", multipath);
    cleanup(&sender, send_peer); cleanup(&receiver, recv_peer);
}

int main(void) {
    for (int client = 0; client < 2; client++) {
        interleave(client != 0, 0, false); interleave(client != 0, 1, false);
        interleave(client != 0, 0, true); interleave(client != 0, 1, true);
        edges(client != 0);
    }
    small_window_response(0); small_window_response(1);
    return 0;
}
