#include "slipstream_runtime.h"
#include <picoquic_config.h>
#include <arpa/inet.h>
#include <assert.h>

int main(void) {
    uint64_t now = 1000000;
    picoquic_quic_config_t config;
    picoquic_config_init(&config);
    config.nb_connections = 1; config.alpn = "test"; config.enable_sslkeylog = 0;
    picoquic_quic_t *quic = picoquic_create_and_configure(&config, NULL, NULL, now, NULL);
    assert(quic);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(53);
    picoquic_cnx_t *cnx = picoquic_create_cnx(quic, picoquic_null_connection_id,
        picoquic_null_connection_id, (struct sockaddr *)&address, now, 0, "test", "test", 1);
    assert(cnx);
    uint8_t buffer[2048]; size_t length = 99;
    struct sockaddr_storage to = {0}, from = {0}; int index = 0;
    assert(picoquic_prepare_packet_ex(cnx, 9, now, buffer, sizeof(buffer), &length,
        &to, &from, &index, NULL) == 0 && length == 0);
    length = 99;
    assert(picoquic_prepare_packet_ex(cnx, -2, now, buffer, sizeof(buffer), &length,
        &to, &from, &index, NULL) == 0 && length == 0);

    assert(picoquic_create_path(cnx, now, NULL, (struct sockaddr *)&address, UINT64_MAX) == 1);
    assert(picoquic_create_path(cnx, now, NULL, (struct sockaddr *)&address, UINT64_MAX) == 2);
    uint64_t identity = cnx->path[2]->unique_path_id;
    cnx->path[1]->path_is_demoted = 1;
    cnx->path[1]->demotion_time = now - 1;
    cnx->path_demotion_needed = 1;
    /* Index 2 moves to 1 during abandoned-path deletion; its missing CID
     * must safely return no packet rather than reading the stale index. */
    length = 99;
    assert(picoquic_prepare_packet_ex(cnx, 2, now, buffer, sizeof(buffer), &length,
        &to, &from, &index, NULL) == 0 && length == 0);
    assert(cnx->nb_paths == 2 && cnx->path[1]->unique_path_id == identity);

    cnx->path[1]->path_is_demoted = 1;
    cnx->path[1]->demotion_time = now - 1; cnx->path_demotion_needed = 1;
    length = 99;
    assert(picoquic_prepare_packet_ex(cnx, 1, now, buffer, sizeof(buffer), &length,
        &to, &from, &index, NULL) == 0 && length == 0);
    assert(cnx->nb_paths == 1);
    picoquic_free(quic);
    return 0;
}
