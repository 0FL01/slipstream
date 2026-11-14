#include "picoquic_internal.h"

const uint8_t *picoquic_decode_stream_reset_frame(picoquic_cnx_t *, const uint8_t *, const uint8_t *);

uint64_t slipstream_test_get_max_data_limit(picoquic_quic_t *quic) {
    if (quic == NULL) {
        return 0;
    }
    return quic->max_data_limit;
}

int slipstream_test_get_defer_stream_data_consumption(picoquic_quic_t *quic) {
    if (quic == NULL) {
        return 0;
    }
    return quic->defer_stream_data_consumption ? 1 : 0;
}

/* A peer can reset before its data arrives; final size still consumes MAX_DATA.
 * Abandoned bytes must be released exactly once, or cancellations starve all streams. */
int slipstream_test_reset_consumption(picoquic_quic_t *quic) {
    struct sockaddr_in peer = {0};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(443);
    peer.sin_addr.s_addr = htonl(0x7f000001);
    picoquic_cnx_t *cnx = picoquic_create_cnx(quic, picoquic_null_connection_id,
        picoquic_null_connection_id, (struct sockaddr *)&peer,
        picoquic_current_time(), 0, "localhost", "test", 1);
    if (cnx == NULL) return -1;
    /* RESET_STREAM: server-initiated bidirectional stream 1, error 0, final size 8192. */
    const uint8_t frame[] = {4, 1, 0, 0x60, 0x00};
    int result = 0;
    for (int repeat = 0; repeat < 2; repeat++) {
        if (picoquic_decode_stream_reset_frame(cnx, frame, frame + sizeof(frame)) != frame + sizeof(frame)) result = -2;
        else if (cnx->data_received != 8192) result = -3;
        else if (cnx->data_consumed != 8192) result = -4;
        if (result != 0) break;
    }
    picoquic_delete_cnx(cnx);
    return result;
}
