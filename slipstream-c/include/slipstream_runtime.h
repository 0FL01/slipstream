#ifndef SLIPSTREAM_RUNTIME_H
#define SLIPSTREAM_RUNTIME_H
#include <picoquic_internal.h>
#include <picoquic_packet_loop.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* All fields are owned by the packet-loop thread. No worker retains pointers. */
#define SS_BUFFER_LIMIT (256 * 1024)
#define SS_RECEIVE_WINDOW (SS_BUFFER_LIMIT / 2)
#define SS_STREAM_LIMIT 64
typedef enum { SS_EMPTY, SS_BUSY, SS_WARM, SS_QUIET } ss_phase;
typedef struct {
    uint8_t bytes[80];
} ss_credentials;
typedef struct ss_stream {
    struct ss_stream *next;
    uint64_t id, deadline;
    int fd;
    bool connected, local_fin, remote_fin, write_closed, failed;
    unsigned stage;
    uint8_t setup[512];
    size_t setup_len;
    uint8_t *out;
    size_t out_len, out_off;
} ss_stream;
typedef struct ss_conn {
    struct ss_conn *next;
    picoquic_cnx_t *cnx;
    ss_stream *streams;
    size_t count;
    bool client, ready, closed;
    const ss_credentials *credentials;
    struct sockaddr_storage target;
    uint64_t last_activity, next_poll, last_pacing_rate[8];
    ss_phase phase;
    size_t active_keepalive, idle_keepalive;
} ss_conn;
typedef struct {
    ss_conn head;
    int listener;
} ss_loop;

int ss_read_credentials(int fd, ss_credentials *credentials);
/* Incremental parser: 0 needs input, 1 OPEN ready, -1 rejected.
 * reply must be queued before processing another parser stage. */
int ss_socks_step(ss_stream *s, const ss_credentials *credentials,
                  uint8_t *open, size_t *open_len, uint8_t *reply, size_t *reply_len);
ss_phase ss_scheduler_phase(size_t streams, uint64_t last, uint64_t now, bool ready, bool blocked);
size_t ss_poll_budget(ss_phase phase, size_t deficit, bool due, bool ready, bool blocked);
uint64_t ss_scheduler_timeout(ss_phase phase, uint64_t quic_delay, uint64_t now,
    uint64_t last_activity, uint64_t next_poll, bool fast_work);
uint64_t ss_sparse_poll_deadline(ss_phase phase, uint64_t now, bool sent);
uint64_t ss_receive_credit_target(uint64_t received, size_t pending, uint64_t advertised);
int ss_pin_certificate(picoquic_quic_t *quic, const char *path);
ss_stream *ss_stream_create(ss_conn *c, int fd, uint64_t id);
void ss_stream_abort(ss_conn *c, ss_stream *s);
void ss_conn_clear(ss_conn *c);
void ss_conn_service(ss_conn *c, uint64_t now);
int ss_stream_event(ss_conn *c, uint64_t id, uint8_t *bytes, size_t length,
                    picoquic_call_back_event_t event, ss_stream *s);
size_t ss_conn_poll_budget(ss_conn *c, int path_id, uint64_t now);
int ss_nonblock(int fd);
int ss_listen(const char *host, int port);
int ss_runtime_wait(ss_loop *loop, int udp_fd, int64_t delay_us);
#endif
