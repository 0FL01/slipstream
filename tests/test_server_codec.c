/* Exercise the server's DNS boundary without scheduling a QUIC packet. */
#include "../src/slipstream_server.c"
#include <assert.h>

static dns_query_t *decode_answer(unsigned char *response, size_t length,
    dns_decoded_t *buffer, size_t *size) {
    *size = DNS_DECODEBUF_4K;
    assert(dns_decode(buffer, size, (dns_packet_t *)response, length) == RCODE_OKAY);
    return (dns_query_t *)buffer;
}

/* Out-of-zone, apex, non-TXT and malformed-payload queries are answered
 * directly from the zone code without creating a QUIC response. */
static void direct_reply(const char *name, dns_type_t type, dns_rcode_t expected,
    size_t expect_ns, size_t expect_an, dns_type_t expect_type, bool spcdns_decode) {
    slot_t slot = {0};
    dns_question_t question = {0};
    question.name = (char *)name; question.type = type; question.class = CLASS_IN;
    dns_query_t query = {0};
    query.id = 0x1234; query.query = true; query.rd = true; query.cd = true;
    query.qdcount = 1; query.questions = &question;
    unsigned char packet[MAX_UDP_PACKET_SIZE];
    size_t size = sizeof(packet);
    assert(dns_encode((dns_packet_t *)packet, &size, &query) == RCODE_OKAY);
    struct sockaddr_storage peer = {0}, local = {0};
    peer.ss_family = AF_INET; local.ss_family = AF_INET;
    unsigned char *decoded = NULL;
    ssize_t length = server_decode(&slot, NULL, &decoded, packet, size, &peer, &local);
    assert(length == 0 && decoded == NULL);
    assert(slot.direct && slot.direct_len);

    if (!spcdns_decode) {
        /* SPCDNS decode_rr_mx rejects a root exchange (null MX, RFC 7505);
         * validate the header and answer count on the wire instead. */
        assert((slot.direct[3] & 0x0f) == expected);
        assert((((size_t)slot.direct[6] << 8) | slot.direct[7]) == expect_an);
        free(slot.direct);
        return;
    }
    dns_decoded_t buffer[DNS_DECODEBUF_4K];
    dns_query_t *answer = decode_answer(slot.direct, slot.direct_len, buffer, &size);
    assert(!answer->query && answer->aa && answer->rd && answer->cd);
    assert(answer->id == query.id && answer->rcode == expected);
    assert(answer->qdcount == 1 && answer->ancount == expect_an);
    if (expect_an) assert(answer->answers[0].generic.type == expect_type);
    assert(answer->nscount == expect_ns);
    assert(answer->arcount == 0);
    assert(!strcmp(answer->questions[0].name, name));
    assert(answer->questions[0].type == type);
    free(slot.direct);
}

/* A valid TXT payload under the zone keeps the original tunnel path: the server
 * hands the decoded bytes to QUIC and encodes the TXT reply itself. */
static void tunnel_payload(const char *name) {
    slot_t slot = {0};
    dns_question_t question = {0};
    question.name = (char *)name; question.type = RR_TXT; question.class = CLASS_IN;
    dns_query_t query = {0};
    query.id = 0x1234; query.query = true; query.rd = true; query.cd = true;
    query.qdcount = 1; query.questions = &question;
    unsigned char packet[MAX_UDP_PACKET_SIZE];
    size_t size = sizeof(packet);
    assert(dns_encode((dns_packet_t *)packet, &size, &query) == RCODE_OKAY);
    struct sockaddr_storage peer = {0}, local = {0};
    peer.ss_family = AF_INET; local.ss_family = AF_INET;
    unsigned char *decoded = NULL;
    ssize_t length = server_decode(&slot, NULL, &decoded, packet, size, &peer, &local);
    assert(length == 1 && decoded);
    assert(!slot.direct);
    assert(slot.error == RCODE_OKAY);

    unsigned char *response = NULL;
    ssize_t answer_len = server_encode(&slot, NULL, &response, decoded, (size_t)length,
        NULL, &peer, &local);
    assert(answer_len > 0 && response);
    free(decoded);

    dns_decoded_t buffer[DNS_DECODEBUF_4K];
    dns_query_t *answer = decode_answer(response, (size_t)answer_len, buffer, &size);
    assert(!answer->query && answer->aa && answer->rd && answer->cd);
    assert(answer->id == query.id && answer->rcode == RCODE_OKAY);
    assert(answer->qdcount == 1 && answer->ancount == 1 && answer->nscount == 0);
    assert(answer->answers[0].txt.type == RR_TXT);
    assert(answer->answers[0].txt.ttl >= 30 && answer->answers[0].txt.ttl <= 302);
    /* The query had no EDNS OPT, so the reply must not invent one. */
    assert(answer->arcount == 0);
    free(response);
}

/* A reply carrying exactly max_quic bytes must fit the requester's EDNS size. */
static void reply_fits(size_t labels_len, int edns) {
    char name[300]; size_t at = 0;
    while (at + 1 < labels_len) {
        size_t run = labels_len - at - 1 > 57 ? 57 : labels_len - at - 1;
        memset(name + at, 'a', run); at += run; name[at++] = '.';
    }
    strcpy(name + at, "t.test.");
    slot_t slot = {0};
    dns_question_t question = {0};
    question.name = name; question.type = RR_TXT; question.class = CLASS_IN;
    dns_answer_t opt = {0};
    opt.opt.name = "."; opt.opt.type = RR_OPT; opt.opt.udp_payload = (size_t)edns;
    dns_query_t query = {0};
    query.id = 7; query.query = true; query.qdcount = 1; query.questions = &question;
    if (edns) { query.arcount = 1; query.additional = &opt; }
    unsigned char packet[MAX_UDP_PACKET_SIZE];
    size_t size = sizeof(packet);
    assert(dns_encode((dns_packet_t *)packet, &size, &query) == RCODE_OKAY);
    struct sockaddr_storage peer = {0}, local = {0};
    peer.ss_family = AF_INET; local.ss_family = AF_INET;
    unsigned char *decoded = NULL;
    (void)server_decode(&slot, NULL, &decoded, packet, size, &peer, &local);
    free(decoded);
    free(slot.direct);
    assert(slot.max_quic > 0);
    unsigned char payload[MAX_UDP_PACKET_SIZE];
    memset(payload, 0x5a, sizeof(payload));
    slot.error = RCODE_OKAY;
    unsigned char *response = NULL;
    ssize_t length = server_encode(&slot, NULL, &response, payload, slot.max_quic, NULL, &peer, &local);
    size_t limit = edns ? (size_t)edns : 512;
    if (limit > MAX_UDP_PACKET_SIZE - 12) limit = MAX_UDP_PACKET_SIZE - 12;
    assert(length > 0 && (size_t)length <= limit);
    /* The cap is tight: a few more bytes would not have fitted. */
    assert((size_t)length + 16 > limit);
    free(response);
}

int main(void) {
    const char *domains[] = {"t.test"};
    server_domains = domains; server_domain_count = 1;
    direct_reply("aa.untrusted.test.", RR_TXT, RCODE_REFUSED, 0, 0, RR_TXT, true);
    direct_reply("aa.t.test.", RR_A, RCODE_NAME_ERROR, 1, 0, RR_A, true);
    direct_reply("00.t.test.", RR_TXT, RCODE_NAME_ERROR, 1, 0, RR_TXT, true);
    direct_reply("t.test.", RR_TXT, RCODE_OKAY, 0, 1, RR_TXT, true);
    direct_reply("t.test.", RR_MX, RCODE_OKAY, 0, 1, RR_MX, false);
    direct_reply("_dmarc.t.test.", RR_TXT, RCODE_OKAY, 0, 1, RR_TXT, true);
    direct_reply("_dmarc.t.test.", RR_A, RCODE_OKAY, 1, 0, RR_A, true);
    tunnel_payload("aa.t.test.");
    reply_fits(240, 1232);
    reply_fits(64, 1232);
    reply_fits(240, 4096);
    reply_fits(64, 4096);
    reply_fits(64, 0);
    return 0;
}
