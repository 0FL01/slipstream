#include <picoquic_config.h>
#include <picoquic_internal.h>
#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const picoquic_packet_context_enum application = picoquic_packet_context_application;

static picoquic_cnx_t *create_connection(uint64_t *now, int multipath) {
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
    /* Model negotiated multipath, using real initialized paths and registered
     * LCIDs. Unique path ID 7 deliberately differs from array index 1. */
    cnx->cnx_state = picoquic_state_ready;
    cnx->is_multipath_enabled = multipath != 0;
    cnx->local_parameters.ack_delay_exponent = 3;
    address.sin_port = htons(54);
    assert(picoquic_create_path(cnx, *now, NULL, (struct sockaddr *)&address, 7) == 1);
    cnx->path[1]->p_local_cnxid = picoquic_create_local_cnxid(cnx, 7, NULL, *now);
    assert(cnx->path[1]->p_local_cnxid && cnx->path[1]->p_local_cnxid->path_id == 7);
    assert(cnx->nb_paths == 2 && cnx->path[0]->unique_path_id == 0);
    assert(cnx->path[1]->unique_path_id == 7);
    return cnx;
}

static void snapshot(picoquic_ack_context_t *copy, const picoquic_ack_context_t *context) {
    memcpy(copy, context, sizeof(*copy));
}

static void unchanged(const picoquic_ack_context_t *context, const picoquic_ack_context_t *before) {
    assert(memcmp(context, before, sizeof(*context)) == 0);
}

static void check_ack(picoquic_cnx_t *cnx, picoquic_packet_context_enum pc,
    uint64_t now, int multipath, uint64_t expected_path) {
    uint8_t frame[256];
    int more_data = 0;
    uint8_t *end = picoquic_format_ack_frame(cnx, frame, frame + sizeof(frame),
        &more_data, now + 8000, pc, 0);
    assert(end && end > frame && more_data == 0);
    size_t length = (size_t)(end - frame);
    uint64_t type = 0, blocks = 99, path = UINT64_MAX, largest = 0, delay = UINT64_MAX;
    assert(picoquic_varint_decode(frame, length, &type) > 0);
    assert(type == (multipath ? picoquic_frame_type_path_ack : picoquic_frame_type_ack));
    size_t consumed = 0;
    /* Exponent zero exposes the encoded value, not a decoded microsecond delay. */
    assert(picoquic_parse_ack_header(frame, length, &blocks, multipath ? &path : NULL,
        &largest, &delay, &consumed, 0) == 0);
    assert(!multipath || path == expected_path);
    assert(largest == 41 && blocks == 0);
    uint64_t range = UINT64_MAX;
    size_t range_length = picoquic_varint_decode(frame + consumed, length - consumed, &range);
    assert(range_length > 0 && range == 0 && consumed + range_length == length);
    if (delay != 1000) {
        fprintf(stderr, "path=%" PRIu64 ": encoded ACK delay=%" PRIu64 ", expected=1000 (8000us >> 3)\n",
            multipath ? path : UINT64_MAX, delay);
    }
    assert(delay == 1000);
}

static void check_reordering(picoquic_cnx_t *cnx, picoquic_packet_context_enum pc,
    picoquic_local_cnxid_t *l_cid, picoquic_ack_context_t *selected, uint64_t now) {
    /* A contiguous new largest packet updates only its timestamp. */
    assert(picoquic_record_pn_received(cnx, pc, l_cid, 42, now + 1) == 0);
    assert(selected->time_stamp_largest_received == now + 1);
    assert(!selected->act[0].out_of_order_received && !selected->act[1].out_of_order_received);
    /* A gap sets both tracks in the same context as the SACK range. */
    assert(picoquic_record_pn_received(cnx, pc, l_cid, 44, now + 2) == 0);
    assert(selected->time_stamp_largest_received == now + 2);
    assert(selected->act[0].out_of_order_received && selected->act[1].out_of_order_received);
    /* An older packet tests each track's own ACK-needed and highest-ACK state. */
    selected->act[0].out_of_order_received = 0;
    selected->act[1].out_of_order_received = 0;
    selected->act[0].highest_ack_sent = 44; selected->act[0].ack_needed = 1;
    selected->act[1].highest_ack_sent = 44; selected->act[1].ack_needed = 0;
    assert(picoquic_record_pn_received(cnx, pc, l_cid, 40, now + 3) == 0);
    assert(selected->act[0].out_of_order_received && !selected->act[1].out_of_order_received);
    selected->act[0].out_of_order_received = 0;
    selected->act[0].ack_needed = 0; selected->act[1].ack_needed = 1;
    assert(picoquic_record_pn_received(cnx, pc, l_cid, 39, now + 4) == 0);
    assert(!selected->act[0].out_of_order_received && selected->act[1].out_of_order_received);
    selected->act[1].out_of_order_received = 0;
    selected->act[0].ack_needed = 1;
    selected->act[0].highest_ack_sent = 38; selected->act[1].highest_ack_sent = 38;
    assert(picoquic_record_pn_received(cnx, pc, l_cid, 38, now + 5) == 0);
    assert(!selected->act[0].out_of_order_received && !selected->act[1].out_of_order_received);
    assert(selected->time_stamp_largest_received == now + 2);
    assert(picoquic_sack_list_last(&selected->sack_list) == 44);
}

static void connection_context(int multipath, picoquic_packet_context_enum pc) {
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = create_connection(&now, multipath);
    picoquic_ack_context_t path0_before, path1_before;
    snapshot(&path0_before, &cnx->path[0]->ack_ctx);
    snapshot(&path1_before, &cnx->path[1]->ack_ctx);
    picoquic_local_cnxid_t *l_cid = cnx->path[0]->p_local_cnxid;
    assert(l_cid);
    assert(picoquic_record_pn_received(cnx, pc, l_cid, 41, now) == 0);
    check_ack(cnx, pc, now, 0, 0);
    assert(cnx->ack_ctx[pc].time_stamp_largest_received == now);
    check_reordering(cnx, pc, l_cid, &cnx->ack_ctx[pc], now);
    unchanged(&cnx->path[0]->ack_ctx, &path0_before);
    unchanged(&cnx->path[1]->ack_ctx, &path1_before);
    picoquic_free(cnx->quic);
}

static void multipath_context(int null_l_cid) {
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = create_connection(&now, 1);
    int index = null_l_cid ? 0 : 1;
    picoquic_ack_context_t *selected = &cnx->path[index]->ack_ctx;
    picoquic_local_cnxid_t *l_cid = null_l_cid ? NULL : cnx->path[index]->p_local_cnxid;
    picoquic_ack_context_t other_before, connection_before;
    snapshot(&other_before, &cnx->path[1 - index]->ack_ctx);
    snapshot(&connection_before, &cnx->ack_ctx[application]);
    assert(selected->time_stamp_largest_received == UINT64_MAX);
    assert(picoquic_record_pn_received(cnx, application, l_cid, 41, now) == 0);
    /* Check the actual wire delay first so unpatched code fails with delay 0. */
    check_ack(cnx, application, now, 1, cnx->path[index]->unique_path_id);
    assert(selected->time_stamp_largest_received == now);
    unchanged(&cnx->path[1 - index]->ack_ctx, &other_before);
    unchanged(&cnx->ack_ctx[application], &connection_before);
    check_reordering(cnx, application, l_cid, selected, now);
    unchanged(&cnx->path[1 - index]->ack_ctx, &other_before);
    unchanged(&cnx->ack_ctx[application], &connection_before);
    picoquic_free(cnx->quic);
}

static void unknown_path_fallback(void) {
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = create_connection(&now, 1);
    picoquic_local_cnxid_t *l_cid = picoquic_create_local_cnxid(cnx, 11, NULL, now);
    assert(l_cid && picoquic_find_path_by_unique_id(cnx, l_cid->path_id) < 0);
    picoquic_ack_context_t path0_before, path1_before;
    snapshot(&path0_before, &cnx->path[0]->ack_ctx);
    snapshot(&path1_before, &cnx->path[1]->ack_ctx);
    assert(picoquic_record_pn_received(cnx, application, l_cid, 41, now) == 0);
    assert(cnx->ack_ctx[application].time_stamp_largest_received == now);
    check_reordering(cnx, application, l_cid, &cnx->ack_ctx[application], now);
    unchanged(&cnx->path[0]->ack_ctx, &path0_before);
    unchanged(&cnx->path[1]->ack_ctx, &path1_before);
    picoquic_free(cnx->quic);
}

int main(void) {
    connection_context(0, application);
    fputs("legacy ACK context: PASS\n", stderr);
    multipath_context(0);
    multipath_context(1); /* NULL LCID keeps the existing default-path behavior. */
    connection_context(1, picoquic_packet_context_handshake);
    connection_context(1, picoquic_packet_context_initial);
    unknown_path_fallback();
    fputs("path ACK context: PASS (unique ID, NULL LCID, reorder, legacy/fallback)\n", stderr);
    return 0;
}
