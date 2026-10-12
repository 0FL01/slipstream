#include <picoquic_config.h>
#include <picoquic_internal.h>
#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint64_t acknowledged;
static unsigned ack_notifications;

/* Observe the real decoder's CC input, then forward to the real Dcubic. */
static void observe_ack(picoquic_cnx_t *cnx, picoquic_path_t *path,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t *ack_state, uint64_t now) {
    if (notification == picoquic_congestion_notification_acknowledgement) {
        acknowledged += ack_state->nb_bytes_acknowledged;
        ack_notifications++;
    }
    picoquic_dcubic_algorithm->alg_notify(cnx, path, notification, ack_state, now);
}

static void ack_packet(uint8_t frame_type) {
    uint64_t now = 1000000;
    picoquic_quic_config_t config;
    picoquic_config_init(&config);
    config.nb_connections = 1; config.alpn = "test"; config.enable_sslkeylog = 0;
    picoquic_quic_t *quic = picoquic_create_and_configure(&config, NULL, NULL, now, &now);
    assert(quic);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(53);
    picoquic_cnx_t *cnx = picoquic_create_cnx(quic, picoquic_null_connection_id,
        picoquic_null_connection_id, (struct sockaddr *)&address, now, 0, "test", "test", 1);
    assert(cnx);
    cnx->cnx_state = picoquic_state_ready;
    picoquic_path_t *path = cnx->path[0];
    path->p_remote_cnxid->cnx_id.id_len = 8;
    memset(path->p_remote_cnxid->cnx_id.id, 0xa5, 8);
    picoquic_congestion_algorithm_t observer = *picoquic_dcubic_algorithm;
    observer.alg_notify = observe_ack;
    picoquic_set_congestion_algorithm(cnx, &observer);
    acknowledged = 0; ack_notifications = 0;

    /* Pending packets are stored unencrypted. Build a short header and one
     * frame with padding sufficient for header protection. */
    picoquic_packet_context_t *context = &cnx->pkt_ctx[picoquic_packet_context_application];
    assert(context->send_sequence == 0 && context->pending_first == NULL);
    picoquic_packet_t *packet = picoquic_create_packet(quic);
    assert(packet);
    packet->ptype = picoquic_packet_1rtt_protected;
    packet->pc = picoquic_packet_context_application;
    size_t header_length = picoquic_predict_packet_header_length(cnx, packet->ptype, context);
    size_t pn_offset = 0, pn_length = 0;
    packet->offset = picoquic_create_packet_header(cnx, packet->ptype,
        context->send_sequence, path, header_length, packet->bytes, &pn_offset, &pn_length);
    assert(packet->offset == header_length && pn_length > 0);
    packet->sequence_number = context->send_sequence++;
    packet->send_path = path; packet->send_time = now;
    packet->delivered_prior = path->delivered_last;
    packet->delivered_time_prior = path->delivered_time_last;
    packet->delivered_sent_prior = path->delivered_sent_last;
    packet->lost_prior = path->total_bytes_lost;
    packet->inflight_prior = path->bytes_in_transit;
    packet->bytes[packet->offset] = frame_type;
    memset(packet->bytes + packet->offset + 1, picoquic_frame_type_padding, 3);
    packet->length = packet->offset + 4;
    packet->checksum_overhead = 16;
    size_t length = packet->length;
    picoquic_queue_for_retransmit(cnx, path, packet, length + packet->checksum_overhead, now);
    assert(context->pending_first == packet && path->bytes_in_transit == length + 16);

    /* ACK packet zero, no delay, no extra ranges. This runs ACK-range recording,
     * ACK-of-frames processing and the final congestion notification together. */
    const uint8_t ack[] = {picoquic_frame_type_ack, 0, 0, 0, 0};
    now += 10000;
    assert(picoquic_decode_frames(cnx, path, ack, sizeof(ack), NULL,
        picoquic_epoch_1rtt, (struct sockaddr *)&address, (struct sockaddr *)&address,
        0, 0, now) == 0);
    assert(context->pending_first == NULL && context->pending_last == NULL);
    assert(path->bytes_in_transit == 0 && path->delivered == length);
    assert(path->last_time_acked_data_frame_sent ==
        (frame_type == picoquic_frame_type_poll ? now : 0));
    assert(ack_notifications == 1);
    if (acknowledged != length) {
        fprintf(stderr, "frame 0x%02x: ACK credit=%" PRIu64 ", packet length=%zu\n",
            frame_type, acknowledged, length);
    }
    assert(acknowledged == length);
    picoquic_free(quic);
}

int main(void) {
    /* PING still depends on the common packet-level recorder. */
    ack_packet(picoquic_frame_type_ping);
    ack_packet(picoquic_frame_type_poll);
    return 0;
}
