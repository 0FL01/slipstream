/* Malformed, truncated and mutated DNS queries must never abort server_decode. */
#include "../src/slipstream_server.c"
#include <assert.h>
#include <stdio.h>
static unsigned rng = 12345;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }
static void feed(const unsigned char *p, size_t n) {
    slot_t slot = {0}; struct sockaddr_storage peer = {0}, local = {0};
    peer.ss_family = AF_INET; local.ss_family = AF_INET;
    unsigned char *decoded = NULL;
    (void)server_decode(&slot, NULL, &decoded, p, n, &peer, &local);
    free(decoded);
    free(slot.direct);
}
int main(int argc, char **argv) {
    const char *domains[] = {"t.test"}; server_domains = domains; server_domain_count = 1;
    dns_question_t q = {0}; q.name = "abcdefgh.ijklmnop.t.test."; q.type = RR_TXT; q.class = CLASS_IN;
    dns_answer_t opt = {0}; opt.opt.name = "."; opt.opt.type = RR_OPT; opt.opt.udp_payload = 1232;
    dns_query_t query = {0}; query.id = 1; query.query = true; query.qdcount = 1; query.questions = &q;
    query.arcount = 1; query.additional = &opt;
    unsigned char pkt[MAX_UDP_PACKET_SIZE]; size_t size = sizeof(pkt);
    assert(dns_encode((dns_packet_t *)pkt, &size, &query) == RCODE_OKAY);
    (void)argc; (void)argv;
    for (size_t n = 0; n <= size; n++) feed(pkt, n);
    {
        unsigned char m[MAX_UDP_PACKET_SIZE];
        for (int i = 0; i < 200000; i++) {
            size_t n = size + (rnd() % 64); memcpy(m, pkt, size); for (size_t k = size; k < n; k++) m[k] = rnd();
            int flips = 1 + rnd() % 4; for (int f = 0; f < flips; f++) m[rnd() % n] = rnd();
            if (rnd() % 4 == 0) n = rnd() % n;
            feed(m, n);
        }
    }
    return 0;
}
