#include "slipstream_runtime.h"
#include "slipstream.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <math.h>

int ss_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int ss_runtime_wait(ss_loop *loop, int udp_fd, int64_t delay_us) {
    /* Bounds match 32 transport connections and 64 streams per connection. */
    struct pollfd fds[2 + 32 * SS_STREAM_LIMIT];
    nfds_t n = 0;
    fds[n++] = (struct pollfd){udp_fd, POLLIN, 0};
    if (loop->listener >= 0 && loop->head.ready && loop->head.count < SS_STREAM_LIMIT)
        fds[n++] = (struct pollfd){loop->listener, POLLIN, 0};
    ss_conn *first = loop->head.client ? &loop->head : loop->head.next;
    for (ss_conn *c = first; c; c = c->next) {
        for (ss_stream *s = c->streams; s && n < sizeof(fds)/sizeof(fds[0]); s = s->next) {
            short events = s->local_fin ? 0 : POLLIN;
            picoquic_stream_head_t *qs = s->id == UINT64_MAX ? NULL : picoquic_find_stream(c->cnx, s->id);
            if (s->stage == 4 && qs && (qs->is_active || qs->send_queue)) events = 0;
            if (!s->connected || s->out_len > s->out_off) events |= POLLOUT;
            /* Don't spin on HUP once local EOF has been delivered. */
            if (events) fds[n++] = (struct pollfd){s->fd, events, 0};
            if ((!s->connected || s->stage < 4) && delay_us > 50000) delay_us = 50000;
        }
        if (c->client) {
            uint64_t now = picoquic_current_time();
            delay_us = (int64_t)ss_scheduler_timeout(c->phase, delay_us > 0 ? (uint64_t)delay_us : 0,
                now, c->last_activity, c->next_poll, !c->ready);
        }
    }
    int ms = delay_us <= 0 ? 0 : (int)((delay_us + 999) / 1000);
    return poll(fds, n, ms);
}

int ss_listen(const char *host, int port) {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (ss_nonblock(fd) || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) || listen(fd, 64)) {
        close(fd); return -1;
    }
    return fd;
}

ss_stream *ss_stream_create(ss_conn *c, int fd, uint64_t id) {
    if (c->count >= SS_STREAM_LIMIT) return NULL;
    ss_stream *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->fd = fd; s->id = id;
    s->connected = c->client;
    s->deadline = picoquic_current_time() + 5000000;
    s->stage = c->credentials ? 0 : 4;
    s->next = c->streams; c->streams = s; c->count++;
    picoquic_stream_head_t *qs = id == UINT64_MAX ? NULL : picoquic_find_stream(c->cnx, id);
    if (qs) qs->app_flow_control = 1;
    return s;
}

static void stream_free(ss_conn *c, ss_stream *s) {
    ss_stream **p = &c->streams;
    while (*p && *p != s) p = &(*p)->next;
    if (!*p) return;
    *p = s->next; c->count--;
    if (s->id != UINT64_MAX && c->cnx) picoquic_unlink_app_stream_ctx(c->cnx, s->id);
    close(s->fd); free(s->out); free(s);
}

void ss_stream_abort(ss_conn *c, ss_stream *s) {
    /* Defer freeing until the service pass: callbacks may be nested in picoquic. */
    if (s->failed) return;
    s->failed = true;
    if (s->id != UINT64_MAX) {
        picoquic_mark_active_stream(c->cnx, s->id, 0, NULL);
        picoquic_reset_stream(c->cnx, s->id, SLIPSTREAM_FILE_CANCEL_ERROR);
        picoquic_stop_sending(c->cnx, s->id, SLIPSTREAM_FILE_CANCEL_ERROR);
    }
}

void ss_conn_clear(ss_conn *c) {
    while (c->streams) stream_free(c, c->streams);
}

static int queue_output(ss_stream *s, const uint8_t *p, size_t n) {
    size_t pending = s->out_len - s->out_off;
    if (n > SS_BUFFER_LIMIT - pending) return -1;
    if (!n) return 0;
    if (s->out_off) memmove(s->out, s->out + s->out_off, pending);
    uint8_t *b = realloc(s->out, pending + n);
    if (!b) return -1;
    s->out = b; s->out_off = 0; s->out_len = pending + n;
    memcpy(b + pending, p, n);
    return 0;
}

static int flush_output(ss_conn *c, ss_stream *s) {
    size_t before = s->out_off;
    while (s->out_off < s->out_len) {
        ssize_t n = send(s->fd, s->out + s->out_off, s->out_len - s->out_off, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n <= 0) return -1;
        s->out_off += (size_t)n;
    }
    size_t pending = s->out_len - s->out_off;
    if (s->out_off > before && s->stage == 4 && s->id != UINT64_MAX && !s->remote_fin &&
        pending < SS_RECEIVE_WINDOW) {
        picoquic_stream_head_t *qs = picoquic_find_stream(c->cnx, s->id);
        if (qs && ss_receive_credit_target(qs->consumed_offset, pending, qs->maxdata_local))
            picoquic_open_flow_control(c->cnx, s->id, SS_RECEIVE_WINDOW - pending);
    }
    if (!pending) { free(s->out); s->out = NULL; s->out_len = s->out_off = 0; }
    if (!pending && s->remote_fin && !s->write_closed) {
        if (shutdown(s->fd, SHUT_WR) && errno != ENOTCONN) return -1;
        s->write_closed = true;
    }
    return 0;
}

static int setup_step(ss_conn *c, ss_stream *s) {
    /* Read only setup bytes. Coalesced application/UoT records stay ordered. */
    for (unsigned iterations = 0; iterations < 8 && s->stage < 4; iterations++) {
        uint8_t open[300], reply[10]; size_t olen, rlen;
        unsigned stage = s->stage;
        size_t before = s->setup_len;
        int r = ss_socks_step(s, c->credentials, open, &olen, reply, &rlen);
        if (queue_output(s, reply, rlen) || flush_output(c, s)) return -1;
        if (r < 0) return -1;
        if (r == 1) {
            s->id = picoquic_get_next_local_stream_id(c->cnx, 0);
            if (picoquic_set_app_stream_ctx(c->cnx, s->id, s) ||
                picoquic_add_to_stream_with_ctx(c->cnx, s->id, open, olen, 0, s)) return -1;
            picoquic_find_stream(c->cnx, s->id)->app_flow_control = 1;
            if (s->setup_len && picoquic_add_to_stream_with_ctx(c->cnx, s->id, s->setup, s->setup_len, 0, s)) return -1;
            s->setup_len = 0;
            c->last_activity = picoquic_current_time();
            return 0;
        }
        if (s->stage != stage || before != s->setup_len) continue;
        ssize_t n = recv(s->fd, s->setup + s->setup_len, sizeof(s->setup) - s->setup_len, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        if (n <= 0) return -1;
        s->setup_len += (size_t)n;
    }
    return 0;
}

void ss_conn_service(ss_conn *c, uint64_t now) {
    for (ss_stream *s = c->streams, *next; s; s = next) {
        next = s->next;
        if (!s->failed && (!s->connected || s->stage < 4) && now >= s->deadline)
            ss_stream_abort(c, s);
        if (!s->failed && !s->connected) {
            struct pollfd p = {s->fd, POLLOUT, 0};
            if (poll(&p, 1, 0) > 0) {
                int error = 0; socklen_t len = sizeof(error);
                if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &error, &len) || error)
                    ss_stream_abort(c, s);
                else s->connected = true;
            }
        }
        if (!s->failed && s->connected) {
            if (flush_output(c, s) || (s->stage < 4 && setup_step(c, s))) ss_stream_abort(c, s);
            if (!s->failed && s->stage == 4 && !s->local_fin) {
                if (s->id == UINT64_MAX) {
                    s->id = picoquic_get_next_local_stream_id(c->cnx, 0);
                    if (picoquic_set_app_stream_ctx(c->cnx, s->id, s)) ss_stream_abort(c, s);
                    else picoquic_find_stream(c->cnx, s->id)->app_flow_control = 1;
                }
                struct pollfd p = {s->fd, POLLIN, 0};
                if (!s->failed && poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR)))
                    picoquic_mark_active_stream(c->cnx, s->id, 1, s);
            }
        }
        if (s->failed || (s->local_fin && s->remote_fin && s->write_closed && !s->out_len))
            stream_free(c, s);
    }
}

int ss_stream_event(ss_conn *c, uint64_t id, uint8_t *bytes, size_t length,
                     picoquic_call_back_event_t event, ss_stream *s) {
    if (!s || s->failed) return 0;
    switch (event) {
    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin:
        if (queue_output(s, bytes, length)) { ss_stream_abort(c, s); return 0; }
        if (length) c->last_activity = picoquic_current_time();
        if (event == picoquic_callback_stream_fin) s->remote_fin = true;
        if (s->connected && flush_output(c, s)) ss_stream_abort(c, s);
        break;
    case picoquic_callback_prepare_to_send: {
        uint8_t buffer[16384];
        size_t cap = length < sizeof(buffer) ? length : sizeof(buffer);
        if (!cap) {
            picoquic_provide_stream_data_buffer(bytes, 0, 0, 1); break;
        }
        ssize_t n = recv(s->fd, buffer, cap, 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            picoquic_provide_stream_data_buffer(bytes, 0, 0, 0); break;
        }
        if (n < 0) { ss_stream_abort(c, s); break; }
        uint8_t *dest = picoquic_provide_stream_data_buffer(bytes, (size_t)n, n == 0, n > 0);
        if (n > 0) {
            if (!dest) { ss_stream_abort(c, s); break; }
            memcpy(dest, buffer, (size_t)n); c->last_activity = picoquic_current_time();
        } else s->local_fin = true;
        break;
    }
    case picoquic_callback_stop_sending:
    case picoquic_callback_stream_reset: ss_stream_abort(c, s); break;
    default: break;
    }
    (void)id;
    return 0;
}

size_t ss_conn_poll_budget(ss_conn *c, int path_id, uint64_t now) {
    if (!c->ready || path_id < 0 || path_id >= c->cnx->nb_paths || path_id >= 8) return 0;
    picoquic_path_t *p = c->cnx->path[path_id];
    if (!p->challenge_verified || !p->p_remote_cnxid || p->path_is_demoted) return 0;
    bool ready = picoquic_find_ready_stream(c->cnx) != NULL;
    bool blocked = false;
    for (ss_stream *s = c->streams; s; s = s->next) {
        picoquic_stream_head_t *qs = s->id == UINT64_MAX ? NULL : picoquic_find_stream(c->cnx, s->id);
        if (qs && (qs->is_active || qs->send_queue) &&
            (qs->sent_offset >= qs->maxdata_remote || c->cnx->data_sent >= c->cnx->maxdata_remote)) {
            blocked = true; break;
        }
    }
    ss_phase phase = ss_scheduler_phase(c->count, c->last_activity, now, ready, blocked);
    if (phase != c->phase) {
        if (phase == SS_WARM || (phase == SS_QUIET && c->phase != SS_WARM)) c->next_poll = now;
        if (phase == SS_EMPTY) c->next_poll = 0;
        c->phase = phase;
    }
    size_t interval = phase == SS_BUSY ? c->active_keepalive : c->idle_keepalive;
    if (interval) picoquic_enable_keep_alive(c->cnx, interval * 1000);
    else picoquic_disable_keep_alive(c->cnx);
    size_t mtu = p->send_mtu ? p->send_mtu : 1;
    double gain = (double)p->pacing.rate > (double)c->last_pacing_rate[path_id] * 1.05 ? 1.25 : 1.0;
    c->last_pacing_rate[path_id] = p->pacing.rate;
    double target = p->pacing.rate ? ceil((double)p->pacing.rate / (double)mtu * gain *
                    (double)(p->smoothed_rtt ? p->smoothed_rtt : 50000) / 1000000.0) :
                    ceil((double)p->cwin / (double)mtu);
    size_t inflight = (size_t)((p->bytes_in_transit + mtu - 1) / mtu);
    size_t wanted = target > 64 ? 64 : (size_t)target;
    /* Experimental outstanding-packet cap, in addition to byte occupancy.
     * Traps and legacy packets on other paths still consume scan work. */
    picoquic_packet_context_t *context = c->cnx->is_multipath_enabled ? &p->pkt_ctx :
        &c->cnx->pkt_ctx[picoquic_packet_context_application];
    picoquic_packet_t *pending = context->pending_first;
    size_t count = 0, scanned = 0;
    while (pending && count < wanted && scanned < 64) {
        if (!pending->is_ack_trap && pending->send_path == p) count++;
        pending = pending->packet_next;
        scanned++;
    }
    /* An unresolved tail at the work cap conservatively fills the target. */
    if (pending && count < wanted) count = wanted;
    if (count > inflight) inflight = count;
    size_t budget = ss_poll_budget(phase, wanted > inflight ? wanted - inflight : 0,
                                   now >= c->next_poll, ready, blocked);
    return budget > 16 ? 16 : budget;
}
