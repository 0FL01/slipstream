#include "slipstream_runtime.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <picoquic_config.h>
#include <arpa/inet.h>

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

static int active_callback(picoquic_cnx_t *cnx, uint64_t id, uint8_t *bytes,
    size_t length, picoquic_call_back_event_t event, void *ctx, void *stream_ctx) {
    (void)cnx;
    return ss_stream_event(ctx, id, bytes, length, event, stream_ctx);
}

static size_t format_active(ss_conn *c, picoquic_stream_head_t *stream, size_t capacity) {
    uint8_t bytes[128];
    assert(capacity <= sizeof(bytes));
    int more = 0, pure_ack = 1, active = 0, ret = 0;
    uint64_t before = stream->sent_offset;
    assert(picoquic_format_stream_frame(c->cnx, stream, bytes, bytes + capacity,
        &more, &pure_ack, &active, &ret) != NULL && ret == 0);
    return (size_t)(stream->sent_offset - before);
}

static void active_readiness(bool client) {
    uint64_t now = picoquic_current_time();
    picoquic_quic_config_t config; picoquic_config_init(&config);
    config.nb_connections = 1; config.alpn = "test"; config.enable_sslkeylog = 0;
    picoquic_quic_t *quic = picoquic_create_and_configure(&config, NULL, NULL, now, NULL);
    assert(quic);
    struct sockaddr_in addr = {0}; addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = htons(53);
    ss_conn c = {0}; c.client = client; c.ready = true; c.last_activity = now;
    c.cnx = picoquic_create_cnx(quic, picoquic_null_connection_id, picoquic_null_connection_id,
        (struct sockaddr *)&addr, now, 0, "test", "test", 1);
    assert(c.cnx);
    c.cnx->cnx_state = picoquic_state_ready;
    c.cnx->max_stream_id_bidir_remote = 64; c.cnx->maxdata_remote = SS_BUFFER_LIMIT;
    picoquic_set_callback(c.cnx, active_callback, &c);
    int fds[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    assert(ss_nonblock(fds[0]) == 0 && ss_nonblock(fds[1]) == 0);
    ss_stream *s = ss_stream_create(&c, fds[0], 0); assert(s); s->connected = true;
    assert(picoquic_set_app_stream_ctx(c.cnx, 0, s) == 0);
    picoquic_stream_head_t *stream = picoquic_find_stream(c.cnx, 0); assert(stream);
    stream->maxdata_remote = SS_BUFFER_LIMIT;
    picoquic_path_t *path = c.cnx->path[0]; assert(path->p_remote_cnxid);
    path->challenge_verified = 1; path->send_mtu = 143;
    path->cwin = 1430; path->pacing.rate = 0; path->bytes_in_transit = 0;
    const uint8_t data[32] = {1, 2, 3};
    assert(send(fds[1], data, 6, 0) == 6);
    ss_conn_service(&c, now);
    assert(stream->is_active && picoquic_find_ready_stream(c.cnx) == stream);
    assert(format_active(&c, stream, 128) == 6);
    assert(stream->is_active); /* Real positive callback optimistically kept it active. */
    assert(ss_conn_poll_budget(&c, 0, now) == 0);
    ss_conn_service(&c, now);
    assert(!stream->is_active && picoquic_find_ready_stream(c.cnx) == NULL);
    assert(ss_conn_poll_budget(&c, 0, now) > 0);

    /* A later write must wake the same owner, and a partial drain stays active. */
    assert(send(fds[1], data, sizeof(data), 0) == sizeof(data));
    ss_loop loop = {0}; loop.head = c; loop.head.client = true; loop.listener = -1;
    int udp = socket(AF_INET, SOCK_DGRAM, 0); assert(udp >= 0);
    assert(ss_runtime_wait(&loop, udp, 50000) > 0);
    ss_conn_service(&c, now);
    size_t first = format_active(&c, stream, 16);
    assert(first > 0 && first < sizeof(data));
    ss_conn_service(&c, now); assert(stream->is_active);
    assert(format_active(&c, stream, 128) == sizeof(data) - first);
    ss_conn_service(&c, now); assert(!stream->is_active);

    /* Raw-socket readiness must not remove already queued QUIC bytes. */
    assert(picoquic_add_to_stream_with_ctx(c.cnx, 0, data, 3, 0, s) == 0);
    ss_conn_service(&c, now);
    assert(!stream->is_active && stream->send_queue && picoquic_find_ready_stream(c.cnx) == stream);
    assert(format_active(&c, stream, 128) == 3);
    assert(shutdown(fds[1], SHUT_WR) == 0);
    ss_conn_service(&c, now); assert(stream->is_active);
    assert(format_active(&c, stream, 128) == 0 && s->local_fin && stream->fin_sent);
    ss_conn_clear(&c); close(fds[1]); close(udp); picoquic_free(quic);
}

int main(void) {
    credentials(); framing(); scheduler(); receive_credit(); partial_output();
    active_readiness(false); active_readiness(true);
    return 0;
}
