#include <picoquic.h>
#include <picosocks.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include "slipstream.h"
#include "slipstream_inline_dots.h"
#include "slipstream_sockloop.h"
#include "lua-resty-base-encoding-base32.h"
#include "SPCDNS/src/dns.h"

volatile sig_atomic_t should_shutdown;
void client_sighandler(int signum) { (void)signum; should_shutdown = 1; }
char *client_domain_name;
size_t client_domain_name_len;

static int encode_segment(dns_packet_t *packet, size_t *packet_len, const uint8_t *data, size_t length) {
    char name[256];
    if ((length * 8 + 4) / 5 + (length * 8 + 4) / 5 / 57 + client_domain_name_len + 3 >= sizeof(name)) return -1;
    size_t len = b32_encode(name, (const char *)data, length, true, false);
    len = slipstream_inline_dotify(name, sizeof(name), len);
    name[len++] = '.';
    memcpy(name + len, client_domain_name, client_domain_name_len); len += client_domain_name_len;
    name[len++] = '.'; name[len] = 0;
    dns_question_t question = {0};
    question.name = name; question.type = RR_TXT; question.class = CLASS_IN;
    dns_answer_t edns = {0};
    edns.opt.name = "."; edns.opt.type = RR_OPT; edns.opt.udp_payload = 1232;
    dns_query_t query = {0};
    query.id = (uint16_t)rand(); query.query = true; query.rd = true;
    query.qdcount = 1; query.questions = &question;
    query.arcount = 1; query.additional = &edns;
    return dns_encode(packet, packet_len, &query) == RCODE_OKAY ? 0 : -1;
}

ssize_t client_encode(void *slot, void *ctx, unsigned char **dest, const unsigned char *src,
    size_t length, size_t *segment_len, struct sockaddr_storage *peer, struct sockaddr_storage *local) {
    (void)slot; (void)ctx; (void)peer; (void)local;
    *dest = NULL;
    if (!length || !*segment_len) return 0;
    size_t segment = *segment_len;
    size_t count = (length + segment - 1) / segment;
    uint8_t *output = malloc(MAX_DNS_QUERY_SIZE * count);
    if (!output) return -1;
    size_t used = 0, first = 0;
    for (size_t offset = 0; offset < length; offset += segment) {
        size_t size = MAX_DNS_QUERY_SIZE;
        size_t n = length - offset < segment ? length - offset : segment;
        if (encode_segment((dns_packet_t *)(output + used), &size, src + offset, n) ||
            (first && (size > first || (offset + n < length && first != size)))) {
            free(output); return -1;
        }
        if (!first) first = size;
        used += size;
    }
    *dest = output; *segment_len = first;
    return (ssize_t)used;
}

ssize_t client_decode(void *slot, void *ctx, unsigned char **dest, const unsigned char *src,
    size_t length, struct sockaddr_storage *peer, struct sockaddr_storage *local) {
    (void)slot; (void)ctx; (void)peer; (void)local;
    *dest = NULL;
    dns_decoded_t decoded[DNS_DECODEBUF_4K] = {0};
    size_t size = sizeof(decoded);
    if (dns_decode(decoded, &size, (const dns_packet_t *)src, length) != RCODE_OKAY) return 0;
    dns_query_t *q = (dns_query_t *)decoded;
    if (q->query || q->rcode != RCODE_OKAY || q->ancount != 1) return 0;
    dns_txt_t *txt = (dns_txt_t *)q->answers;
    if (txt->type != RR_TXT || txt->len > PICOQUIC_MAX_PACKET_SIZE) return 0;
    *dest = malloc(txt->len);
    if (!*dest) return -1;
    memcpy(*dest, txt->text, txt->len);
    return (ssize_t)txt->len;
}
