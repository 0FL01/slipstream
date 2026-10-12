#include "slipstream_sockloop.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifndef __USE_XOPEN2K
#define __USE_XOPEN2K
#endif
#ifndef __USE_POSIX
#define __USE_POSIX
#endif
#include <netdb.h>

#ifndef __APPLE__
#ifdef __LINUX__
#include <linux/prctl.h>  /* Definition of PR_* constants */
#else
#endif
#endif

#include <pthread.h>

#ifndef SOCKET_TYPE
#define SOCKET_TYPE int
#endif
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif
#ifndef SOCKET_CLOSE
#define SOCKET_CLOSE(x) close(x)
#endif
#ifndef WSA_LAST_ERROR
#define WSA_LAST_ERROR(x) ((long)(x))
#endif

#include "picosocks.h"
#include "picoquic.h"
#include "picoquic_internal.h"
#include "picoquic_packet_loop.h"
#include "slipstream_slot.h"
#include "slipstream_runtime.h"

# if defined(UDP_SEGMENT)
static int udp_gso_available = 1;
#else
static int udp_gso_available = 0;
#endif


int slipstream_packet_loop_(picoquic_network_thread_ctx_t* thread_ctx, picoquic_socket_ctx_t* s_ctx) {
    picoquic_quic_t* quic = thread_ctx->quic;
    picoquic_packet_loop_param_t* param = thread_ctx->param;
    const picoquic_packet_loop_cb_fn loop_callback = thread_ctx->loop_callback;
    void* loop_callback_ctx = thread_ctx->loop_callback_ctx;
    slot_t slots[PICOQUIC_PACKET_LOOP_RECV_MAX] = {0};

    size_t send_buffer_size = param->socket_buffer_size;
    size_t send_msg_size = 0;
    size_t* send_msg_ptr = NULL;
    if (udp_gso_available && !param->do_not_use_gso) {
        send_buffer_size = 0xFFFF;
        send_msg_ptr = &send_msg_size;
    }
    if (send_buffer_size == 0) {
        send_buffer_size = 0xffff;
    }

    size_t buffer_size;
    if (param->is_client) {
        buffer_size = PICOQUIC_MAX_PACKET_SIZE;
    } else {
        buffer_size = MAX_DNS_QUERY_SIZE;
    }

    while (!thread_ctx->thread_should_close) {
        if (loop_callback) {
            int ret = loop_callback(quic, picoquic_packet_loop_before_select, loop_callback_ctx, s_ctx);
            if (ret < 0) {
                break;
            }
        }

        size_t nb_slots_written = 0;
        size_t nb_packet_received = 0;
        while (nb_slots_written < PICOQUIC_PACKET_LOOP_RECV_MAX) {
            int64_t delta_t = 0;

            if (!param->is_client && nb_slots_written == 0) {
                // Server mode: wait for a packet to arrive
                delta_t = picoquic_get_next_wake_delay(quic, picoquic_current_time(), 10000000);
            }

            if (param->is_client && nb_slots_written == 0) {
                const uint64_t current_time = picoquic_current_time();
                const int64_t delay_max = param->delay_max == 0 ? 10000000 : param->delay_max;
                delta_t = picoquic_get_next_wake_delay(quic, current_time, delay_max);
            }

            struct sockaddr_storage peer_addr;
            struct sockaddr_storage local_addr;
            int if_index_to = 0;
            uint8_t received_ecn;
            uint8_t buffer[buffer_size];
            int is_wake_up_event;
            int socket_rank = -1;
            /* Server transport output can only ride a DNS request. QUIC's due
             * send timer must not cause a spin while waiting for that request. */
            if (!param->is_client) delta_t = 1000000;
            /* Never wait or run lifecycle cleanup with pending response slots:
             * those slots borrow connection pointers until this batch drains. */
            if (delta_t > 0 && nb_slots_written == 0) {
                int waited = ss_runtime_wait(loop_callback_ctx, s_ctx->fd, delta_t);
                if (waited < 0 && errno != EINTR) return -1;
                if (loop_callback(quic, picoquic_packet_loop_before_select, loop_callback_ctx, s_ctx) < 0)
                    return 0;
            }
            int bytes_recv = picoquic_packet_loop_select(s_ctx, 1, &peer_addr, &local_addr, &if_index_to, &received_ecn,
                buffer, buffer_size, 0, &is_wake_up_event, thread_ctx, &socket_rank);
            if (bytes_recv < 0) {
                /* The interrupt error is expected if the loop is closing. */
                return thread_ctx->thread_should_close ? PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP : -1;
            }

            if (bytes_recv == 0 && is_wake_up_event) {
                const int ret = loop_callback(quic, picoquic_packet_loop_wake_up, loop_callback_ctx, NULL);
                if (ret < 0) {
                    return ret;
                }
            }
            if (bytes_recv == 0) {
                break;
            }

            slot_t* slot = &slots[nb_slots_written];
            assert(slot != NULL);
            memset(slot, 0, sizeof(slot_t));
            slot->path_id = -1;
            nb_slots_written++;

            unsigned char* decoded = NULL;
            bytes_recv = param->decode(slot, thread_ctx->loop_callback_ctx, &decoded,
                (const unsigned char*)buffer, bytes_recv, &peer_addr, &local_addr);
            if (bytes_recv < 0) {
                /* Malformed DNS must not terminate the listener. */
                free(decoded);
                nb_slots_written--;
                continue;
            }

            if (bytes_recv == 0) {
                continue;
            }

            memcpy(buffer, decoded, bytes_recv);
            free(decoded);

            /* Submit the packet to the server */
            uint8_t* received_buffer = buffer;
            uint64_t current_time = picoquic_current_time();
            picoquic_cnx_t* last_cnx = NULL;
            int last_path_id = -1;
            int ret = picoquic_incoming_packet_ex(quic, received_buffer,
                (size_t)bytes_recv, (struct sockaddr*)&peer_addr,
                (struct sockaddr*)&local_addr, if_index_to, received_ecn,
                &last_cnx, &last_path_id, current_time);
            if (ret < 0) {
                return ret;
            }
            if (last_cnx == NULL) {
                DBG_PRINTF("last_cnx null in recv", NULL);
                continue;
            }
            slot->cnx = last_cnx;
            slot->path_id = last_path_id;
            if (last_path_id >= 0 && last_path_id < last_cnx->nb_paths)
                slot->unique_path_id = last_cnx->path[last_path_id]->unique_path_id;
            nb_packet_received++;

            if (!param->is_client) {
                last_cnx->no_ack_delay = 1;
            }
        }

        const uint64_t loop_time = picoquic_current_time();
        size_t nb_packets_sent = 0;
        size_t nb_slots_read = 0;
        const size_t max_slots = param->is_client ? PICOQUIC_PACKET_LOOP_SEND_MAX : nb_slots_written;
        while (nb_slots_read < max_slots) {
            uint8_t send_buffer[send_buffer_size];
            slot_t* slot = &slots[nb_slots_read];
            assert(slot != NULL);
            nb_slots_read++;

            size_t send_length = 0;
            struct sockaddr_storage peer_addr = {0};
            struct sockaddr_storage local_addr = {0};
            int if_index = param->dest_if;
            if (slot->error == RCODE_OKAY) {
                picoquic_connection_id_t log_cid;
                int ret = 0;
                if (!param->is_client && slot->cnx) {
                    /* Resolve identity again: previous reply preparation can delete paths. */
                    slot->path_id = picoquic_get_path_id_from_unique(slot->cnx, slot->unique_path_id);
                    if (slot->path_id < 0) continue;
                    ret = picoquic_prepare_packet_ex(slot->cnx, slot->path_id, loop_time,
                        send_buffer, send_buffer_size, &send_length,
                        &peer_addr, &local_addr, &if_index, send_msg_ptr);
                }
                else if (param->is_client) {
                    ret = picoquic_prepare_next_packet_ex(quic, loop_time,
                        send_buffer, send_buffer_size, &send_length,
                        &peer_addr, &local_addr, &if_index, &log_cid, &slot->cnx,
                        send_msg_ptr);
                }
                if (ret < 0) {
                    return -1;
                }
                if (param->is_client && send_length == 0) {
                    break;
                }
            }

            if (param->is_client && peer_addr.ss_family == 0 && slot->peer_addr.ss_family == 0) {
                continue;
            }

            int sock_err = 0;
            int bytes_sent;
            unsigned char* encoded = NULL;
            size_t segment_len = send_msg_size == 0 ? send_length : send_msg_size;
            ssize_t encoded_len = param->encode(slot, loop_callback_ctx, &encoded,
                (const unsigned char*)send_buffer, send_length, &segment_len, &peer_addr, &local_addr);
            if (encoded_len <= 0) {
                DBG_PRINTF("Encoding fails, ret=%d\n", encoded_len);
                free(encoded);
                if (param->is_client) return -1;
                continue;
            }

            if (encoded_len < segment_len) {
                DBG_PRINTF("Encoded len shorter than original: %d < %d", encoded_len, segment_len);
                free(encoded);
                return -1;
            }

            if (send_msg_size > 0) {
                send_msg_size = segment_len; // new size after encoding
            }

            const SOCKET_TYPE send_socket = s_ctx->fd;
            bytes_sent = picoquic_sendmsg(send_socket,
                (struct sockaddr*)&peer_addr, (struct sockaddr*)&local_addr, param->dest_if,
                (const char*)encoded, (int)encoded_len, (int)send_msg_size, &sock_err);
            free(encoded);
            if (bytes_sent == 0) {
                DBG_PRINTF("BYTES_SENT == 0 %d\n", bytes_sent);
                return -1;
            }
            if (bytes_sent < 0) {
                return bytes_sent;
            }

            nb_packets_sent++;
        }

        if (!param->is_client) {
            continue;
        }

        size_t nb_polls_sent = 0;
        ss_loop *application = loop_callback_ctx;
        ss_conn *connection = &application->head;
        for (int poll_path = 0; poll_path < connection->cnx->nb_paths; poll_path++) {
          size_t poll_budget = ss_conn_poll_budget(connection, poll_path, loop_time);
          bool sparse_attempt = poll_budget > 0 &&
              (connection->phase == SS_WARM || connection->phase == SS_QUIET);
          size_t sent_before = nb_polls_sent;
          uint64_t poll_unique_id = connection->cnx->path[poll_path]->unique_path_id;
          while (poll_budget-- > 0) {
            uint8_t send_buffer[send_buffer_size];
            slot_t poll_slot = {0};
            slot_t* slot = &poll_slot;
            slot->cnx = connection->cnx;
            int path_id = picoquic_get_path_id_from_unique(slot->cnx, poll_unique_id);
            if (path_id < 0) break;

            slot->cnx->is_poll_requested = 1;

            size_t send_length = 0;
            struct sockaddr_storage peer_addr = {0};
            struct sockaddr_storage local_addr = {0};
            int if_index = param->dest_if;
            int ret = picoquic_prepare_packet_ex(slot->cnx, path_id, loop_time,
                send_buffer, send_buffer_size, &send_length,
                &peer_addr, &local_addr, &if_index, send_msg_ptr);
            /* This request belongs to this prepare call. A finalized data/ACK
             * packet still needs submission even if no POLL consumed it. */
            slot->cnx->is_poll_requested = 0;
            if (ret < 0) {
                return -1;
            }
            if (param->is_client && send_length == 0) {
                break;
            }

            int sock_err = 0;
            int bytes_sent;
            unsigned char* encoded = NULL;
            size_t segment_len = send_msg_size == 0 ? send_length : send_msg_size;
            ssize_t encoded_len = param->encode(slot, loop_callback_ctx, &encoded,
                (const unsigned char*)send_buffer, send_length, &segment_len, &peer_addr, &local_addr);
            if (encoded_len <= 0) {
                DBG_PRINTF("Encoding fails, ret=%d\n", encoded_len);
                free(encoded);
                return -1;
            }

            if (encoded_len < segment_len) {
                DBG_PRINTF("Encoded len shorter than original: %d < %d", encoded_len, segment_len);
                free(encoded);
                return -1;
            }

            if (send_msg_size > 0) {
                send_msg_size = segment_len; // new size after encoding
            }

            const SOCKET_TYPE send_socket = s_ctx->fd;
            bytes_sent = picoquic_sendmsg(send_socket,
                (struct sockaddr*)&peer_addr, (struct sockaddr*)&local_addr, if_index,
                (const char*)encoded, (int)encoded_len, (int)send_msg_size, &sock_err);
            free(encoded);
            if (bytes_sent == 0) {
                DBG_PRINTF("BYTES_SENT == 0 %d\n", bytes_sent);
                return -1;
            }
            if (bytes_sent < 0) {
                return bytes_sent;
            }

            nb_polls_sent++;
          }
          if (sparse_attempt)
              connection->next_poll = ss_sparse_poll_deadline(connection->phase,
                  loop_time, nb_polls_sent > sent_before);
        }

        // if (param->is_client) {
        //     DBG_PRINTF("[polls_sent:%d][sent:%d][received:%d]", nb_polls_sent, nb_packets_sent, nb_packet_received);
        // }
    }

    return thread_ctx->return_code;
}

void* slipstream_packet_loop(picoquic_network_thread_ctx_t* thread_ctx) {
    return slipstream_packet_loop_bound(thread_ctx, NULL);
}

/* Explicit server binding must happen on a fresh socket, before any wildcard bind. */
static int open_bound_socket(const picoquic_packet_loop_param_t* param,
    const struct sockaddr_in* address, picoquic_socket_ctx_t* ctx) {
    int recv_set = 0, send_set = 0;
    ctx->af = AF_INET;
    ctx->port = ntohs(address->sin_port);
    ctx->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ctx->fd == INVALID_SOCKET) return -1;
    if (param->local_af != AF_INET || !param->do_not_use_gso ||
        picoquic_socket_set_ecn_options(ctx->fd, AF_INET, &recv_set, &send_set) ||
        picoquic_socket_set_pkt_info(ctx->fd, AF_INET) ||
        bind(ctx->fd, (const struct sockaddr*)address, sizeof(*address)) ||
        picoquic_socket_set_pmtud_options(ctx->fd, AF_INET) ||
        (param->socket_buffer_size > 0 &&
         (setsockopt(ctx->fd, SOL_SOCKET, SO_SNDBUF, &param->socket_buffer_size, sizeof(int)) ||
          setsockopt(ctx->fd, SOL_SOCKET, SO_RCVBUF, &param->socket_buffer_size, sizeof(int))))) {
        picoquic_packet_loop_close_socket(ctx);
        return -1;
    }
    return 1;
}

void* slipstream_packet_loop_bound(picoquic_network_thread_ctx_t* thread_ctx,
    const struct sockaddr_in* listen_address) {
    const picoquic_packet_loop_param_t* param = thread_ctx->param;
    if (!param->do_not_use_gso && param->encode != NULL && !param->is_client) {
        DBG_FATAL_PRINTF("%s", "GSO disabled because encoding is enabled and server mode");
    }

    picoquic_socket_ctx_t s_ctx = {0};
    int opened = listen_address ? open_bound_socket(param, listen_address, &s_ctx) :
        picoquic_packet_loop_open_sockets(param->local_port,
        param->local_af, param->socket_buffer_size,
        0, param->do_not_use_gso, &s_ctx);
    if (opened <= 0) {
        thread_ctx->return_code = PICOQUIC_ERROR_UNEXPECTED_ERROR;
        return NULL;
    }

    thread_ctx->thread_is_ready = 1;
    thread_ctx->return_code = slipstream_packet_loop_(thread_ctx, &s_ctx);
    thread_ctx->thread_is_ready = 0;

    /* Close the sockets */
    picoquic_packet_loop_close_socket(&s_ctx);

    if (thread_ctx->is_threaded) {
        pthread_exit((void*)&thread_ctx->return_code);
    }
    return (NULL);
}
