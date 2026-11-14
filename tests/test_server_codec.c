/* Exercise the server's DNS boundary without scheduling a QUIC packet. */
#include "../src/slipstream_server.c"
#include <assert.h>

static void empty_response(const char *name, dns_type_t type, dns_rcode_t expected) {
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
    assert(length == (expected == RCODE_OKAY ? 1 : 0));
    assert(slot.error == expected);
    free(decoded);

    unsigned char *response = NULL;
    length = server_encode(&slot, NULL, &response, NULL, 0, NULL, &peer, &local);
    assert(length > 0 && response);
    dns_decoded_t buffer[DNS_DECODEBUF_4K];
    size = sizeof(buffer);
    assert(dns_decode(buffer, &size, (dns_packet_t *)response, (size_t)length) == RCODE_OKAY);
    dns_query_t *answer = (dns_query_t *)buffer;
    assert(!answer->query && answer->aa && answer->rd && answer->cd);
    assert(answer->id == query.id && answer->rcode == expected);
    assert(answer->qdcount == 1 && answer->ancount == 0);
    assert(!strcmp(answer->questions[0].name, name));
    assert(answer->questions[0].type == type);
    assert(answer->arcount == 1 && answer->additional[0].opt.type == RR_OPT);
    assert(answer->additional[0].opt.udp_payload == 1232);
    assert(!memcmp(&peer, &slot.peer_addr, sizeof(peer)));
    assert(!memcmp(&local, &slot.local_addr, sizeof(local)));
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
    empty_response("aa.t.test.", RR_TXT, RCODE_OKAY);
    empty_response("aa.untrusted.test.", RR_TXT, RCODE_NAME_ERROR);
    empty_response("aa.t.test.", RR_A, RCODE_NAME_ERROR);
    empty_response("00.t.test.", RR_TXT, RCODE_NAME_ERROR);
    reply_fits(240, 1232);
    reply_fits(60, 1232);
    reply_fits(240, 4096);
    reply_fits(60, 4096);
    reply_fits(60, 0);
    return 0;
}
