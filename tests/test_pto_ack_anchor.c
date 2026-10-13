#include <picoquic_config.h>
#include <picoquic_internal.h>
#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Exercise exported recovery, real headers/queues and the existing frame
 * classifier. No production test hook or replacement timer implementation. */
void picoquic_insert_hole_in_send_sequence_if_needed(picoquic_cnx_t *cnx,
    picoquic_path_t *path, picoquic_packet_context_t *ctx, uint64_t now, uint64_t *wake);
static const picoquic_packet_context_enum application = picoquic_packet_context_application;
static const uint8_t payload[] = {'a', 'b', 0, 1, 0xff, 'c'};
static unsigned failures, timeout_notifications, repeat_notifications;
static picoquic_congestion_algorithm_t observer;

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); failures++; \
} } while (0)

static void observe_loss(picoquic_cnx_t *cnx, picoquic_path_t *path,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t *state, uint64_t now) {
    timeout_notifications += notification == picoquic_congestion_notification_timeout;
    repeat_notifications += notification == picoquic_congestion_notification_repeat;
    picoquic_dcubic_algorithm->alg_notify(cnx, path, notification, state, now);
}

static picoquic_packet_context_t *context(picoquic_cnx_t *cnx,
    picoquic_path_t *path, picoquic_packet_context_enum pc) {
    return cnx->is_multipath_enabled && pc == application ? &path->pkt_ctx : &cnx->pkt_ctx[pc];
}

static picoquic_cnx_t *connection(uint64_t *now, int multipath, int two_paths) {
    picoquic_quic_config_t config;
    picoquic_config_init(&config);
    config.nb_connections = 1; config.alpn = "test"; config.enable_sslkeylog = 0;
    config.multipath_option = multipath;
    picoquic_quic_t *quic = picoquic_create_and_configure(&config, NULL, NULL, *now, now);
    assert(quic);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(53);
    picoquic_cnx_t *cnx = picoquic_create_cnx(quic, picoquic_null_connection_id,
        picoquic_null_connection_id, (struct sockaddr *)&address, *now, 0, "test", "test", 1);
    assert(cnx);
    cnx->cnx_state = picoquic_state_ready;
    cnx->is_multipath_enabled = multipath != 0;
    cnx->remote_parameters = cnx->local_parameters;
    cnx->maxdata_remote = cnx->remote_parameters.initial_max_data;
    cnx->max_stream_id_bidir_remote = STREAM_ID_FROM_RANK(
        cnx->remote_parameters.initial_max_stream_id_bidir, 1, 0);
    cnx->path[0]->p_remote_cnxid->cnx_id.id_len = 8;
    memset(cnx->path[0]->p_remote_cnxid->cnx_id.id, 0xa5, 8);
    if (two_paths) {
        address.sin_port = htons(54);
        assert(picoquic_create_path(cnx, *now, NULL, (struct sockaddr *)&address, 7) == 1);
        cnx->path[1]->p_local_cnxid = picoquic_create_local_cnxid(cnx, 7, NULL, *now);
        assert(cnx->path[1]->p_local_cnxid);
        uint8_t cid[8], reset[PICOQUIC_RESET_SECRET_SIZE] = {0};
        memset(cid, 0xb6, sizeof(cid));
        picoquic_remote_cnxid_t *stashed = NULL;
        assert(picoquic_stash_remote_cnxid(cnx, 0, multipath ? 7 : 0, 1,
            sizeof(cid), cid, reset, &stashed) == 0 && stashed);
        assert(picoquic_assign_peer_cnxid_to_path(cnx, 1) == 0);
    }
    for (int i = 0; i < cnx->nb_paths; i++) {
        cnx->path[i]->smoothed_rtt = 40000;
        cnx->path[i]->rtt_min = 40000;
        cnx->path[i]->retransmit_timer = 100000;
    }
    observer = *picoquic_dcubic_algorithm;
    observer.alg_notify = observe_loss;
    picoquic_set_congestion_algorithm(cnx, &observer);
    timeout_notifications = repeat_notifications = 0;
    return cnx;
}

static picoquic_packet_t *pending(picoquic_cnx_t *cnx, picoquic_path_t *path,
    picoquic_packet_context_enum pc, const uint8_t *frame, size_t length, uint64_t now) {
    picoquic_packet_context_t *ctx = context(cnx, path, pc);
    picoquic_packet_t *packet = picoquic_create_packet(cnx->quic);
    assert(packet);
    packet->ptype = pc == application ? picoquic_packet_1rtt_protected : picoquic_packet_handshake;
    packet->pc = pc;
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
    assert(ctx->pending_last == packet && !packet->is_evaluated);
    return packet;
}

static picoquic_packet_t *stream_packet(picoquic_cnx_t *cnx, picoquic_path_t *path, uint64_t now) {
    uint8_t frame[64];
    assert(picoquic_add_to_stream(cnx, 0, payload, sizeof(payload), 0) == 0);
    int more = 0, pure = 1, active = 0, ret = 0;
    uint8_t *end = picoquic_format_stream_frame(cnx, picoquic_find_stream(cnx, 0),
        frame, frame + sizeof(frame), &more, &pure, &active, &ret);
    assert(end > frame && !pure && ret == 0);
    return pending(cnx, path, application, frame, (size_t)(end - frame), now);
}

enum tail_kind { ack_tail, poll_tail, mixed_tail, trap_tail, no_tail };
static const char *tail_name[] = {"ACK", "POLL", "ACK+POLL", "trap", "none"};

static picoquic_packet_t *tail(picoquic_cnx_t *cnx, picoquic_path_t *path,
    picoquic_packet_context_enum pc, enum tail_kind kind, uint64_t now) {
    if (kind == trap_tail) {
        picoquic_packet_context_t *ctx = context(cnx, path, pc);
        cnx->quic->sequence_hole_pseudo_period = 16;
        ctx->next_sequence_hole = ctx->send_sequence;
        uint64_t wake = UINT64_MAX;
        picoquic_insert_hole_in_send_sequence_if_needed(cnx, path, ctx, now, &wake);
        assert(ctx->pending_last->is_ack_trap && ctx->pending_last->send_path == NULL);
        return ctx->pending_last;
    }
    uint8_t frame[] = {picoquic_frame_type_ack, 0, 0, 0, 0, picoquic_frame_type_poll};
    const uint8_t *bytes = kind == poll_tail ? frame + 5 : frame;
    size_t length = kind == poll_tail ? 1 : kind == mixed_tail ? 6 : 5;
    size_t consumed = 0; int pure = 0;
    assert(picoquic_skip_frame(bytes, length, &consumed, &pure) == 0 && pure);
    return pending(cnx, path, pc, bytes, length, now);
}

static uint64_t recover(picoquic_cnx_t *cnx, picoquic_path_t *path,
    picoquic_packet_context_enum pc, uint64_t now) {
    picoquic_packet_context_t *ctx = context(cnx, path, pc);
    uint64_t sequence = ctx->send_sequence, wake = UINT64_MAX;
    picoquic_packet_t *output = picoquic_create_packet(cnx->quic);
    assert(output);
    size_t header = 0;
    int length = picoquic_retransmit_needed(cnx, pc, path, now,
        &wake, output, sizeof(output->bytes), &header);
    assert(length >= 0 && ctx->send_sequence == sequence);
    if (header != 0) {
        /* Recovery prepares the next fresh PN on the selected send path;
         * ownership and protection remain in the ordinary sender. */
        assert(output->sequence_number == sequence && output->send_path == path && output->pc == pc);
    }
    picoquic_recycle_packet(cnx->quic, output);
    return wake;
}

static void ownership(picoquic_cnx_t *cnx, picoquic_packet_t *old,
    picoquic_path_t *path, uint64_t sent, uint64_t pn, const uint8_t *bytes, size_t length) {
    CHECK(old->send_path == path && old->send_time == sent && old->sequence_number == pn);
    CHECK(old->length == length && memcmp(old->bytes, bytes, length) == 0);
    CHECK(!old->is_queued_for_retransmit && old->is_queued_for_data_repeat);
    CHECK(old->is_queued_for_spurious_detection && picoquic_first_data_repeat_packet(cnx) == old);
}

/* Reach nb_retransmit > 0 via actual PTOs, then submit the actual repeated
 * STREAM bytes under fresh headers/PNs as the ordinary sender does. */
static picoquic_packet_t *prime(picoquic_cnx_t *cnx, uint64_t *now, unsigned rounds) {
    picoquic_path_t *path = cnx->path[0];
    picoquic_packet_t *old = stream_packet(cnx, path, *now);
    for (unsigned i = 0; i < rounds; i++) {
        *now += picoquic_current_retransmit_timer(cnx, path);
        (void)recover(cnx, path, application, *now);
        assert(path->is_pto_required && path->nb_retransmit == i + 1);
        assert(!old->is_queued_for_retransmit && old->is_queued_for_data_repeat);
        uint8_t frame[64]; int more = 0, pure = 1;
        uint8_t *end = picoquic_copy_stream_frames_for_retransmit(cnx,
            frame, frame + sizeof(frame), UINT64_MAX, &more, &pure);
        assert(end > frame && !pure && !more && !old->is_queued_for_data_repeat);
        old = pending(cnx, path, application, frame, (size_t)(end - frame), *now);
        path->is_pto_required = 0;
    }
    return old;
}

static void anchor_case(int multipath, unsigned rounds, enum tail_kind kind) {
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = connection(&now, multipath, 0);
    picoquic_path_t *path = cnx->path[0];
    picoquic_packet_t *old = prime(cnx, &now, rounds);
    picoquic_packet_context_t *ctx = context(cnx, path, application);
    uint64_t rto = picoquic_current_retransmit_timer(cnx, path), due = now + rto;
    assert(rto == (100000ULL << rounds));
    uint64_t sent = old->send_time, pn = old->sequence_number;
    size_t length = old->length; uint8_t original[PICOQUIC_MAX_PACKET_SIZE];
    memcpy(original, old->bytes, length);
    uint64_t losses = cnx->nb_retransmission_total, timers = path->nb_timer_losses;
    unsigned notifications = timeout_notifications;
    if (kind != no_tail) {
        (void)tail(cnx, path, application, kind, due - 1000);
        /* A second refresh tests a chain of non-ack-eliciting packets. Traps
         * cannot be consecutive by the existing hole insertion API. */
        if (kind != trap_tail) (void)tail(cnx, path, application, kind, due - 100);
    }
    uint64_t inflight = path->bytes_in_transit, sequence = ctx->send_sequence;
    uint64_t wake = recover(cnx, path, application, due - 1);
    CHECK(wake == due && old->is_queued_for_retransmit && !path->is_pto_required);
    CHECK(path->bytes_in_transit == inflight && ctx->send_sequence == sequence);
    CHECK(cnx->nb_retransmission_total == losses && path->nb_retransmit == rounds);
    (void)recover(cnx, path, application, due);
    int recovered = !old->is_queued_for_retransmit;
    fprintf(stderr, "ANCHOR mp=%d tail=%s prior_PTO=%u STREAM=%" PRIu64
        " RTO=%" PRIu64 " due=%" PRIu64 " prewake=%" PRIu64
        " recovered=%d loss_delta=%" PRIu64 " timer_delta=%" PRIu64 " nb_retransmit=%" PRIu64 "\n",
        multipath, tail_name[kind], rounds, sent, rto, due, wake, recovered,
        cnx->nb_retransmission_total - losses, path->nb_timer_losses - timers, path->nb_retransmit);
    CHECK(recovered && path->is_pto_required && path->nb_retransmit == rounds + 1);
    CHECK(cnx->nb_retransmission_total == losses + 1 && path->nb_timer_losses == timers + 1);
    CHECK(timeout_notifications == notifications + 1 && repeat_notifications == 0);
    uint64_t actual = due;
    if (!recovered) {
        actual = (kind == trap_tail ? due - 1000 : due - 100) + rto;
        (void)recover(cnx, path, application, actual);
        assert(!old->is_queued_for_retransmit);
    }
    fprintf(stderr, "ACTUAL mp=%d tail=%s prior_PTO=%u first_recovery=%" PRIu64
        " loss_delta=%" PRIu64 " timer_delta=%" PRIu64 " nb_retransmit=%" PRIu64 "\n",
        multipath, tail_name[kind], rounds, actual, cnx->nb_retransmission_total - losses,
        path->nb_timer_losses - timers, path->nb_retransmit);
    ownership(cnx, old, path, sent, pn, original, length);
    CHECK(path->retransmit_timer == 100000 && path->smoothed_rtt == 40000);
    picoquic_free(cnx->quic);
}

static void new_data_and_context(int multipath) {
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = connection(&now, multipath, 1);
    picoquic_packet_t *old = stream_packet(cnx, cnx->path[0], now);
    picoquic_packet_t *newer = stream_packet(cnx, cnx->path[1], now + 50000);
    /* MP application is path-local; legacy application intentionally shares
     * a context across paths. Do not introduce a new legacy path filter. */
    uint64_t due = now + (multipath ? 100000 : 150000);
    (void)recover(cnx, cnx->path[0], application, now + 99999);
    CHECK(old->is_queued_for_retransmit && !cnx->path[0]->is_pto_required);
    (void)recover(cnx, cnx->path[0], application, due);
    CHECK(!old->is_queued_for_retransmit && newer->is_queued_for_retransmit);
    CHECK(cnx->path[0]->nb_retransmit == 1 && cnx->path[1]->nb_retransmit == 0);
    picoquic_free(cnx->quic);

    cnx = connection(&now, multipath, 0);
    old = stream_packet(cnx, cnx->path[0], now);
    newer = stream_packet(cnx, cnx->path[0], now + 50000);
    CHECK(recover(cnx, cnx->path[0], application, now + 100000) == now + 150000);
    CHECK(old->is_queued_for_retransmit && newer->is_queued_for_retransmit);
    (void)recover(cnx, cnx->path[0], application, now + 150000);
    CHECK(!old->is_queued_for_retransmit && newer->is_queued_for_retransmit);
    picoquic_free(cnx->quic);
    fprintf(stderr, "CONTROL mp=%d newer STREAM / existing context selection checked\n", multipath);
}

static void safety_and_rack(int multipath) {
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = connection(&now, multipath, 0);
    CHECK(recover(cnx, cnx->path[0], application, now) == UINT64_MAX);
    CHECK(cnx->nb_retransmission_total == 0 && !cnx->path[0]->is_pto_required);
    picoquic_free(cnx->quic);
    for (enum tail_kind kind = ack_tail; kind <= mixed_tail; kind++) {
        cnx = connection(&now, multipath, 0);
        picoquic_path_t *path = cnx->path[0];
        (void)tail(cnx, path, application, kind, now);
        CHECK(recover(cnx, path, application, now + 99999) == now + 100000);
        (void)recover(cnx, path, application, now + 100000);
        CHECK(context(cnx, path, application)->pending_first == NULL && path->bytes_in_transit == 0);
        CHECK(cnx->nb_retransmission_total == 0 && path->nb_retransmit == 0 && !path->is_pto_required);
        picoquic_free(cnx->quic);
    }
    cnx = connection(&now, multipath, 0);
    picoquic_path_t *path = cnx->path[0];
    picoquic_packet_context_t *ctx = context(cnx, path, application);
    picoquic_packet_t *ack = tail(cnx, path, application, ack_tail, now);
    (void)tail(cnx, path, application, trap_tail, now + 99999);
    (void)picoquic_dequeue_retransmit_packet(cnx, ctx, ack, 1, 0);
    (void)recover(cnx, path, application, now + 100000);
    CHECK(ctx->pending_first == NULL && cnx->nb_retransmission_total == 0 && path->nb_retransmit == 0);
    picoquic_free(cnx->quic);

    cnx = connection(&now, multipath, 0);
    path = cnx->path[0]; ctx = context(cnx, path, application);
    picoquic_packet_t *old = stream_packet(cnx, path, now);
    (void)tail(cnx, path, application, poll_tail, now + 19000);
    /* Actual ACK-derived RACK state, with no timer expiration. This is a
     * recovery fixture, not an alternate ACK decoder: decoder gates already
     * exercise those fields separately. */
    ctx->highest_acknowledged = old->sequence_number + 1;
    ctx->latest_time_acknowledged = now + 10000;
    ctx->highest_acknowledged_time = now + 20000;
    uint64_t rack_delay = path->smoothed_rtt >> 2;
    if (rack_delay > PICOQUIC_RACK_DELAY / 2) rack_delay = PICOQUIC_RACK_DELAY / 2;
    uint64_t due = ctx->highest_acknowledged_time + rack_delay - 10000 + cnx->remote_parameters.max_ack_delay;
    if (due > now + path->retransmit_timer) due = now + path->retransmit_timer;
    CHECK(due < ctx->pending_last->send_time + picoquic_current_retransmit_timer(cnx, path));
    CHECK(recover(cnx, path, application, due - 1) == due && old->is_queued_for_retransmit);
    (void)recover(cnx, path, application, due);
    CHECK(!old->is_queued_for_retransmit && cnx->nb_retransmission_total == 1);
    CHECK(path->nb_timer_losses == 0 && path->nb_retransmit == 0 && !path->is_pto_required);
    CHECK(repeat_notifications == 1 && timeout_notifications == 0);
    fprintf(stderr, "CONTROL mp=%d empty/nonack/trap safety; RACK=%" PRIu64
        " losses=%" PRIu64 " timer_losses=%" PRIu64 " checked\n",
        multipath, due, cnx->nb_retransmission_total, path->nb_timer_losses);
    picoquic_free(cnx->quic);
}

static void other_packet_context(void) {
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = connection(&now, 1, 0);
    picoquic_path_t *path = cnx->path[0];
    /* MP handshake retains its connection-wide number space; an application
     * ACK tail must not enter its timer selection. */
    const uint8_t ping[] = {picoquic_frame_type_ping};
    picoquic_packet_t *old = pending(cnx, path, picoquic_packet_context_handshake, ping, sizeof(ping), now);
    (void)tail(cnx, path, application, ack_tail, now + 99999);
    CHECK(recover(cnx, path, picoquic_packet_context_handshake, now + 99999) == now + 100000);
    (void)recover(cnx, path, picoquic_packet_context_handshake, now + 100000);
    CHECK(!old->is_queued_for_retransmit && path->is_pto_required && path->nb_retransmit == 1);
    picoquic_free(cnx->quic);
}

int main(void) {
    for (int mp = 0; mp <= 1; mp++) {
        new_data_and_context(mp);
        safety_and_rack(mp);
        for (unsigned rounds = 0; rounds <= 2; rounds++) {
            for (enum tail_kind kind = ack_tail; kind <= no_tail; kind++) anchor_case(mp, rounds, kind);
        }
    }
    other_packet_context();
    fprintf(stderr, "PTO ACK anchor: %u failed checks\n", failures);
    return failures != 0;
}
