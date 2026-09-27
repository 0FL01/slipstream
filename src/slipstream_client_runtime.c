#include "slipstream.h"
#include "slipstream_runtime.h"
#include "slipstream_sockloop.h"
#include <picoquic_config.h>
#include <picosocks.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

extern volatile sig_atomic_t should_shutdown;
extern void client_sighandler(int);
extern char *client_domain_name;
extern size_t client_domain_name_len;
extern ssize_t client_encode(void*, void*, unsigned char**, const unsigned char*, size_t,
    size_t*, struct sockaddr_storage*, struct sockaddr_storage*);
extern ssize_t client_decode(void*, void*, unsigned char**, const unsigned char*, size_t,
    struct sockaddr_storage*, struct sockaddr_storage*);

typedef struct {
    ss_loop loop;
    address_t *addresses;
    size_t address_count, attempt;
    uint64_t started;
    bool exhausted;
} client_runtime;

static int callback(picoquic_cnx_t *cnx, uint64_t id, uint8_t *bytes, size_t length,
    picoquic_call_back_event_t event, void *ctx, void *stream) {
    ss_conn *c = ctx;
    (void)cnx;
    switch (event) {
    case picoquic_callback_ready: c->ready = true; break;
    case picoquic_callback_close:
    case picoquic_callback_application_close:
    case picoquic_callback_stateless_reset: c->closed = true; break;
    default: return ss_stream_event(c, id, bytes, length, event, stream);
    }
    return 0;
}

static int connect_attempt(picoquic_quic_t *quic, client_runtime *r) {
    ss_conn *c = &r->loop.head;
    c->ready = c->closed = false;
    c->phase = SS_EMPTY;
    c->next_poll = 0;
    memset(c->last_pacing_rate, 0, sizeof(c->last_pacing_rate));
    r->started = picoquic_current_time();
    c->cnx = picoquic_create_cnx(quic, picoquic_null_connection_id, picoquic_null_connection_id,
        (struct sockaddr *)&r->addresses[r->attempt].server_address, r->started, 0,
        SLIPSTREAM_SNI, SLIPSTREAM_ALPN, 1);
    if (!c->cnx) return -1;
    picoquic_set_callback(c->cnx, callback, c);
    picoquic_enable_keep_alive(c->cnx, c->idle_keepalive * 1000);
    return picoquic_start_client_cnx(c->cnx);
}

static int loop_callback(picoquic_quic_t *quic, picoquic_packet_loop_cb_enum mode, void *ctx, void *arg) {
    client_runtime *r = ctx;
    ss_conn *c = &r->loop.head;
    (void)arg;
    if (mode != picoquic_packet_loop_before_select) return 0;
    if (should_shutdown) return -1;
    uint64_t now = picoquic_current_time();
    if (!c->ready && (c->closed || now - r->started >= 3000000)) {
        ss_conn_clear(c);
        picoquic_set_callback(c->cnx, NULL, NULL);
        picoquic_delete_cnx(c->cnx); c->cnx = NULL;
        if (++r->attempt >= r->address_count || connect_attempt(quic, r)) {
            r->exhausted = true; return -1;
        }
    } else if (c->closed) {
        /* Established transport loss is terminal; never silently replay flows. */
        r->exhausted = true; return -1;
    }
    if (!c->ready) return 0;
    for (size_t i = 0; i < r->address_count; i++) {
        if (i == r->attempt || r->addresses[i].added) continue;
        int id = -1;
        if (!picoquic_probe_new_path_ex(c->cnx, (struct sockaddr *)&r->addresses[i].server_address,
            (struct sockaddr *)&c->cnx->path[0]->local_addr, 0, now, 0, &id) && id >= 0)
            r->addresses[i].added = true;
    }
    for (unsigned accepted = 0; accepted < 16 && c->count < SS_STREAM_LIMIT; accepted++) {
        int fd = accept(r->loop.listener, NULL, NULL);
        if (fd < 0) break;
        if (ss_nonblock(fd) || !ss_stream_create(c, fd, UINT64_MAX)) close(fd);
        else c->last_activity = now;
    }
    ss_conn_service(c, now);
    /* Updating the phase also updates keepalive, including Empty transitions. */
    (void)ss_conn_poll_budget(c, 0, now);
    return 0;
}

int picoquic_slipstream_client(int port, address_t *addresses, size_t count, const char *domain,
    const char *cc, bool gso, size_t active_keepalive, size_t idle_keepalive,
    const char *host, const char *cert, bool flow_stdin) {
    if (!count || count > 8 || !domain || strlen(domain) > 180 || !cert || !*cert ||
        (flow_stdin && strcmp(host, "127.0.0.1"))) return 2;
    ss_credentials credentials;
    memset(&credentials, 0, sizeof(credentials));
    if (flow_stdin && ss_read_credentials(STDIN_FILENO, &credentials)) return 2;
    client_runtime r = {0};
    r.loop.listener = -1;
    r.loop.head.client = true;
    r.loop.head.credentials = flow_stdin ? &credentials : NULL;
    r.loop.head.active_keepalive = active_keepalive;
    r.loop.head.idle_keepalive = idle_keepalive;
    r.addresses = addresses; r.address_count = count;
    client_domain_name = strdup(domain); client_domain_name_len = strlen(domain);
    if (!client_domain_name) return 2;
    picoquic_quic_config_t config;
    picoquic_config_init(&config);
    int mtu = (int)((240.0 - (double)strlen(domain)) / 1.6);
    config.nb_connections = 1;
    config.mtu_max = config.initial_send_mtu_ipv4 = config.initial_send_mtu_ipv6 = mtu;
    config.cc_algo_id = cc; config.multipath_option = 1;
    config.do_preemptive_repeat = 1; config.disable_port_blocking = 1;
    config.enable_sslkeylog = 0; config.alpn = SLIPSTREAM_ALPN;
    picoquic_quic_t *quic = picoquic_create_and_configure(&config, callback, &r.loop.head,
        picoquic_current_time(), NULL);
    int ret = 2;
    if (!quic) goto done;
    if (ss_pin_certificate(quic, cert)) goto cleanup;
    picoquic_set_cookie_mode(quic, 0);
    picoquic_set_default_priority(quic, 2);
    picoquic_set_default_idle_timeout(quic, 30000);
    quic->default_tp.initial_max_stream_data_bidi_local = SS_RECEIVE_WINDOW;
    quic->default_tp.initial_max_stream_data_bidi_remote = SS_RECEIVE_WINDOW;
    r.loop.listener = ss_listen(host, port);
    if (r.loop.listener < 0 || connect_attempt(quic, &r)) goto cleanup;
    picoquic_packet_loop_param_t param = {0};
    param.local_af = addresses[0].server_address.ss_family;
    param.do_not_use_gso = !gso;
    param.is_client = 1; param.decode = client_decode; param.encode = client_encode;
    picoquic_network_thread_ctx_t thread = {0};
    thread.quic = quic; thread.param = &param;
    thread.loop_callback = loop_callback; thread.loop_callback_ctx = &r;
    signal(SIGTERM, client_sighandler); signal(SIGINT, client_sighandler);
    slipstream_packet_loop(&thread);
    ret = r.exhausted ? 1 : should_shutdown ? 0 : thread.return_code;
cleanup:
    ss_conn_clear(&r.loop.head);
    if (r.loop.head.cnx) picoquic_set_callback(r.loop.head.cnx, NULL, NULL);
    picoquic_free(quic);
    if (r.loop.listener >= 0) close(r.loop.listener);
done:
    free(client_domain_name); client_domain_name = NULL;
    memset(&credentials, 0, sizeof(credentials));
    return ret;
}
