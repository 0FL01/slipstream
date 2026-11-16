#include <picoquic.h>
#include <picosocks.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
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

/* Keyed DNS-layer obfuscation. QUIC remains the security boundary; this only
 * keeps the QUIC header out of QNAME/TXT bytes and gives probes a normal
 * NXDOMAIN. Enabled only when a 16-byte FlowRelay token file is supplied. */
#define DNS_CRYPTO_MAGIC0 0x53
#define DNS_CRYPTO_MAGIC1 0x4c
#define DNS_CRYPTO_NONCE 4
#define DNS_CRYPTO_OVERHEAD (DNS_CRYPTO_NONCE + 2)
#define DNS_CRYPTO_LABEL "slipstream-dns-v1"

static bool dns_crypto_enabled;
static uint8_t dns_crypto_key[32];
static uint32_t dns_crypto_fallback_nonce;

static int dns_crypto_stream(uint8_t direction, const uint8_t *nonce,
    unsigned char *out, size_t length) {
    uint8_t prefix[5] = { direction, nonce[0], nonce[1], nonce[2], nonce[3] };
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;
    int ok = EVP_DigestInit_ex(ctx, EVP_shake128(), NULL) == 1
        && EVP_DigestUpdate(ctx, dns_crypto_key, sizeof(dns_crypto_key)) == 1
        && EVP_DigestUpdate(ctx, prefix, sizeof(prefix)) == 1
        && EVP_DigestFinalXOF(ctx, out, length) == 1;
    EVP_MD_CTX_free(ctx);
    return ok ? 0 : -1;
}

int server_dns_crypto_init(const char *token_file) {
    if (!token_file) return 0;
    FILE *file = fopen(token_file, "rb");
    if (!file) return -1;
    uint8_t token[17];
    size_t read = fread(token, 1, sizeof(token), file);
    int invalid = ferror(file) || read != 16;
    if (fclose(file)) invalid = 1;
    if (invalid) return -1;
    uint8_t digest[32];
    unsigned int digest_len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;
    int ok = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1
        && EVP_DigestUpdate(ctx, DNS_CRYPTO_LABEL, sizeof(DNS_CRYPTO_LABEL) - 1) == 1
        && EVP_DigestUpdate(ctx, token, 16) == 1
        && EVP_DigestFinal_ex(ctx, digest, &digest_len) == 1
        && digest_len == sizeof(digest);
    EVP_MD_CTX_free(ctx);
    if (!ok) return -1;
    memcpy(dns_crypto_key, digest, sizeof(dns_crypto_key));
    dns_crypto_fallback_nonce = (uint32_t)time(NULL);
    dns_crypto_enabled = true;
    return 0;
}

/* Returns malloc'd plaintext, or NULL when the frame is truncated or does not
 * carry the expected magic (an unauthenticated probe). */
static unsigned char *dns_crypto_decrypt(const unsigned char *src, size_t length,
    size_t *out_len) {
    if (length < DNS_CRYPTO_OVERHEAD) return NULL;
    const unsigned char *nonce = src;
    size_t body = length - DNS_CRYPTO_NONCE;
    unsigned char *out = malloc(body);
    if (!out) return NULL;
    if (dns_crypto_stream(1, nonce, out, body)) { free(out); return NULL; }
    for (size_t i = 0; i < body; i++) out[i] ^= src[DNS_CRYPTO_NONCE + i];
    if (out[0] != DNS_CRYPTO_MAGIC0 || out[1] != DNS_CRYPTO_MAGIC1) {
        free(out);
        return NULL;
    }
    memmove(out, out + 2, body - 2);
    *out_len = body - 2;
    return out;
}

static unsigned char *dns_crypto_encrypt(const unsigned char *src, size_t length,
    size_t *out_len) {
    size_t total = length + DNS_CRYPTO_OVERHEAD;
    unsigned char *out = malloc(total);
    if (!out) return NULL;
    if (RAND_bytes(out, DNS_CRYPTO_NONCE) != 1) {
        uint32_t fallback = ++dns_crypto_fallback_nonce;
        out[0] = (unsigned char)(fallback >> 24); out[1] = (unsigned char)(fallback >> 16);
        out[2] = (unsigned char)(fallback >> 8); out[3] = (unsigned char)fallback;
    }
    if (dns_crypto_stream(2, out, out + DNS_CRYPTO_NONCE, length + 2)) {
        free(out);
        return NULL;
    }
    for (size_t i = 0; i < length; i++)
        out[DNS_CRYPTO_OVERHEAD + i] ^= src[i];
    out[DNS_CRYPTO_NONCE] ^= DNS_CRYPTO_MAGIC0;
    out[DNS_CRYPTO_NONCE + 1] ^= DNS_CRYPTO_MAGIC1;
    *out_len = total;
    return out;
}

/* Header-only error reply for queries the zone encoder cannot echo (unknown
 * QCLASS, OPT as a question type). Keeps mutated input from tripping the
 * codec's assertions. */
static ssize_t server_error_reply(slot_t *slot, const dns_query_t *q, dns_rcode_t rcode) {
    dns_query_t response = {0};
    response.id = q->id; response.aa = true; response.rd = q->rd; response.cd = q->cd;
    response.rcode = rcode; response.qdcount = 0;
    size_t size = MAX_UDP_PACKET_SIZE;
    uint8_t *packet = malloc(size);
    if (!packet) return -1;
    if (dns_encode((dns_packet_t *)packet, &size, &response) != RCODE_OKAY) {
        free(packet);
        return -1;
    }
    slot->direct = packet;
    slot->direct_len = size;
    return 0;
}

static size_t request_edns_payload(const dns_query_t *q, bool *present, bool *fdo) {
    size_t payload = 1232;
    *present = false; *fdo = false;
    for (size_t i = 0; i < q->arcount; i++) {
        if (q->additional[i].generic.type != RR_OPT) continue;
        size_t value = q->additional[i].opt.udp_payload;
        if (value < 512) value = 512;
        if (value > 1232) value = 1232;
        payload = value;
        *present = true;
        *fdo = q->additional[i].opt.fdo;
    }
    return payload;
}

/* Replies for the served zone and for names that are not tunnel payloads.
 * Keeping the zone conventional makes active probing uninteresting. */
static ssize_t server_zone_reply(slot_t *slot, const dns_query_t *q,
    const char *zone, bool zone_match, bool apex) {
    dns_query_t response = {0};
    response.id = q->id;
    response.aa = true;
    response.rd = q->rd; response.cd = q->cd;
    response.qdcount = 1; response.questions = q->questions;
    dns_answer_t answers[4] = {0};
    dns_answer_t authority[2] = {0};
    dns_answer_t additional[4] = {0};
    size_t ancount = 0, nscount = 0, addcount = 0;
    char zone_fqdn[260] = {0};
    char ns1[320] = {0}, ns2[320] = {0}, rname[320] = {0};

    if (zone_match) {
        snprintf(zone_fqdn, sizeof(zone_fqdn), "%s.", zone);
        snprintf(ns1, sizeof(ns1), "ns1.%s.", zone);
        snprintf(ns2, sizeof(ns2), "ns2.%s.", zone);
        snprintf(rname, sizeof(rname), "hostmaster.%s.", zone);
    }
    dns_soa_t soa = {0};
    soa.name = zone_fqdn; soa.type = RR_SOA; soa.class = CLASS_IN; soa.ttl = 300;
    soa.mname = ns1; soa.rname = rname;
    soa.serial = (uint32_t)(time(NULL) / 86400u);
    soa.refresh = 3600; soa.retry = 600; soa.expire = 86400; soa.minimum = 300;

    dns_ns_t nsrec1 = {0}, nsrec2 = {0};
    nsrec1.name = zone_fqdn; nsrec1.type = RR_NS; nsrec1.class = CLASS_IN; nsrec1.ttl = 3600; nsrec1.nsdname = ns1;
    nsrec2 = nsrec1; nsrec2.nsdname = ns2;

    const struct sockaddr_in *local4 = slot->local_addr.ss_family == AF_INET
        ? (const struct sockaddr_in *)&slot->local_addr : NULL;
    const struct sockaddr_in6 *local6 = slot->local_addr.ss_family == AF_INET6
        ? (const struct sockaddr_in6 *)&slot->local_addr : NULL;
    dns_a_t arec = {0};
    arec.name = q->questions[0].name; arec.type = RR_A; arec.class = CLASS_IN; arec.ttl = 300;
    if (local4) arec.address = local4->sin_addr.s_addr;
    dns_aaaa_t aaaarec = {0};
    aaaarec.name = q->questions[0].name; aaaarec.type = RR_AAAA; aaaarec.class = CLASS_IN; aaaarec.ttl = 300;
    if (local6) aaaarec.address = local6->sin6_addr;
    dns_a_t glue1 = {0}, glue2 = {0};
    glue1.name = ns1; glue1.type = RR_A; glue1.class = CLASS_IN; glue1.ttl = 300;
    glue1.address = local4 ? local4->sin_addr.s_addr : 0;
    glue2 = glue1; glue2.name = ns2;

    if (!zone_match) {
        response.rcode = RCODE_REFUSED;
    } else {
        dns_type_t qtype = q->questions[0].type;
        if (!apex) {
            response.rcode = RCODE_NAME_ERROR;
            authority[0].soa = soa; nscount = 1;
        } else if (qtype == RR_SOA) {
            response.rcode = RCODE_OKAY;
            answers[0].soa = soa; ancount = 1;
        } else if (qtype == RR_NS) {
            response.rcode = RCODE_OKAY;
            answers[0].ns = nsrec1; answers[1].ns = nsrec2; ancount = 2;
            if (local4) {
                additional[0].a = glue1; additional[1].a = glue2; addcount = 2;
            }
        } else if (qtype == RR_A && local4) {
            response.rcode = RCODE_OKAY;
            answers[0].a = arec; ancount = 1;
        } else if (qtype == RR_AAAA && local6) {
            response.rcode = RCODE_OKAY;
            answers[0].aaaa = aaaarec; ancount = 1;
        } else {
            response.rcode = RCODE_OKAY; /* apex exists: NODATA, not NXDOMAIN */
            authority[0].soa = soa; nscount = 1;
        }
    }

    bool edns_present = false, edns_fdo = false;
    size_t payload = request_edns_payload(q, &edns_present, &edns_fdo);
    dns_edns0opt_t opt = {0};
    if (edns_present) {
        opt.name = "."; opt.type = RR_OPT; opt.class = CLASS_UNKNOWN; opt.ttl = 0;
        opt.udp_payload = payload; opt.fdo = edns_fdo;
        additional[addcount++].opt = opt;
    }
    response.answers = ancount ? answers : NULL;
    response.nameservers = nscount ? authority : NULL;
    response.additional = addcount ? additional : NULL;
    response.ancount = ancount; response.nscount = nscount; response.arcount = addcount;

    size_t size = MAX_UDP_PACKET_SIZE;
    uint8_t *packet = malloc(size);
    if (!packet) return -1;
    if (dns_encode((dns_packet_t *)packet, &size, &response) != RCODE_OKAY) {
        free(packet);
        return -1;
    }
    slot->direct = packet;
    slot->direct_len = size;
    return 0;
}

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
    unsigned char *wrapped = NULL;
    const unsigned char *text = src;
    size_t text_len = length;
    if (length && dns_crypto_enabled) {
        wrapped = dns_crypto_encrypt(src, length, &text_len);
        if (!wrapped) return -1;
        text = wrapped;
    }
    if (length) {
        txt.name = q->questions[0].name; txt.type = RR_TXT; txt.class = CLASS_IN;
        txt.ttl = 30 + (picoquic_current_time() / 1000u) % 241u;
        txt.text = (char *)text; txt.len = text_len;
        response.ancount = 1; response.answers = (dns_answer_t *)&txt;
    }
    bool edns_present = false, edns_fdo = false;
    size_t payload = request_edns_payload(q, &edns_present, &edns_fdo);
    dns_answer_t edns = {0};
    if (edns_present) {
        edns.opt.name = "."; edns.opt.type = RR_OPT; edns.opt.class = CLASS_UNKNOWN;
        edns.opt.ttl = 0; edns.opt.udp_payload = payload; edns.opt.fdo = edns_fdo;
        response.arcount = 1; response.additional = &edns;
    }
    size_t size = MAX_UDP_PACKET_SIZE;
    uint8_t *packet = malloc(size);
    if (!packet) { free(wrapped); return -1; }
    if (dns_encode((dns_packet_t *)packet, &size, &response) != RCODE_OKAY) {
        free(packet); free(wrapped); return -1;
    }
    free(wrapped);
    *dest = packet; *peer = slot->peer_addr; *local = slot->local_addr;
    return (ssize_t)size;
}

/* Largest QUIC payload whose TXT reply fits the requester's EDNS size (512 without
 * EDNS) and the encoder buffer: header, echoed question, compressed answer RR, an
 * OPT RR only when the query carried one, and one length byte per 255-byte TXT
 * string. */
size_t server_quic_cap(const dns_query_t *q, size_t name_len) {
    size_t limit = 512;
    bool edns = false;
    for (size_t i = 0; i < q->arcount; i++)
        if (q->additional[i].generic.type == RR_OPT) {
            limit = q->additional[i].opt.udp_payload;
            edns = true;
        }
    if (limit < 512) limit = 512;
    if (limit > MAX_UDP_PACKET_SIZE - 12) limit = MAX_UDP_PACKET_SIZE - 12; /* SPCDNS encoder slack */
    size_t overhead = 12 + (name_len + 2 + 4) + (edns ? 12 : 0) + 11 + 8; /* 8: encoder margin */
    if (limit <= overhead + 2) return 1;
    size_t avail = limit - overhead;
    return avail - (avail + 255) / 256;
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
    if (q->questions[0].class != CLASS_IN)
        return server_error_reply(slot, q, RCODE_REFUSED);
    if (q->questions[0].type == RR_OPT)
        return server_error_reply(slot, q, RCODE_FORMAT_ERROR);
    const char *name = q->questions[0].name;
    size_t n = strlen(name);
    if (n && name[n-1] == '.') n--;

    char zone[256] = {0};
    size_t zone_len = 0;
    bool zone_match = false;
    for (size_t i = 0; i < server_domain_count; i++) {
        size_t d = strlen(server_domains[i]);
        if (d && server_domains[i][d-1] == '.') d--;
        if (n == d || (n > d + 1 && name[n-d-1] == '.' &&
            !strncasecmp(name + n - d, server_domains[i], d))) {
            if (d >= sizeof(zone)) return -1;
            memcpy(zone, server_domains[i], d); zone[d] = 0;
            zone_len = d; zone_match = true; break;
        }
    }
    bool apex = zone_match && n == zone_len;
    if (!zone_match || apex || q->questions[0].type != RR_TXT) {
        /* Out-of-zone, apex, or a non-tunnel query: answer like a normal zone. */
        return server_zone_reply(slot, q, zone, zone_match, apex);
    }

    slot->max_quic = server_quic_cap(q, n);
    if (dns_crypto_enabled)
        slot->max_quic = slot->max_quic > DNS_CRYPTO_OVERHEAD ? slot->max_quic - DNS_CRYPTO_OVERHEAD : 1;
    size_t data_len = n - zone_len - 1;
    if (!data_len || data_len > 253)
        return server_zone_reply(slot, q, zone, true, false);
    char data[256]; memcpy(data, name, data_len); data[data_len] = 0;
    size_t encoded = slipstream_inline_undotify(data, data_len);
    char *decoded = malloc(encoded);
    if (!decoded) return -1;
    size_t decoded_len = b32_decode(decoded, data, encoded, false);
    if (decoded_len == (size_t)-1) {
        free(decoded);
        return server_zone_reply(slot, q, zone, true, false);
    }
    if (dns_crypto_enabled) {
        size_t plain_len = 0;
        unsigned char *plain = dns_crypto_decrypt((unsigned char *)decoded, decoded_len, &plain_len);
        free(decoded);
        if (!plain) return server_zone_reply(slot, q, zone, true, false);
        decoded = (char *)plain;
        decoded_len = plain_len;
    }
    slot->error = RCODE_OKAY; *dest = (unsigned char *)decoded;
    return (ssize_t)decoded_len;
}
