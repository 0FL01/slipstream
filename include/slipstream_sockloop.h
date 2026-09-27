#ifndef SLIPSTREAM_SOCKLOOP_H
#define SLIPSTREAM_SOCKLOOP_H

#include "picoquic_packet_loop.h"

void* slipstream_packet_loop(picoquic_network_thread_ctx_t* thread_ctx);
void* slipstream_packet_loop_bound(picoquic_network_thread_ctx_t* thread_ctx,
                                  const struct sockaddr_in* listen_address);

#endif //SLIPSTREAM_SOCKLOOP_H
