#ifndef SLIPSTREAM_H
#define SLIPSTREAM_H
/* Header file for the picoquic sample project. 
 * It contains the definitions common to client and server */

#ifdef __cplusplus
extern "C" {
#endif

#define SLIPSTREAM_ALPN "picoquic_sample"
#define SLIPSTREAM_SNI "test.example.com"

#define SLIPSTREAM_NO_ERROR 0
#define SLIPSTREAM_INTERNAL_ERROR 0x101
#define SLIPSTREAM_FILE_CANCEL_ERROR 0x105

#define SLIPSTREAM_QLOG_DIR "./qlog";
#include <stdbool.h>
#include <stddef.h>
#include <sys/socket.h>

typedef struct st_address_t {
    struct sockaddr_storage server_address;
    bool added;
} address_t;

int picoquic_slipstream_client(int listen_port, struct st_address_t* server_addresses, size_t server_address_count, const char* domain_name,
                               const char* cc_algo_id, bool gso, size_t keep_alive_interval,
                               size_t idle_keep_alive_interval, const char* listen_host,
                               const char* cert, bool flow_relay_stdin);

int picoquic_slipstream_server(int server_port, bool listen_ipv6, const char* pem_cert, const char* pem_key,
                               struct sockaddr_storage* target_address, const char* const* domains,
                               size_t domain_count, size_t max_connections, size_t idle_timeout);

#ifdef __cplusplus
}
#endif

#endif
