#include "slipstream.h"
#include "slipstream_runtime.h"
#include "slipstream_sockloop.h"
#include "slipstream_server_cc.h"
#include <picoquic_config.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

extern volatile sig_atomic_t should_shutdown;
extern void server_sighandler(int);
extern const char *const *server_domains;
extern size_t server_domain_count;
extern ssize_t server_encode(void*, void*, unsigned char**, const unsigned char*, size_t,
    size_t*, struct sockaddr_storage*, struct sockaddr_storage*);
extern ssize_t server_decode(void*, void*, unsigned char**, const unsigned char*, size_t,
    struct sockaddr_storage*, struct sockaddr_storage*);

static int server_callback(picoquic_cnx_t *cnx, uint64_t id, uint8_t *bytes, size_t length,
    picoquic_call_back_event_t event, void *ctx, void *stream) {
    ss_conn *c = ctx;
    ss_conn *head = picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx));
    if (c == head) {
        c = calloc(1, sizeof(*c));
        if (!c) return -1;
        c->cnx = cnx; c->target = head->target;
        c->next = head->next; head->next = c;
        picoquic_set_callback(cnx, server_callback, c);
    }
    if (event == picoquic_callback_close || event == picoquic_callback_application_close ||
        event == picoquic_callback_stateless_reset) {
        ss_conn_clear(c);
        ss_conn **p = &head->next;
        while (*p && *p != c) p = &(*p)->next;
        if (*p) *p = c->next;
        picoquic_set_callback(cnx, NULL, NULL);
        free(c);
        return 0;
    }
    if (!stream && (event == picoquic_callback_stream_data || event == picoquic_callback_stream_fin)) {
        int fd = socket(c->target.ss_family, SOCK_STREAM, 0);
        ss_stream *s = NULL;
        if (fd >= 0 && !ss_nonblock(fd)) s = ss_stream_create(c, fd, id);
        if (!s) {
            if (fd >= 0) close(fd);
            picoquic_reset_stream(cnx, id, SLIPSTREAM_INTERNAL_ERROR);
            picoquic_stop_sending(cnx, id, SLIPSTREAM_INTERNAL_ERROR);
            return 0;
        }
        if (picoquic_set_app_stream_ctx(cnx, id, s)) { ss_stream_abort(c, s); return 0; }
        socklen_t len = c->target.ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        int ret = connect(fd, (struct sockaddr *)&c->target, len);
        if (ret && errno != EINPROGRESS) { ss_stream_abort(c, s); return 0; }
        s->connected = ret == 0;
        stream = s;
    }
    return ss_stream_event(c, id, bytes, length, event, stream);
}

static int server_loop_callback(picoquic_quic_t *quic, picoquic_packet_loop_cb_enum mode,
    void *ctx, void *arg) {
    (void)quic; (void)arg;
    ss_loop *loop = ctx;
    if (mode != picoquic_packet_loop_before_select) return 0;
    if (should_shutdown) return -1;
    uint64_t now = picoquic_current_time();
    for (picoquic_cnx_t *cnx = picoquic_get_first_cnx(quic), *next; cnx; cnx = next) {
        next = picoquic_get_next_cnx(cnx);
        uint64_t idle = cnx->idle_timeout ? cnx->idle_timeout : cnx->local_parameters.max_idle_timeout * 1000;
        if (cnx->cnx_state == picoquic_state_disconnected ||
            (idle && now - cnx->latest_receive_time >= idle))
            picoquic_delete_cnx(cnx);
    }
    for (ss_conn *c = loop->head.next; c; c = c->next) ss_conn_service(c, now);
    return 0;
}

int picoquic_slipstream_server(int port, bool ipv6, const char *cert, const char *key,
    struct sockaddr_storage *target, const char *const *domains, size_t domain_count,
    size_t max_connections, size_t idle_timeout) {
    if (!domain_count || max_connections == 0 || max_connections > 32 || !idle_timeout) return 2;
    server_domains = domains; server_domain_count = domain_count;
    ss_loop loop = {0}; loop.listener = -1; loop.head.target = *target;
    picoquic_quic_config_t config;
    picoquic_config_init(&config);
    config.nb_connections = (uint32_t)max_connections;
    config.server_cert_file = cert; config.server_key_file = key;
    config.server_port = port;
    config.mtu_max = config.initial_send_mtu_ipv4 = config.initial_send_mtu_ipv6 = 900;
    config.multipath_option = 1; config.do_preemptive_repeat = 1;
    config.disable_port_blocking = 1; config.enable_sslkeylog = 0; config.alpn = SLIPSTREAM_ALPN;
    picoquic_quic_t *quic = picoquic_create_and_configure(&config, server_callback, &loop.head,
        picoquic_current_time(), NULL);
    if (!quic) return 2;
    picoquic_set_cookie_mode(quic, 0);
    picoquic_set_default_priority(quic, 2);
    picoquic_set_default_idle_timeout(quic, idle_timeout * 1000);
    quic->default_tp.initial_max_stream_data_bidi_local = SS_RECEIVE_WINDOW;
    quic->default_tp.initial_max_stream_data_bidi_remote = SS_RECEIVE_WINDOW;
    picoquic_set_default_congestion_algorithm(quic, slipstream_server_cc_algorithm);
    picoquic_packet_loop_param_t param = {0};
    param.local_af = ipv6 ? AF_INET6 : AF_INET; param.local_port = port;
    param.do_not_use_gso = 1; param.decode = server_decode; param.encode = server_encode;
    picoquic_network_thread_ctx_t thread = {0};
    thread.quic = quic; thread.param = &param;
    thread.loop_callback = server_loop_callback; thread.loop_callback_ctx = &loop;
    signal(SIGTERM, server_sighandler); signal(SIGINT, server_sighandler);
    slipstream_packet_loop(&thread);
    /* Keep the default context alive until picoquic destruction callbacks finish. */
    picoquic_free(quic);
    return should_shutdown ? 0 : thread.return_code;
}
