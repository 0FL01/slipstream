/* Linked only into the synthetic diagnostic server. No production log sink.
 * Wrappers observe real calls; they neither probe nor change transport state. */
#include "slipstream_runtime.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t slots, empty, ack_slots, misc_slots, empty_control;
static uint64_t checks, denied, denied_control, credit_calls, credit_advances;
static uint64_t packet_ns_min = UINT64_MAX, packet_ns_max, bucket_max;
static uint64_t cwin_min = UINT64_MAX, cwin_max;

static int ack_pending(picoquic_cnx_t *cnx) {
    if (cnx->ack_ctx[picoquic_packet_context_application].act[0].ack_needed) return 1;
    for (int i = 0; i < cnx->nb_paths; i++)
        if (cnx->path[i]->ack_ctx.act[0].ack_needed) return 1;
    return 0;
}

static void report(void) {
    printf("{\"slots\":%" PRIu64 ",\"empty\":%" PRIu64
        ",\"ack_slots\":%" PRIu64 ",\"misc_slots\":%" PRIu64
        ",\"empty_control\":%" PRIu64 ",\"pacing_checks\":%" PRIu64
        ",\"pacing_denied\":%" PRIu64 ",\"pacing_denied_control\":%" PRIu64
        ",\"credit_calls\":%" PRIu64 ",\"credit_advances\":%" PRIu64
        ",\"packet_ns_min\":%" PRIu64 ",\"packet_ns_max\":%" PRIu64
        ",\"bucket_max\":%" PRIu64 ",\"cwin_min\":%" PRIu64
        ",\"cwin_max\":%" PRIu64 "}\n",
        slots, empty, ack_slots, misc_slots, empty_control, checks, denied,
        denied_control, credit_calls, credit_advances, packet_ns_min, packet_ns_max,
        bucket_max, cwin_min, cwin_max);
}

int __real_picoquic_is_sending_authorized_by_pacing(picoquic_cnx_t *, picoquic_path_t *, uint64_t, uint64_t *);
int __wrap_picoquic_is_sending_authorized_by_pacing(picoquic_cnx_t *cnx,
    picoquic_path_t *path, uint64_t now, uint64_t *next) {
    int ret = __real_picoquic_is_sending_authorized_by_pacing(cnx, path, now, next);
    if (!cnx->client_mode && cnx->cnx_state == picoquic_state_ready) {
        checks++;
        uint64_t ns = (uint64_t)path->pacing.packet_time_nanosec;
        if (ns < packet_ns_min) packet_ns_min = ns;
        if (ns > packet_ns_max) packet_ns_max = ns;
        if ((uint64_t)path->pacing.bucket_max > bucket_max) bucket_max = (uint64_t)path->pacing.bucket_max;
        if (path->cwin < cwin_min) cwin_min = path->cwin;
        if (path->cwin > cwin_max) cwin_max = path->cwin;
        if (!ret) {
            denied++;
            denied_control += ack_pending(cnx) || cnx->first_misc_frame || cnx->max_stream_data_needed;
        }
    }
    return ret;
}

int __real_picoquic_prepare_packet_ex(picoquic_cnx_t *, int, uint64_t, uint8_t *, size_t,
    size_t *, struct sockaddr_storage *, struct sockaddr_storage *, int *, size_t *);
int __wrap_picoquic_prepare_packet_ex(picoquic_cnx_t *cnx, int path, uint64_t now,
    uint8_t *buffer, size_t capacity, size_t *length, struct sockaddr_storage *peer,
    struct sockaddr_storage *local, int *index, size_t *segment) {
    static int registered;
    if (!registered) { atexit(report); registered = 1; }
    int ready = !cnx->client_mode && cnx->cnx_state == picoquic_state_ready;
    int ack = ready && ack_pending(cnx);
    int misc = ready && (cnx->first_misc_frame || cnx->max_stream_data_needed);
    int ret = __real_picoquic_prepare_packet_ex(cnx, path, now, buffer, capacity,
        length, peer, local, index, segment);
    if (ready) {
        slots++; ack_slots += ack; misc_slots += misc;
        if (!*length) { empty++; empty_control += ack || misc; }
    }
    return ret;
}

int __real_picoquic_open_flow_control(picoquic_cnx_t *, uint64_t, uint64_t);
int __wrap_picoquic_open_flow_control(picoquic_cnx_t *cnx, uint64_t id, uint64_t expected) {
    picoquic_stream_head_t *stream = picoquic_find_stream(cnx, id);
    uint64_t before = stream ? stream->maxdata_local : 0;
    int ret = __real_picoquic_open_flow_control(cnx, id, expected);
    if (!cnx->client_mode) {
        credit_calls++;
        credit_advances += stream && stream->maxdata_local > before;
    }
    return ret;
}
