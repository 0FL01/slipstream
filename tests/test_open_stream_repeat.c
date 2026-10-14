#include <picoquic_config.h>
#include <picoquic_internal.h>
#include <tls_api.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Existing internal sender entry point; no production API is added. */
int picoquic_preemptive_retransmit_in_context(picoquic_cnx_t *cnx,
    picoquic_packet_context_t *pkt_ctx, uint64_t rtt, uint64_t current_time,
    uint64_t *next_wake_time, uint8_t *new_bytes, size_t capacity,
    size_t *length, int *has_data, int *more_data, int test_only);
void *picoquic_setup_test_aead_context(int, const uint8_t *, const char *);
void *picoquic_pn_enc_create_for_test(const uint8_t *, const char *);
int picoquic_prepare_packet_ready(picoquic_cnx_t *, picoquic_path_t *,
    picoquic_packet_t *, uint64_t, uint8_t *, size_t, size_t *, uint64_t *, int *);

static const picoquic_packet_context_enum application = picoquic_packet_context_application;
static const uint8_t payload[] = {'a', 'b', 'c', 0, 1, 2, 0xff, 'd', 'e', 'f', 'g'};

static picoquic_cnx_t *connection_mode(uint64_t *now, int client, int multipath) {
    picoquic_quic_config_t config;
    picoquic_config_init(&config);
    config.nb_connections = 1; config.alpn = "test"; config.enable_sslkeylog = 0;
    config.do_preemptive_repeat = 1;
    config.multipath_option = multipath;
    picoquic_quic_t *quic = picoquic_create_and_configure(&config, NULL, NULL, *now, now);
    assert(quic);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(53);
    picoquic_cnx_t *cnx = picoquic_create_cnx(quic, picoquic_null_connection_id,
        picoquic_null_connection_id, (struct sockaddr *)&address, *now, 0, "test", "test", client);
    assert(cnx);
    cnx->cnx_state = picoquic_state_ready;
    cnx->is_multipath_enabled = multipath != 0;
    /* Symmetric default transport parameters stand in for completed negotiation. */
    cnx->remote_parameters = cnx->local_parameters;
    cnx->maxdata_remote = cnx->remote_parameters.initial_max_data;
    cnx->max_stream_id_bidir_remote = STREAM_ID_FROM_RANK(
        cnx->remote_parameters.initial_max_stream_id_bidir, client, 0);
    cnx->latest_progress_time = cnx->latest_receive_time = *now;
    cnx->path[0]->smoothed_rtt = 240000;
    cnx->path[0]->retransmit_timer = 720000;
    cnx->path[0]->p_remote_cnxid->cnx_id.id_len = 8;
    memset(cnx->path[0]->p_remote_cnxid->cnx_id.id, 0xa5, 8);
    return cnx;
}

static picoquic_cnx_t *connection(uint64_t *now, int client) {
    return connection_mode(now, client, 0);
}

static size_t stream_frame(picoquic_cnx_t *cnx, uint8_t *frame, size_t capacity) {
    int more = 0, pure_ack = 1, active = 0, ret = 0;
    uint8_t *end = picoquic_format_stream_frame(cnx, picoquic_find_stream(cnx, 0),
        frame, frame + capacity, &more, &pure_ack, &active, &ret);
    assert(ret == 0 && end > frame && end <= frame + capacity && !pure_ack);
    return (size_t)(end - frame);
}

static picoquic_packet_t *pending(picoquic_cnx_t *cnx, const uint8_t *frame,
    size_t length, uint64_t now) {
    picoquic_packet_context_t *ctx = &cnx->pkt_ctx[application];
    picoquic_path_t *path = cnx->path[0];
    picoquic_packet_t *packet = picoquic_create_packet(cnx->quic);
    assert(packet);
    packet->ptype = picoquic_packet_1rtt_protected; packet->pc = application;
    size_t header = picoquic_predict_packet_header_length(cnx, packet->ptype, ctx);
    size_t pn_offset = 0, pn_length = 0;
    packet->offset = picoquic_create_packet_header(cnx, packet->ptype,
        ctx->send_sequence, path, header, packet->bytes, &pn_offset, &pn_length);
    assert(packet->offset == header && pn_length > 0);
    packet->sequence_number = ctx->send_sequence++;
    packet->send_path = path; packet->send_time = now;
    packet->delivered_prior = path->delivered_last;
    packet->delivered_time_prior = path->delivered_time_last;
    packet->delivered_sent_prior = path->delivered_sent_last;
    packet->lost_prior = path->total_bytes_lost;
    packet->inflight_prior = path->bytes_in_transit;
    memcpy(packet->bytes + packet->offset, frame, length);
    packet->length = packet->offset + length; packet->checksum_overhead = 16;
    picoquic_queue_for_retransmit(cnx, path, packet, packet->length + 16, now);
    assert(ctx->pending_last == packet);
    return packet;
}

static int repeat(picoquic_cnx_t *cnx, uint64_t now, uint8_t *bytes,
    size_t capacity, size_t *length, uint64_t *wake) {
    int has_data = 0, more = 0;
    *length = 0; *wake = UINT64_MAX;
    assert(picoquic_preemptive_retransmit_in_context(cnx, &cnx->pkt_ctx[application],
        cnx->path[0]->smoothed_rtt, now, wake, bytes, capacity, length,
        &has_data, &more, 0) == 0);
    assert(more == 0);
    return has_data;
}

struct received { uint8_t bytes[sizeof(payload)]; size_t length; unsigned fins; };

static int receive_bytes(picoquic_cnx_t *cnx, uint64_t stream_id, uint8_t *bytes,
    size_t length, picoquic_call_back_event_t event, void *context, void *stream_context) {
    (void)cnx; (void)stream_context;
    struct received *received = context;
    if (event == picoquic_callback_stream_data || event == picoquic_callback_stream_fin) {
        assert(stream_id == 0 && received->length + length <= sizeof(received->bytes));
        memcpy(received->bytes + received->length, bytes, length);
        received->length += length;
        received->fins += event == picoquic_callback_stream_fin;
    }
    return 0;
}

static void decode(picoquic_cnx_t *cnx, const uint8_t *bytes, size_t length, uint64_t now) {
    picoquic_path_t *path = cnx->path[0];
    assert(picoquic_decode_frames(cnx, path, bytes, length, NULL, picoquic_epoch_1rtt,
        (struct sockaddr *)&path->peer_addr, (struct sockaddr *)&path->local_addr,
        0, 0, now) == 0);
}

/* The final expectation is deliberately identical on both builds: the baseline
 * FIN-only policy fails the open-data assertion, rather than being a bug gate. */
static void early_copy(int fin, int empty) {
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = connection(&now, 1);
    uint8_t prefix[64], frame[64], copy[64];
    assert(picoquic_add_to_stream(cnx, 0, payload, 3, 0) == 0);
    size_t prefix_length = stream_frame(cnx, prefix, sizeof(prefix));
    size_t data_length = empty ? 0 : sizeof(payload) - 3;
    assert(picoquic_add_to_stream(cnx, 0, payload + 3, data_length, fin) == 0);
    size_t frame_length = stream_frame(cnx, frame, sizeof(frame));
    picoquic_stream_head_t *stream = picoquic_find_stream(cnx, 0);
    assert(stream && stream->fin_sent == (unsigned)fin && !stream->reset_sent);
    uint64_t id = UINT64_MAX, offset = UINT64_MAX;
    size_t parsed_length = 0, consumed = 0; int parsed_fin = -1;
    assert(picoquic_parse_stream_header(frame, frame_length, &id, &offset,
        &parsed_length, &parsed_fin, &consumed) == 0);
    assert(id == 0 && offset == 3 && parsed_length == data_length && parsed_fin == fin);
    assert(consumed + data_length == frame_length);
    assert(memcmp(frame + consumed, payload + 3, data_length) == 0);
    picoquic_packet_t *packet = pending(cnx, frame, frame_length, now);
    picoquic_packet_context_t *ctx = &cnx->pkt_ctx[application];
    uint64_t inflight = cnx->path[0]->bytes_in_transit;
    uint64_t rtt = cnx->path[0]->smoothed_rtt;
    uint64_t early_delay = rtt > 8 * PICOQUIC_ACK_DELAY_MAX ? rtt / 8 : PICOQUIC_ACK_DELAY_MAX;
    assert(early_delay <= rtt / 2);
    uint64_t early_time = now + early_delay, wake = 0; size_t length = 0;
    assert(!repeat(cnx, early_time - 1, copy, sizeof(copy), &length, &wake));
    assert(length == 0 && wake == early_time && !packet->was_preemptively_repeated);

    /* Use the real loss-recovery entry point before the early copy. Neither
     * loss nor PTO is due, and the ordinary wake is the real path timer. */
    picoquic_packet_t *ordinary = picoquic_create_packet(cnx->quic);
    assert(ordinary);
    size_t header = 0; wake = UINT64_MAX;
    assert(picoquic_retransmit_needed(cnx, application, cnx->path[0], early_time,
        &wake, ordinary, sizeof(ordinary->bytes), &header) == 0);
    assert(wake == now + picoquic_current_retransmit_timer(cnx, cnx->path[0]));
    assert(early_time < wake && !cnx->path[0]->is_pto_required);
    picoquic_recycle_packet(cnx->quic, ordinary);
    int has_data = repeat(cnx, early_time, copy, sizeof(copy), &length, &wake);
    fprintf(stderr, "POLICY %s%s: has_data=%d, length=%zu, early=%llu us, PTO=%llu us\n",
        fin ? "FIN" : "open", empty ? "-only" : "-data", has_data, length,
        (unsigned long long)early_delay,
        (unsigned long long)picoquic_current_retransmit_timer(cnx, cnx->path[0]));
    assert(has_data == 1 && length == frame_length);
    assert(memcmp(copy, frame, frame_length) == 0);
    assert(ctx->pending_first == packet && ctx->pending_last == packet);
    assert(packet->packet_next == NULL && packet->packet_previous == NULL);
    assert(packet->send_time == now && packet->sequence_number == 0);
    assert(packet->was_preemptively_repeated && cnx->nb_preemptive_repeat == 1);
    assert(cnx->path[0]->bytes_in_transit == inflight);
    assert(memcmp(packet->bytes + packet->offset, frame, frame_length) == 0);

    struct received received = {0};
    picoquic_cnx_t *peer = connection(&now, 0);
    picoquic_set_callback(peer, receive_bytes, &received);
    decode(peer, prefix, prefix_length, early_time);
    decode(peer, copy, length, early_time);
    decode(peer, packet->bytes + packet->offset, frame_length, early_time + 1);
    decode(peer, copy, length, early_time + 2);
    assert(received.length == 3 + data_length && received.fins == (unsigned)fin);
    assert(memcmp(received.bytes, payload, received.length) == 0);
    picoquic_free(peer->quic);

    for (unsigned i = 0; i < 3; i++) {
        assert(!repeat(cnx, early_time + i, copy, sizeof(copy), &length, &wake));
        assert(length == 0 && packet->was_preemptively_repeated);
        assert(ctx->pending_first == packet && ctx->pending_last == packet);
        assert(cnx->nb_preemptive_repeat == 1 && cnx->path[0]->bytes_in_transit == inflight);
    }
    picoquic_free(cnx->quic);
}

enum suppression { acked, reset, destroyed, zero_data };

static void suppressed(enum suppression reason) {
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = connection(&now, 1);
    uint8_t frame[64], copy[64]; size_t frame_length;
    assert(picoquic_add_to_stream(cnx, 0, payload, sizeof(payload), 0) == 0);
    frame_length = stream_frame(cnx, frame, sizeof(frame));
    picoquic_stream_head_t *stream = picoquic_find_stream(cnx, 0);
    assert(stream && !stream->fin_sent);
    if (reason == zero_data) {
        uint8_t *end = picoquic_format_stream_frame_header(frame, frame + sizeof(frame), 0, 3);
        assert(end);
        frame[0] |= 2; *end++ = 0; /* Valid explicit-length, non-FIN, empty STREAM. */
        frame_length = (size_t)(end - frame);
    }
    /* ACKed/destroyed/empty open STREAMs must not enable a needed PING either.
     * Real RESET sets fin_sent as well: check its STREAM suppression alone to
     * retain the intentional baseline FIN policy for other packet frames. */
    if (reason != reset) frame[frame_length++] = picoquic_frame_type_ping;
    picoquic_packet_t *packet = pending(cnx, frame, frame_length, now);
    if (reason == acked) {
        packet = pending(cnx, frame, frame_length, now);
        const uint8_t ack[] = {picoquic_frame_type_ack, 0, 0, 0, 0};
        decode(cnx, ack, sizeof(ack), now + 10000);
        assert(cnx->pkt_ctx[application].pending_first == packet);
        assert(picoquic_check_sack_list(&stream->sack_list, 0, sizeof(payload) - 1));
        /* Keep this suppression check inside the existing repeat age window,
         * independent of the ACK decoder's new RTT estimate. */
        cnx->path[0]->smoothed_rtt = 240000;
    } else if (reason == reset) {
        assert(picoquic_reset_stream(cnx, 0, 1) == 0);
        uint8_t reset_frame[64];
        assert(stream_frame(cnx, reset_frame, sizeof(reset_frame)) > 0);
        assert(stream->reset_sent);
    } else if (reason == destroyed) {
        picoquic_delete_stream(cnx, stream);
        assert(picoquic_find_stream(cnx, 0) == NULL);
    }
    uint64_t wake = 0, inflight = cnx->path[0]->bytes_in_transit;
    size_t length = 0;
    assert(!repeat(cnx, now + PICOQUIC_ACK_DELAY_MAX, copy, sizeof(copy), &length, &wake));
    assert(length == 0 && !packet->was_preemptively_repeated && cnx->nb_preemptive_repeat == 0);
    assert(cnx->pkt_ctx[application].pending_first == packet);
    assert(cnx->pkt_ctx[application].pending_last == packet);
    assert(cnx->path[0]->bytes_in_transit == inflight);
    picoquic_free(cnx->quic);
}

static void low_rtt_window(uint64_t rtt) {
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = connection(&now, 1);
    cnx->path[0]->smoothed_rtt = rtt;
    uint8_t frame[64], copy[64];
    assert(picoquic_add_to_stream(cnx, 0, payload, sizeof(payload), 0) == 0);
    size_t frame_length = stream_frame(cnx, frame, sizeof(frame));
    picoquic_packet_t *packet = pending(cnx, frame, frame_length, now);
    uint64_t inflight = cnx->path[0]->bytes_in_transit;
    uint64_t delay = rtt / 4, wake = UINT64_MAX;
    size_t length = 0;
    /* The old 100ms floor was at or beyond the rtt/2 exclusion boundary.
     * Enabled early repeat must have a usable interval, not one exact tick. */
    assert(delay < rtt / 2);
    assert(!repeat(cnx, now + delay - 1, copy, sizeof(copy), &length, &wake));
    assert(length == 0 && wake == now + delay && !packet->was_preemptively_repeated);
    assert(repeat(cnx, now + delay + 1, copy, sizeof(copy), &length, &wake));
    assert(length == frame_length && memcmp(copy, frame, frame_length) == 0);
    size_t copy_length = length;
    assert(packet->was_preemptively_repeated && cnx->nb_preemptive_repeat == 1);
    assert(cnx->pkt_ctx[application].pending_first == packet &&
        cnx->pkt_ctx[application].pending_last == packet);
    assert(cnx->path[0]->bytes_in_transit == inflight);
    assert(!repeat(cnx, now + delay + 2, copy, sizeof(copy), &length, &wake));
    assert(length == 0 && cnx->nb_preemptive_repeat == 1);

    struct received received = {0};
    picoquic_cnx_t *peer = connection(&now, 0);
    picoquic_set_callback(peer, receive_bytes, &received);
    decode(peer, copy, copy_length, now + delay + 1);
    decode(peer, packet->bytes + packet->offset, frame_length, now + delay + 2);
    assert(received.length == sizeof(payload) && received.fins == 0);
    assert(memcmp(received.bytes, payload, sizeof(payload)) == 0);
    fprintf(stderr, "POLICY low RTT=%llu us: early=%llu us, one copy, peer bytes once\n",
        (unsigned long long)rtt, (unsigned long long)delay);
    picoquic_free(peer->quic);
    picoquic_free(cnx->quic);
}

/* Exercise the real ready-packet sender, including ACK formatting, congestion
 * admission, packet protection and pending ownership, rather than only the
 * preemptive helper. No new transport entry point is exported. */
static picoquic_cnx_t *ready_sender(uint64_t *now, int multipath, uint64_t rtt) {
    picoquic_cnx_t *cnx = connection_mode(now, 1, multipath);
    picoquic_path_t *path = cnx->path[0];
    static const uint8_t secret[32] = {1, 2, 3, 4};
    cnx->crypto_context[3].aead_encrypt = picoquic_setup_test_aead_context(1,
        secret, PICOQUIC_LABEL_QUIC_V1_KEY_BASE);
    cnx->crypto_context[3].pn_enc = picoquic_pn_enc_create_for_test(secret,
        PICOQUIC_LABEL_QUIC_V1_KEY_BASE);
    assert(cnx->crypto_context[3].aead_encrypt && cnx->crypto_context[3].pn_enc);
    path->send_mtu = 143; path->challenge_verified = 1;
    path->latest_sent_time = *now; path->cwin = 1000;
    path->rtt_min = path->smoothed_rtt = rtt;
    path->rtt_is_initialized = 1; path->retransmit_timer = 1000000;
    cnx->quic->cwin_max = UINT64_MAX;
    cnx->local_parameters.active_connection_id_limit = 1;
    cnx->remote_parameters.active_connection_id_limit = 1;
    cnx->local_parameters.initial_max_path_id = 0;
    cnx->is_ack_frequency_updated = 0; cnx->keep_alive_interval = 0;
    cnx->no_ack_delay = 1;
    picoquic_set_congestion_algorithm(cnx, picoquic_dcubic_algorithm);
    path->cwin = 1000;
    picoquic_update_pacing_data(cnx, path, 1);
    return cnx;
}

static picoquic_packet_t *prepare_ready(picoquic_cnx_t *cnx, uint64_t now) {
    uint8_t wire[143]; size_t sent = 0;
    uint64_t wake = UINT64_MAX; int initial = 0;
    picoquic_packet_t *packet = picoquic_create_packet(cnx->quic);
    assert(packet);
    assert(picoquic_prepare_packet_ready(cnx, cnx->path[0], packet, now,
        wire, sizeof(wire), &sent, &wake, &initial) == 0);
    if (!sent) { picoquic_recycle_packet(cnx->quic, packet); return NULL; }
    assert(sent <= sizeof(wire) && packet->is_queued_for_retransmit);
    return packet;
}

static unsigned ready_frames(picoquic_packet_t *packet, unsigned *acks,
    const uint8_t *expected, size_t expected_length, uint64_t expected_offset) {
    unsigned streams = 0; *acks = 0;
    for (size_t i = packet->offset; i < packet->length;) {
        size_t used = 0; int pure = 0; uint64_t type = 0;
        assert(picoquic_frames_varint_decode(packet->bytes + i,
            packet->bytes + packet->length, &type));
        assert(picoquic_skip_frame(packet->bytes + i, packet->length - i,
            &used, &pure) == 0 && used);
        *acks += type == picoquic_frame_type_ack || type == picoquic_frame_type_ack_ecn ||
            type == picoquic_frame_type_path_ack || type == picoquic_frame_type_path_ack_ecn;
        if (type >= picoquic_frame_type_stream_range_min &&
            type <= picoquic_frame_type_stream_range_max) {
            uint64_t id = UINT64_MAX, offset = UINT64_MAX;
            size_t length = 0, header = 0; int fin = -1;
            assert(picoquic_parse_stream_header(packet->bytes + i, used,
                &id, &offset, &length, &fin, &header) == 0);
            assert(id == 0 && offset == expected_offset && !fin);
            assert(length == expected_length && header + length == used);
            assert(memcmp(packet->bytes + i + header, expected, length) == 0);
            streams++;
        }
        assert(type != picoquic_frame_type_ping);
        i += used;
    }
    return streams;
}

enum ack_slot_case { no_ack, coalesce_ack, fresh_data, no_room, forced_no_room, cwin_blocked };

static void ack_slot(int multipath, uint64_t rtt, enum ack_slot_case test) {
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = ready_sender(&now, multipath, rtt);
    picoquic_path_t *path = cnx->path[0];
    picoquic_packet_context_t *ctx = multipath ? &path->pkt_ctx : &cnx->pkt_ctx[application];
    size_t header = picoquic_predict_packet_header_length(cnx,
        picoquic_packet_1rtt_protected, ctx);
    uint8_t data[128], original_bytes[128]; memset(data, 0x36, sizeof(data));
    int large = test == no_room || test == forced_no_room;
    size_t data_length = large ? path->send_mtu - header - 16 - 4 : 45;
    assert(data_length > 0 && data_length < sizeof(data));
    assert(picoquic_add_to_stream(cnx, 0, data, data_length, 0) == 0);
    picoquic_packet_t *original = prepare_ready(cnx, now);
    assert(original && !original->was_preemptively_repeated);
    assert(picoquic_find_stream(cnx, 0)->send_queue == NULL);
    assert(original->length <= sizeof(original_bytes));
    memcpy(original_bytes, original->bytes, original->length);
    uint64_t original_time = original->send_time, original_number = original->sequence_number;
    now += (rtt == 140000 ? rtt / 4 : PICOQUIC_ACK_DELAY_MAX) + 1;
    if (test != no_ack) {
        assert(picoquic_record_pn_received(cnx, application,
            path->p_local_cnxid, 0, now) == 0);
        picoquic_set_ack_needed(cnx, now, application, path, 1);
    }
    if (test == fresh_data)
        assert(picoquic_add_to_stream(cnx, 0, data, data_length, 0) == 0);
    if (test == cwin_blocked) path->cwin = 1;
    if (test == forced_no_room) {
        cnx->is_forced_probe_up_required = 1; path->is_cca_probing_up = 1;
    }
    picoquic_packet_t *packet = prepare_ready(cnx, now);
    assert(packet && ctx->pending_first == original && original->is_queued_for_retransmit);
    assert(original->send_time == original_time && original->sequence_number == original_number);
    assert(memcmp(original->bytes, original_bytes, original->length) == 0);
    unsigned acks = 0;
    unsigned streams = ready_frames(packet, &acks, data, data_length,
        test == fresh_data ? data_length : 0);
    assert(acks == (unsigned)(test != no_ack));
    if (large || test == cwin_blocked) {
        /* A failed fit is not a copy. An ACK-only packet must not acquire the
         * copy flag, and forced-probe fallback must not append a PING here. */
        assert(!streams && !original->was_preemptively_repeated &&
            !packet->is_preemptive_repeat && cnx->nb_preemptive_repeat == 0);
        if (large) {
            packet = prepare_ready(cnx, now + 1);
            assert(packet && ready_frames(packet, &acks, data, data_length, 0) == 1 && !acks);
            assert(original->was_preemptively_repeated && packet->is_preemptive_repeat);
            assert(cnx->nb_preemptive_repeat == 1);
        }
    } else if (test == fresh_data) {
        assert(streams == 1 && !original->was_preemptively_repeated &&
            !packet->is_preemptive_repeat && cnx->nb_preemptive_repeat == 0);
    } else {
        assert(streams == 1 && original->was_preemptively_repeated &&
            packet->is_preemptive_repeat && cnx->nb_preemptive_repeat == 1);
    }
    fprintf(stderr, "POLICY ready ACK slot: mp=%d RTT=%llu case=%d copies=%llu checked\n",
        multipath, (unsigned long long)rtt, (int)test,
        (unsigned long long)cnx->nb_preemptive_repeat);
    picoquic_free(cnx->quic);
}

int main(void) {
    early_copy(1, 0);
    early_copy(1, 1); /* Empty FIN retains the baseline eligibility. */
    suppressed(acked); suppressed(reset); suppressed(destroyed); suppressed(zero_data);
    fputs("POLICY controls: FIN, ACKed, reset, destroyed, zero-data, peer duplicate PASS\n", stderr);
    early_copy(0, 0);
    fputs("POLICY open-data early copy: PASS (one copy, pending retained, peer bytes once)\n", stderr);
    low_rtt_window(40000);
    low_rtt_window(100000);
    low_rtt_window(140000);
    low_rtt_window(200000);
    for (int multipath = 0; multipath <= 1; multipath++) {
        for (int low = 0; low <= 1; low++) {
            for (int test = no_ack; test <= cwin_blocked; test++)
                ack_slot(multipath, low ? 140000 : 240000, (enum ack_slot_case)test);
        }
    }
    return 0;
}
