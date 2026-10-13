#include "slipstream_sockloop.h"
#include "slipstream_runtime.h"
#include "slipstream_slot.h"
#include <picosocks.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Run the production loop for one batch, with only its I/O scripted here. */
static struct {
    const char *name;
    ss_loop app;
    picoquic_quic_t quic;
    picoquic_cnx_t cnx;
    picoquic_path_t path;
    picoquic_path_t *paths[1];
    bool poll, clear_request, allocate;
    size_t length;
    int prepare_result, encode_result, send_result;
    unsigned callbacks, next_calls, poll_calls, encode_calls, send_calls, frees;
    uint64_t elapsed;
    unsigned char *owned;
} f;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s: %s\n", f.name, #condition); \
        abort(); \
    } \
} while (0)

static const uint64_t batch_time = 10000000;
static uint64_t test_time(void) { return batch_time + f.elapsed; }

static int64_t test_wake_delay(picoquic_quic_t *quic, uint64_t now, int64_t maximum) {
    CHECK(quic == &f.quic && now == test_time() && maximum > 0);
    return 0;
}

static int test_wait(ss_loop *app, int fd, int64_t delay) {
    (void)app; (void)fd; (void)delay;
    CHECK(false); /* No real blocking or socket access in this fixture. */
    return -1;
}

static int test_select(picoquic_socket_ctx_t *s_ctx, int nb_sockets,
    struct sockaddr_storage *from, struct sockaddr_storage *to, int *if_index,
    unsigned char *ecn, uint8_t *buffer, int maximum, int64_t delay,
    int *wake, picoquic_network_thread_ctx_t *thread, int *rank) {
    (void)from; (void)to; (void)if_index; (void)ecn; (void)buffer; (void)rank;
    CHECK(s_ctx->fd == -1 && nb_sockets == 1 && maximum > 0 && delay == 0);
    CHECK(thread->loop_callback_ctx == &f.app);
    *wake = 0;
    return 0;
}

static int prepared(uint8_t *buffer, size_t maximum, size_t *length,
    struct sockaddr_storage *to, struct sockaddr_storage *from, int *if_index,
    size_t *segment) {
    CHECK(maximum >= f.length && segment == NULL);
    *length = f.length;
    memset(buffer, 0x5a, f.length);
    to->ss_family = from->ss_family = AF_INET;
    *if_index = 7;
    return f.prepare_result;
}

static int test_prepare_next(picoquic_quic_t *quic, uint64_t now,
    uint8_t *buffer, size_t maximum, size_t *length,
    struct sockaddr_storage *to, struct sockaddr_storage *from, int *if_index,
    picoquic_connection_id_t *cid, picoquic_cnx_t **cnx, size_t *segment) {
    (void)cid;
    CHECK(quic == &f.quic && now == test_time());
    CHECK(f.cnx.is_poll_requested == 0);
    *cnx = &f.cnx;
    if (++f.next_calls > 1 || f.poll) {
        if (f.poll) f.elapsed += 1000; /* Time spent preparing an empty data attempt. */
        *length = 0;
        return 0;
    }
    return prepared(buffer, maximum, length, to, from, if_index, segment);
}

static int test_prepare_poll(picoquic_cnx_t *cnx, int path_id, uint64_t now,
    uint8_t *buffer, size_t maximum, size_t *length,
    struct sockaddr_storage *to, struct sockaddr_storage *from, int *if_index,
    size_t *segment) {
    CHECK(f.poll && cnx == &f.cnx && path_id == 0 && now == test_time());
    CHECK(cnx->is_poll_requested == 1 && ++f.poll_calls == 1);
    if (f.clear_request) cnx->is_poll_requested = 0;
    return prepared(buffer, maximum, length, to, from, if_index, segment);
}

static size_t test_poll_budget(ss_conn *connection, int path_id, uint64_t now) {
    CHECK(connection == &f.app.head && path_id == 0 && now == batch_time);
    return f.poll ? 1 : 0;
}

static ssize_t test_encode(void *slot_ctx, void *app, unsigned char **encoded,
    const unsigned char *buffer, size_t length, size_t *segment,
    struct sockaddr_storage *to, struct sockaddr_storage *from) {
    slot_t *slot = slot_ctx;
    CHECK(app == &f.app && slot->cnx == &f.cnx);
    CHECK(f.cnx.is_poll_requested == 0);
    CHECK(length == f.length && length > 0 && *segment == length);
    CHECK(to->ss_family == AF_INET && from->ss_family == AF_INET);
    for (size_t i = 0; i < length; i++) CHECK(buffer[i] == 0x5a);
    CHECK(++f.encode_calls == 1 && f.owned == NULL);
    f.elapsed += 1000; /* Encoding/submission time must reach the next prepare. */
    if (f.allocate) {
        f.owned = malloc(16);
        CHECK(f.owned != NULL);
        memset(f.owned, 0xa5, 16);
    }
    *encoded = f.owned;
    *segment = 16;
    return f.encode_result;
}

static void test_free(void *allocation) {
    if (allocation) {
        CHECK(allocation == f.owned && ++f.frees == 1);
        f.owned = NULL;
    }
    free(allocation);
}

static int test_send(int fd, struct sockaddr *to, struct sockaddr *from, int if_index,
    const char *bytes, int length, int segment, int *error) {
    CHECK(fd == -1 && to->sa_family == AF_INET && from->sa_family == AF_INET);
    CHECK(if_index == (f.poll ? 7 : 3) && segment == 0);
    CHECK(bytes == (const char *)f.owned && length == 16);
    CHECK(f.cnx.is_poll_requested == 0 && ++f.send_calls == 1);
    for (int i = 0; i < length; i++) CHECK((unsigned char)bytes[i] == 0xa5);
    *error = 0;
    return f.send_result;
}

/* No production callback/API is added: substitutions exist in this TU only. */
#define picoquic_current_time test_time
#define picoquic_get_next_wake_delay test_wake_delay
#define ss_runtime_wait test_wait
#define picoquic_packet_loop_select test_select
#define picoquic_prepare_next_packet_ex test_prepare_next
#define picoquic_prepare_packet_ex test_prepare_poll
#define ss_conn_poll_budget test_poll_budget
#define picoquic_sendmsg test_send
#define free test_free
#include "../src/slipstream_sockloop.c"
#undef free

static int test_callback(picoquic_quic_t *quic, picoquic_packet_loop_cb_enum mode,
    void *app, void *argument) {
    (void)argument;
    CHECK(quic == &f.quic && app == &f.app && mode == picoquic_packet_loop_before_select);
    return ++f.callbacks == 1 ? 0 : -1;
}

static void run_case(const char *name, bool poll, bool clear_request, size_t length,
    int prepare_result, int encode_result, bool allocate, int send_result, ss_phase phase) {
    memset(&f, 0, sizeof(f));
    f.name = name;
    f.poll = poll; f.clear_request = clear_request; f.length = length;
    f.prepare_result = prepare_result; f.encode_result = encode_result;
    f.allocate = allocate; f.send_result = send_result;
    f.paths[0] = &f.path; f.path.unique_path_id = 19;
    f.cnx.path = f.paths; f.cnx.nb_paths = 1;
    f.app.head.cnx = &f.cnx; f.app.head.phase = phase;
    f.app.head.next_poll = test_time();
    picoquic_packet_loop_param_t param = {0};
    param.is_client = 1; param.do_not_use_gso = 1;
    param.socket_buffer_size = 2048; param.dest_if = 3; param.encode = test_encode;
    picoquic_network_thread_ctx_t thread = {0};
    thread.quic = &f.quic; thread.param = &param;
    thread.loop_callback = test_callback; thread.loop_callback_ctx = &f.app;
    picoquic_socket_ctx_t socket_ctx = {0};
    socket_ctx.fd = -1;
    int result = slipstream_packet_loop_(&thread, &socket_ctx);
    bool encoded = prepare_result >= 0 && length > 0;
    bool submitted = encoded && encode_result >= 16;
    bool failed = prepare_result < 0 || (encoded && encode_result < 16) ||
        (submitted && send_result <= 0);
    CHECK(result == (failed ? (submitted && send_result < 0 ? send_result : -1) : 0));
    CHECK(f.cnx.is_poll_requested == 0);
    CHECK(f.poll_calls == (poll ? 1U : 0U));
    CHECK(f.next_calls == (!poll && encoded && !failed ? 2U : 1U));
    CHECK(f.encode_calls == (encoded ? 1U : 0U));
    CHECK(f.send_calls == (submitted ? 1U : 0U));
    CHECK(f.frees == (encoded && allocate ? 1U : 0U) && f.owned == NULL);
    CHECK(f.callbacks == (failed ? 1U : 2U));
    uint64_t deadline = batch_time;
    if (poll && !failed && (phase == SS_WARM || phase == SS_QUIET))
        deadline = ss_sparse_poll_deadline(phase, batch_time, submitted);
    CHECK(f.app.head.next_poll == deadline);
}

int main(void) {
    /* This case fails on the old loop: positive data/ACK never reaches encode. */
    run_case("poll-stale-flag", true, false, 12, 0, 16, true, 16, SS_QUIET);
    run_case("poll-consumed-flag", true, true, 12, 0, 16, true, 16, SS_WARM);
    run_case("poll-empty", true, false, 0, 0, 16, true, 16, SS_QUIET);
    run_case("poll-prepare-error", true, false, 12, -1, 16, true, 16, SS_WARM);
    run_case("poll-encode-error", true, true, 12, 0, -1, true, 16, SS_QUIET);
    run_case("poll-encode-zero", true, true, 12, 0, 0, true, 16, SS_WARM);
    run_case("poll-encode-null", true, true, 12, 0, -1, false, 16, SS_QUIET);
    run_case("poll-encode-short", true, true, 12, 0, 15, true, 16, SS_QUIET);
    run_case("poll-send-zero", true, true, 12, 0, 16, true, 0, SS_QUIET);
    run_case("poll-send-error", true, true, 12, 0, 16, true, -7, SS_WARM);
    run_case("regular-positive", false, false, 12, 0, 16, true, 16, SS_BUSY);
    run_case("regular-empty", false, false, 0, 0, 16, true, 16, SS_BUSY);
    run_case("regular-prepare-error", false, false, 12, -1, 16, true, 16, SS_BUSY);
    run_case("regular-encode-error", false, false, 12, 0, -1, true, 16, SS_BUSY);
    run_case("regular-encode-zero", false, false, 12, 0, 0, true, 16, SS_BUSY);
    run_case("regular-encode-null", false, false, 12, 0, -1, false, 16, SS_BUSY);
    run_case("regular-encode-short", false, false, 12, 0, 15, true, 16, SS_BUSY);
    run_case("regular-send-zero", false, false, 12, 0, 16, true, 0, SS_BUSY);
    run_case("regular-send-error", false, false, 12, 0, 16, true, -7, SS_BUSY);
    return 0;
}
