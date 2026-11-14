#include <picoquic.h>
#include <picosocks.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "slipstream.h"
#include "slipstream_inline_dots.h"
#include "slipstream_slot.h"
#include "slipstream_sockloop.h"
#include "slipstream_utils.h"
#include "lua-resty-base-encoding-base32.h"

volatile sig_atomic_t should_shutdown;
void server_sighandler(int signum) { (void)signum; should_shutdown = 1; }
const char *const *server_domains;
size_t server_domain_count;

ssize_t server_encode(void *slot_ptr, void *ctx, unsigned char **dest, const unsigned char *src,
    size_t length, size_t *segment_len, struct sockaddr_storage *peer, struct sockaddr_storage *local) {
    (void)ctx; (void)segment_len;
    *dest = NULL;
    slot_t *slot = slot_ptr;
    dns_query_t *q = (dns_query_t *)slot->dns_decoded;
    dns_query_t response = {0};
    response.id = q->id; response.aa = true; response.rd = q->rd; response.cd = q->cd;
    response.rcode = slot->error;
    response.qdcount = 1; response.questions = q->questions;
    dns_txt_t txt = {0};
    if (length) {
        txt.name = q->questions[0].name; txt.type = RR_TXT; txt.class = CLASS_IN;
        txt.ttl = 60; txt.text = (char *)src; txt.len = length;
        response.ancount = 1; response.answers = (dns_answer_t *)&txt;
    }
    dns_answer_t edns = {0};
    edns.opt.name = "."; edns.opt.type = RR_OPT; edns.opt.udp_payload = 1232;
    response.arcount = 1; response.additional = &edns;
    size_t size = MAX_UDP_PACKET_SIZE;
    uint8_t *packet = malloc(size);
    if (!packet) return -1;
    if (dns_encode((dns_packet_t *)packet, &size, &response) != RCODE_OKAY) { free(packet); return -1; }
    *dest = packet; *peer = slot->peer_addr; *local = slot->local_addr;
    return (ssize_t)size;
}

ssize_t server_decode(void *slot_ptr, void *ctx, unsigned char **dest, const unsigned char *src,
    size_t length, struct sockaddr_storage *peer, struct sockaddr_storage *local) {
    (void)ctx;
    *dest = NULL;
    slot_t *slot = slot_ptr;
    slot->peer_addr = *peer; slot->local_addr = *local;
    sockaddr_dummy(peer);
    size_t size = sizeof(slot->dns_decoded);
    if (dns_decode(slot->dns_decoded, &size, (const dns_packet_t *)src, length) != RCODE_OKAY) return -1;
    dns_query_t *q = (dns_query_t *)slot->dns_decoded;
    if (!q->query || q->qdcount != 1) return -1;
    slot->error = RCODE_NAME_ERROR;
    if (q->questions[0].type != RR_TXT) return 0;
    const char *name = q->questions[0].name;
    size_t n = strlen(name);
    if (n && name[n-1] == '.') n--;
    size_t data_len = 0;
    for (size_t i = 0; i < server_domain_count; i++) {
        size_t d = strlen(server_domains[i]);
        if (d && server_domains[i][d-1] == '.') d--;
        if (n > d + 1 && name[n-d-1] == '.' && !strncasecmp(name + n-d, server_domains[i], d)) {
            data_len = n - d - 1; break;
        }
    }
    if (!data_len || data_len > 253) return 0;
    char data[256]; memcpy(data, name, data_len); data[data_len] = 0;
    size_t encoded = slipstream_inline_undotify(data, data_len);
    char *decoded = malloc(encoded);
    if (!decoded) return -1;
    size_t decoded_len = b32_decode(decoded, data, encoded, false);
    if (decoded_len == (size_t)-1) { free(decoded); return 0; }
    slot->error = RCODE_OKAY; *dest = (unsigned char *)decoded;
    return (ssize_t)decoded_len;
}
