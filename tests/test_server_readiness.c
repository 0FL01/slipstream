#include <pthread.h>
#include "slipstream_sockloop.h"
#include "slipstream_runtime.h"
#include "slipstream_slot.h"
#include <picoquic_config.h>
#include <picosocks.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Script only datagram I/O, not TCP readiness, stream selection or formatting. */
static ss_loop app;
static ss_conn connection;
static picoquic_quic_t *quic;
static int peer_fd;
static unsigned callbacks, selected, incoming, prepares, encoded, submitted;
static const uint8_t data[] = {9, 8, 7, 6, 5, 4};

static int callback(picoquic_cnx_t *cnx, uint64_t id, uint8_t *bytes,
    size_t length, picoquic_call_back_event_t event, void *ctx, void *stream) {
    (void)cnx;
    return ss_stream_event(ctx, id, bytes, length, event, stream);
}

static int loop_callback(picoquic_quic_t *q, picoquic_packet_loop_cb_enum mode,
    void *ctx, void *argument) {
    (void)argument;
    assert(q == quic && ctx == &app && mode == picoquic_packet_loop_before_select);
    if (++callbacks == 3) return -1;
    ss_conn_service(&connection, picoquic_current_time());
    assert(picoquic_find_ready_stream(connection.cnx) == NULL);
    return 0;
}

static int wait_ready(ss_loop *loop, int fd, int64_t delay) {
    assert(loop == &app && fd == -1 && delay > 0);
    return 1;
}

static int select_packet(picoquic_socket_ctx_t *sockets, int count,
    struct sockaddr_storage *from, struct sockaddr_storage *to, int *index,
    unsigned char *ecn, uint8_t *bytes, int maximum, int64_t delay,
    int *wake, picoquic_network_thread_ctx_t *thread, int *rank) {
    (void)index; (void)ecn; (void)rank;
    assert(sockets->fd == -1 && count == 1 && maximum > 0 && delay == 0);
    assert(thread->loop_callback_ctx == &app);
    *wake = 0;
    from->ss_family = to->ss_family = AF_INET;
    bytes[0] = 0;
    return ++selected <= 3 ? 1 : 0;
}

static ssize_t decode_packet(void *slot, void *ctx, unsigned char **decoded,
    const unsigned char *bytes, size_t length, struct sockaddr_storage *from,
    struct sockaddr_storage *to) {
    (void)slot; (void)bytes; (void)from; (void)to;
    assert(ctx == &app && length == 1);
    *decoded = malloc(1); assert(*decoded); **decoded = 0;
    return 1;
}

static int receive_packet(picoquic_quic_t *q, uint8_t *bytes, size_t length,
    struct sockaddr *from, struct sockaddr *to, int index, unsigned char ecn,
    picoquic_cnx_t **cnx, int *path, uint64_t now) {
    (void)bytes; (void)from; (void)to; (void)index; (void)ecn; (void)now;
    assert(q == quic && length == 1 && ++incoming <= 3);
    *cnx = connection.cnx; *path = 0;
    return 0;
}

static int prepare_reply(picoquic_cnx_t *cnx, int path, uint64_t now,
    uint8_t *bytes, size_t maximum, size_t *length, struct sockaddr_storage *to,
    struct sockaddr_storage *from, int *index, size_t *segment) {
    (void)now; (void)index;
    assert(cnx == connection.cnx && path == 0 && maximum >= 128 && !segment);
    to->ss_family = from->ss_family = AF_INET;
    picoquic_stream_head_t *stream = picoquic_find_ready_stream(cnx);
    if (++prepares == 1) {
        assert(stream == NULL);
        *length = 0;
    } else {
        /* Backend data appeared while the earlier DNS response was encoded. */
        assert(prepares <= 3 && stream != NULL);
        uint64_t before = stream->sent_offset;
        int more = 0, pure_ack = 1, active = 0, ret = 0;
        uint8_t *end = picoquic_format_stream_frame(cnx, stream, bytes, bytes + 128,
            &more, &pure_ack, &active, &ret);
        assert(end && ret == 0 && stream->sent_offset - before == sizeof(data));
        uint64_t id, offset; size_t size, header; int fin;
        assert(picoquic_parse_stream_header(bytes, (size_t)(end - bytes),
            &id, &offset, &size, &fin, &header) == 0);
        assert(size == sizeof(data) && !memcmp(bytes + header, data, sizeof(data)));
        *length = (size_t)(end - bytes);
    }
    return 0;
}

static ssize_t encode_reply(void *ctx, void *loop, unsigned char **encoded_bytes,
    const unsigned char *bytes, size_t length, size_t *segment,
    struct sockaddr_storage *to, struct sockaddr_storage *from) {
    (void)bytes;
    slot_t *slot = ctx;
    assert(loop == &app && slot->cnx == connection.cnx);
    assert(to->ss_family == AF_INET && from->ss_family == AF_INET);
    if (++encoded == 1) {
        assert(length == 0);
        picoquic_set_callback(connection.cnx, callback, &connection);
        assert(send(peer_fd, data, sizeof(data), 0) == sizeof(data));
        assert(picoquic_find_ready_stream(connection.cnx) == NULL);
    } else {
        assert(encoded <= 3 && length > sizeof(data));
        if (encoded == 2) {
            ss_conn_service(&connection, picoquic_current_time());
            assert(picoquic_find_ready_stream(connection.cnx) == NULL);
            assert(send(peer_fd, data, sizeof(data), 0) == sizeof(data));
        }
    }
    *encoded_bytes = calloc(1, 256); assert(*encoded_bytes); *segment = 256;
    return 256;
}

static int send_reply(int fd, struct sockaddr *to, struct sockaddr *from,
    int index, const char *bytes, int length, int segment, int *error) {
    (void)index; (void)bytes;
    assert(fd == -1 && to->sa_family == AF_INET && from->sa_family == AF_INET);
    assert(length == 256 && segment == 0 && ++submitted <= 3);
    *error = 0; return length;
}

#define ss_runtime_wait wait_ready
#define picoquic_packet_loop_select select_packet
#define picoquic_incoming_packet_ex receive_packet
#define picoquic_prepare_packet_ex prepare_reply
#define picoquic_sendmsg send_reply
#include "../src/slipstream_sockloop.c"

int main(void) {
    uint64_t now = picoquic_current_time();
    picoquic_quic_config_t config; picoquic_config_init(&config);
    config.nb_connections = 1; config.alpn = "test"; config.enable_sslkeylog = 0;
    quic = picoquic_create_and_configure(&config, NULL, NULL, now, NULL); assert(quic);
    struct sockaddr_in address = {0}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(53);
    connection.cnx = picoquic_create_cnx(quic, picoquic_null_connection_id,
        picoquic_null_connection_id, (struct sockaddr *)&address, now, 0, "test", "test", 1);
    assert(connection.cnx); connection.cnx->cnx_state = picoquic_state_ready;
    connection.cnx->maxdata_remote = SS_BUFFER_LIMIT;
    connection.cnx->max_stream_id_bidir_remote = 64;
    /* The unbound server callback context must not service an unrelated head. */
    picoquic_set_callback(connection.cnx, callback, &app.head);
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    peer_fd = pair[1]; assert(ss_nonblock(pair[0]) == 0);
    ss_stream *s = ss_stream_create(&connection, pair[0], 0); assert(s); s->connected = true;
    assert(picoquic_set_app_stream_ctx(connection.cnx, 0, s) == 0);
    picoquic_find_stream(connection.cnx, 0)->maxdata_remote = SS_BUFFER_LIMIT;
    app.listener = -1; app.head.next = &connection;
    picoquic_packet_loop_param_t param = {0}; param.do_not_use_gso = 1;
    param.socket_buffer_size = 2048; param.decode = decode_packet; param.encode = encode_reply;
    picoquic_network_thread_ctx_t thread = {0}; thread.quic = quic; thread.param = &param;
    thread.loop_callback = loop_callback; thread.loop_callback_ctx = &app;
    picoquic_socket_ctx_t socket_ctx = {0}; socket_ctx.fd = -1;
    assert(slipstream_packet_loop_(&thread, &socket_ctx) == 0);
    assert(prepares == 3 && encoded == 3 && submitted == 3 && callbacks == 3);
    ss_conn_clear(&connection); close(peer_fd); picoquic_free(quic);
    return 0;
}
