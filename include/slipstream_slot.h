#ifndef SLIPSTREAM_SLOT
#define SLIPSTREAM_SLOT

#include "SPCDNS/src/dns.h"
#include "picoquic.h"

typedef struct st_slot_t {
    dns_decoded_t dns_decoded[DNS_DECODEBUF_4K];
    dns_rcode_t error;
    struct sockaddr_storage peer_addr;
    struct sockaddr_storage local_addr;
    picoquic_cnx_t* cnx;
    int path_id;
    uint64_t unique_path_id;
    bool is_poll_packet;
    size_t max_quic; /* per-query QUIC payload cap from EDNS size and question; 0 = none */
    uint8_t* direct;  /* locally generated DNS reply (zone/NXDOMAIN); sent without QUIC */
    size_t direct_len;
} slot_t;

#endif // SLIPSTREAM_SLOT
