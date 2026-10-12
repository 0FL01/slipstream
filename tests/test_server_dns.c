#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "slipstream_slot.h"

extern const char *const *server_domains;
extern size_t server_domain_count;
extern ssize_t server_decode(void *, void *, unsigned char **, const unsigned char *, size_t,
    struct sockaddr_storage *, struct sockaddr_storage *);
extern ssize_t server_encode(void *, void *, unsigned char **, const unsigned char *, size_t,
    size_t *, struct sockaddr_storage *, struct sockaddr_storage *);

static unsigned failures;
static int next_id = 0xab00;
static const unsigned char reply[] = {0, 0xff, 0x80, 1};

/* Exercise the production codec on SPCDNS wire bytes, including slot ownership.
 * Only a positive TXT decode may return bytes for QUIC submission. */
static dns_query_t *exchange(const char *name, dns_type_t type, dns_class_t class,
    const char *payload, bool prepared, bool opt, dns_decoded_t *response_buffer) {
    /* SPCDNS takes absolute presentation names; both input spellings have the
     * same terminal root label on the wire. Configured domains also vary dots. */
    char absolute[256];
    assert(strlen(name) + 2 <= sizeof(absolute));
    snprintf(absolute, sizeof(absolute), "%s%s", name, name[strlen(name) - 1] == '.' ? "" : ".");
    dns_question_t question = {.name = absolute, .type = type, .class = class};
    dns_answer_t edns = {0};
    edns.opt.name = "."; edns.opt.type = RR_OPT; edns.opt.udp_payload = 576;
    edns.opt.fdo = true;
    dns_query_t query = {0};
    query.id = next_id++; query.query = true; query.rd = !opt; query.cd = opt;
    query.qdcount = 1; query.questions = &question;
    query.arcount = opt ? 1 : 0; query.additional = &edns;
    dns_packet_t packet[DNS_BUFFER_UDP];
    size_t packet_size = sizeof(packet);
    assert(dns_encode(packet, &packet_size, &query) == RCODE_OKAY);

    slot_t slot = {0};
    struct sockaddr_storage peer = {0}, local = {0};
    struct sockaddr_in *p = (struct sockaddr_in *)&peer;
    p->sin_family = AF_INET; p->sin_addr.s_addr = htonl(INADDR_LOOPBACK); p->sin_port = htons(12345);
    struct sockaddr_in *l = (struct sockaddr_in *)&local;
    l->sin_family = AF_INET; l->sin_addr.s_addr = htonl(INADDR_LOOPBACK); l->sin_port = htons(53);
    const struct sockaddr_storage original_peer = peer, original_local = local;
    unsigned char *decoded = NULL;
    ssize_t decoded_size = server_decode(&slot, NULL, &decoded,
        (const unsigned char *)packet, packet_size, &peer, &local);
    if (payload) {
        assert(decoded_size == (ssize_t)strlen(payload));
        assert(decoded && !memcmp(decoded, payload, strlen(payload)));
        assert(slot.error == RCODE_OKAY);
    } else {
        assert(decoded_size == 0 && decoded == NULL);
    }
    free(decoded);
    assert(slot.cnx == NULL);
    assert(!memcmp(&slot.peer_addr, &original_peer, sizeof(peer)));
    assert(!memcmp(&slot.local_addr, &original_local, sizeof(local)));

    unsigned char *encoded = NULL;
    size_t segment = prepared ? sizeof(reply) : 0;
    ssize_t encoded_size = server_encode(&slot, NULL, &encoded,
        reply, segment, &segment, &peer, &local);
    assert(encoded_size > 0 && encoded);
    assert(!memcmp(&peer, &original_peer, sizeof(peer)));
    assert(!memcmp(&local, &original_local, sizeof(local)));
    size_t response_size = 4096;
    assert(dns_decode(response_buffer, &response_size, (const dns_packet_t *)encoded,
        (size_t)encoded_size) == RCODE_OKAY);
    free(encoded);
    dns_query_t *response = (dns_query_t *)response_buffer;
    dns_query_t *request = (dns_query_t *)slot.dns_decoded;
    assert(response->id == query.id && !response->query && response->aa);
    assert(response->rd == query.rd && response->cd == query.cd);
    assert(!response->ra && !response->tc && response->qdcount == 1);
    assert(!strcmp(response->questions[0].name, request->questions[0].name));
    assert(response->questions[0].type == type && response->questions[0].class == class);
    assert(response->nscount == 0 && response->ancount == (prepared ? 1u : 0u));
    /* Preserve the existing unconditional OPT, including for requests without EDNS. */
    assert(response->arcount == 1 && response->additional[0].generic.type == RR_OPT);
    assert(!strcmp(response->additional[0].opt.name, "."));
    assert(response->additional[0].opt.udp_payload == 1232);
    assert(response->additional[0].opt.version == 0 && !response->additional[0].opt.fdo);
    assert(response->additional[0].opt.numopts == 0);
    if (prepared) {
        dns_txt_t *txt = &response->answers[0].txt;
        assert(txt->type == RR_TXT && txt->class == CLASS_IN && txt->ttl == 60);
        assert(!strcmp(txt->name, request->questions[0].name));
        assert(txt->len == sizeof(reply) && !memcmp(txt->text, reply, sizeof(reply)));
    }
    return response;
}

static void expect_code(const char *name, dns_type_t type, dns_class_t class,
    const char *payload, bool prepared, bool opt, dns_rcode_t expected) {
    dns_decoded_t buffer[DNS_DECODEBUF_4K];
    dns_query_t *response = exchange(name, type, class, payload, prepared, opt, buffer);
    if (response->rcode != expected) {
        fprintf(stderr, "%s type=%d class=%d: rcode=%d answers=%zu, expected rcode=%d\n",
            name, type, class, response->rcode, response->ancount, expected);
        failures++;
    }
}

/* A successfully decoded transport QName exists even without a prepared QUIC
 * reply. Its empty TXT answer must agree with A/NS existence for the same name. */
static void valid_empty(void) {
    const struct { const char *name, *payload; } names[] = {
        {"NBSWY3DP.t.test", "hello"},
        {"NBSW.Y3DP.T.TeSt.", "hello"},
        {"NBSWY3DP.NBSWY3DP.t.test", "hellohello"},
    };
    const dns_type_t types[] = {RR_A, RR_NS, RR_TXT};
    for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
        for (unsigned opt = 0; opt < 2; opt++) {
            for (size_t t = 0; t < sizeof(types) / sizeof(types[0]); t++)
                expect_code(names[n].name, types[t], CLASS_IN,
                    types[t] == RR_TXT ? names[n].payload : NULL, false, opt != 0, RCODE_OKAY);
            expect_code(names[n].name, RR_TXT, CLASS_IN,
                names[n].payload, true, opt != 0, RCODE_OKAY);
        }
    }
}

/* RFC 9156 section 3 (6c)/(6d): NODATA advances to the next label;
 * NXDOMAIN cuts the original descendant lookup without submitting its TXT. */
static void minimization(dns_type_t type, bool apex) {
    dns_decoded_t buffer[DNS_DECODEBUF_4K];
    dns_query_t *direct = exchange("NBSWY3DP.NBSWY3DP.t.test", RR_TXT, CLASS_IN,
        "hellohello", true, false, buffer);
    assert(direct->rcode == RCODE_OKAY && direct->ancount == 1);
    const char *ancestors[] = {"t.test", "NBSWY3DP.t.test"};
    unsigned txt_submissions = 0;
    dns_rcode_t result = RCODE_SERVER_FAILURE;
    for (size_t i = apex ? 0 : 1; i < sizeof(ancestors) / sizeof(ancestors[0]); i++) {
        dns_query_t *probe = exchange(ancestors[i], type, CLASS_IN, NULL, false, false, buffer);
        result = probe->rcode;
        if (result == RCODE_NAME_ERROR) {
            fprintf(stderr, "RFC9156 type=%d ancestor=%s: NXDOMAIN/answers=%zu cuts descendant TXT; direct TXT=NOERROR/1\n",
                type, ancestors[i], probe->ancount);
            break;
        }
        assert(result == RCODE_OKAY && probe->ancount == 0);
        if (i + 1 == sizeof(ancestors) / sizeof(ancestors[0])) {
            txt_submissions++;
            result = exchange("NBSWY3DP.NBSWY3DP.t.test", RR_TXT, CLASS_IN,
                "hellohello", true, false, buffer)->rcode;
        }
    }
    if (result != RCODE_OKAY || txt_submissions != 1) failures++;
}

static void malformed(void) {
    dns_question_t questions[] = {
        {.name = "ancestor.t.test.", .type = RR_NS, .class = CLASS_IN},
        {.name = "t.test.", .type = RR_A, .class = CLASS_IN},
    };
    dns_query_t query = {.id = 123, .query = true, .qdcount = 1, .questions = questions};
    dns_packet_t packet[DNS_BUFFER_UDP];
    size_t size = sizeof(packet);
    assert(dns_encode(packet, &size, &query) == RCODE_OKAY);
    const size_t truncated[] = {0, 1, 2, 11, 13, size - 1};
    for (size_t i = 0; i < sizeof(truncated) / sizeof(truncated[0]); i++) {
        slot_t slot = {0};
        struct sockaddr_storage peer = {0}, local = {0};
        unsigned char *decoded = NULL;
        assert(server_decode(&slot, NULL, &decoded, (const unsigned char *)packet,
            truncated[i], &peer, &local) == -1 && decoded == NULL);
    }
    /* Valid DNS envelopes that are not a single question query remain rejected. */
    for (unsigned variant = 0; variant < 3; variant++) {
        query.query = variant != 0; query.qdcount = variant == 0 ? 1 : (variant == 1 ? 0 : 2);
        size = sizeof(packet);
        assert(dns_encode(packet, &size, &query) == RCODE_OKAY);
        slot_t slot = {0};
        struct sockaddr_storage peer = {0}, local = {0};
        unsigned char *decoded = NULL;
        assert(server_decode(&slot, NULL, &decoded, (const unsigned char *)packet,
            size, &peer, &local) == -1 && decoded == NULL);
    }
}

int main(void) {
    const char *domains[] = {"T.Test.", "tt.test", "child.t.test"};
    server_domains = domains; server_domain_count = sizeof(domains) / sizeof(domains[0]);
    minimization(RR_NS, true);
    minimization(RR_NS, false);
    minimization(RR_A, true);
    minimization(RR_A, false);
    const dns_type_t types[] = {RR_NS, RR_A, RR_AAAA, RR_MX, RR_ANY};
    const char *names[] = {"t.test", "T.TeSt.", "ancestor.t.test", "deep.ancestor.T.TeSt.",
        "tt.test.", "ancestor.tt.test", "child.t.test"};
    const char *outside[] = {"test", "untrusted.test", "xt.test", "ancestor.xt.test",
        "t.test.invalid", "ancestor.t.test.invalid", "t.testx"};
    for (size_t t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
        for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
            for (unsigned opt = 0; opt < 2; opt++)
                expect_code(names[n], types[t], CLASS_IN, NULL, false, opt != 0, RCODE_OKAY);
            expect_code(names[n], types[t], CLASS_CH, NULL, false, false, RCODE_NAME_ERROR);
            expect_code(names[n], types[t], CLASS_HS, NULL, false, true, RCODE_NAME_ERROR);
        }
        for (size_t n = 0; n < sizeof(outside) / sizeof(outside[0]); n++)
            expect_code(outside[n], types[t], CLASS_IN, NULL, false, false, RCODE_NAME_ERROR);
    }
    expect_code("NBSWY3DP.t.test", RR_TXT, CLASS_IN, "hello", true, false, RCODE_OKAY);
    expect_code("NBSW.Y3DP.T.TeSt.", RR_TXT, CLASS_IN, "hello", true, true, RCODE_OKAY);
    expect_code("NBSWY3DP.tt.test", RR_TXT, CLASS_IN, "hello", false, true, RCODE_OKAY);
    valid_empty();
    expect_code("t.test", RR_TXT, CLASS_IN, NULL, false, false, RCODE_NAME_ERROR);
    expect_code("invalid!.t.test", RR_TXT, CLASS_IN, NULL, false, false, RCODE_NAME_ERROR);
    expect_code("A.t.test", RR_TXT, CLASS_IN, NULL, false, false, RCODE_NAME_ERROR);
    expect_code("NBSWY3DP.xt.test", RR_TXT, CLASS_IN, NULL, false, false, RCODE_NAME_ERROR);
    expect_code("NBSWY3DP.t.test.invalid", RR_TXT, CLASS_IN, NULL, false, true, RCODE_NAME_ERROR);
    const dns_class_t classes[] = {CLASS_CH, CLASS_HS};
    const char *invalid_txt[] = {"t.test", "invalid!.t.test", "A.t.test",
        "NBSWY3DP.xt.test", "NBSWY3DP.t.test.invalid"};
    for (size_t c = 0; c < sizeof(classes) / sizeof(classes[0]); c++) {
        /* Preserve the existing TXT decode's class handling; the non-IN
         * rejection above is specific to non-TXT questions. */
        expect_code("NBSWY3DP.t.test", RR_TXT, classes[c], "hello", true, false, RCODE_OKAY);
        for (size_t n = 0; n < sizeof(invalid_txt) / sizeof(invalid_txt[0]); n++)
            expect_code(invalid_txt[n], RR_TXT, classes[c], NULL, false, false, RCODE_NAME_ERROR);
    }
    malformed();
    return failures ? 1 : 0;
}
