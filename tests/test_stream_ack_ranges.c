#include <picoquic_config.h>
#include <picoquic_internal.h>
#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Exported production ACK handler, also reached by the packet frame walker.
 * No replacement range logic or production test hook. */
int picoquic_process_ack_of_stream_frame(picoquic_cnx_t *cnx, uint8_t *bytes,
    size_t bytes_max, size_t *consumed);

static const uint64_t max_offset = (UINT64_C(1) << 62) - 1;
static unsigned failures;
static const char *case_name;

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s line %d: %s\n", case_name, __LINE__, #condition); failures++; \
} } while (0)

static picoquic_cnx_t *connection(uint64_t *now) {
    picoquic_quic_config_t config;
    picoquic_config_init(&config);
    config.nb_connections = 1; config.alpn = "test"; config.enable_sslkeylog = 0;
    picoquic_quic_t *quic = picoquic_create_and_configure(&config, NULL, NULL, *now, now);
    assert(quic);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(53);
    picoquic_cnx_t *cnx = picoquic_create_cnx(quic, picoquic_null_connection_id,
        picoquic_null_connection_id, (struct sockaddr *)&address, *now, 0, "test", "test", 1);
    assert(cnx);
    cnx->cnx_state = picoquic_state_ready;
    assert(picoquic_create_stream(cnx, 0));
    assert(picoquic_create_stream(cnx, 4));
    return cnx;
}

/* Serialized QUIC STREAM, with or without its explicit length field. */
static size_t frame(uint8_t *bytes, size_t capacity, uint64_t stream_id,
    uint64_t offset, size_t length, int fin, int explicit_length) {
    uint8_t *end = picoquic_format_stream_frame_header(bytes, bytes + capacity, stream_id, offset);
    assert(end);
    if (explicit_length) {
        bytes[0] |= 2;
        end = picoquic_frames_varint_encode(end, bytes + capacity, length);
        assert(end);
    }
    if (fin) bytes[0] |= 1;
    assert(length <= (size_t)(bytes + capacity - end));
    memset(end, 0xa5, length);
    return (size_t)(end - bytes) + length;
}

static int ack_direct(picoquic_cnx_t *cnx, uint64_t stream_id, uint64_t offset,
    size_t length, int fin, int explicit_length) {
    uint8_t bytes[64];
    size_t size = frame(bytes, sizeof(bytes), stream_id, offset, length, fin, explicit_length);
    size_t consumed = SIZE_MAX;
    int ret = picoquic_process_ack_of_stream_frame(cnx, bytes, size, &consumed);
    CHECK(consumed == size);
    return ret;
}

static void ack_packet(picoquic_cnx_t *cnx, uint64_t offset, size_t length, int fin) {
    picoquic_packet_t packet = {0};
    picoquic_packet_data_t packet_data = {0};
    packet.ptype = picoquic_packet_1rtt_protected;
    packet.pc = picoquic_packet_context_application;
    packet.length = frame(packet.bytes, sizeof(packet.bytes), 0, offset, length, fin, 1);
    picoquic_process_ack_of_frames(cnx, &packet, &packet_data, 0, 1000000);
}

static int needs_repeat(picoquic_cnx_t *cnx, uint64_t offset, size_t length, int fin) {
    uint8_t bytes[64];
    size_t size = frame(bytes, sizeof(bytes), 0, offset, length, fin, 1);
    int no_repeat = -1, no_spurious = 0, preemptive = 0;
    CHECK(picoquic_check_frame_needs_repeat(cnx, bytes, size,
        picoquic_packet_1rtt_protected, &no_repeat, &no_spurious, &preemptive) == 0);
    return !no_repeat;
}

static int range_is(picoquic_sack_item_t *item, uint64_t first, uint64_t last) {
    return item && item->start_of_sack_range == first && item->end_of_sack_range == last;
}

static void check_hole(picoquic_cnx_t *cnx) {
    picoquic_sack_list_t *sack = &picoquic_find_stream(cnx, 0)->sack_list;
    CHECK(picoquic_sack_list_size(sack) == 2);
    CHECK(range_is(picoquic_sack_first_item(sack), 0, 3));
    CHECK(range_is(picoquic_sack_last_item(sack), 5, 7));
    CHECK(needs_repeat(cnx, 4, 1, 0));
    CHECK(!needs_repeat(cnx, 0, 4, 0));
    CHECK(!needs_repeat(cnx, 5, 3, 0));
    CHECK(needs_repeat(cnx, 0, 8, 0));
}

static void hole_case(void) {
    case_name = "hole";
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = connection(&now);
    picoquic_sack_list_t *sack = &picoquic_find_stream(cnx, 0)->sack_list;
    ack_packet(cnx, 0, 4, 0);
    ack_packet(cnx, 5, 3, 0);
    check_hole(cnx);
    /* Duplicates do not fill the one-byte gap. */
    ack_packet(cnx, 0, 4, 0);
    ack_packet(cnx, 5, 3, 0);
    check_hole(cnx);
    ack_packet(cnx, 5, 0, 0);
    fprintf(stderr, "HOLE empty5 ranges=%zu repeat4=%d\n",
        picoquic_sack_list_size(sack), needs_repeat(cnx, 4, 1, 0));
    check_hole(cnx);
    /* Empty offset zero formerly underflowed and swallowed both ranges and
     * the unacknowledged byte between them. */
    ack_packet(cnx, 0, 0, 0);
    fprintf(stderr, "HOLE empty0 ranges=%zu repeat4=%d\n",
        picoquic_sack_list_size(sack), needs_repeat(cnx, 4, 1, 0));
    check_hole(cnx);
    CHECK(needs_repeat(cnx, 8, 0, 1));
    ack_packet(cnx, 8, 0, 1);
    CHECK(picoquic_sack_list_size(sack) == 2);
    CHECK(range_is(picoquic_sack_last_item(sack), 5, 8));
    CHECK(!needs_repeat(cnx, 8, 0, 1) && needs_repeat(cnx, 4, 1, 0));
    ack_packet(cnx, 4, 1, 0);
    CHECK(picoquic_sack_list_size(sack) == 1);
    CHECK(range_is(picoquic_sack_first_item(sack), 0, 8));
    CHECK(!needs_repeat(cnx, 4, 1, 0) && !needs_repeat(cnx, 0, 8, 1));
    CHECK(picoquic_sack_list_is_empty(&picoquic_find_stream(cnx, 4)->sack_list));
    picoquic_free(cnx->quic);
}

static void empty_cases(void) {
    case_name = "empty";
    const uint64_t offsets[] = {0, 5, max_offset};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        for (int explicit_length = 0; explicit_length <= 1; explicit_length++) {
            uint64_t now = 1000000;
            picoquic_cnx_t *cnx = connection(&now);
            picoquic_sack_list_t *sack = &picoquic_find_stream(cnx, 0)->sack_list;
            CHECK(ack_direct(cnx, 0, offsets[i], 0, 0, explicit_length) == 0);
            fprintf(stderr, "EMPTY offset=%" PRIu64 " LEN=%d ranges=%zu\n",
                offsets[i], explicit_length, picoquic_sack_list_size(sack));
            CHECK(picoquic_sack_list_is_empty(sack));
            CHECK(needs_repeat(cnx, 4, 1, 0));
            CHECK(ack_direct(cnx, 0, offsets[i], 0, 0, explicit_length) == 0);
            CHECK(picoquic_sack_list_is_empty(sack));
            picoquic_free(cnx->quic);
        }
    }
}

static void fin_and_consumption_cases(void) {
    case_name = "FIN/consumption";
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = connection(&now);
    picoquic_sack_list_t *sack = &picoquic_find_stream(cnx, 0)->sack_list;
    CHECK(ack_direct(cnx, 0, 0, 0, 1, 1) == 0);
    CHECK(range_is(picoquic_sack_first_item(sack), 0, 0));
    CHECK(!needs_repeat(cnx, 0, 0, 1));
    CHECK(ack_direct(cnx, 0, 0, 0, 1, 0) == 0);
    CHECK(picoquic_sack_list_size(sack) == 1);
    picoquic_free(cnx->quic);

    cnx = connection(&now);
    sack = &picoquic_find_stream(cnx, 0)->sack_list;
    CHECK(ack_direct(cnx, 0, 5, 3, 1, 0) == 0);
    CHECK(range_is(picoquic_sack_first_item(sack), 5, 8));
    CHECK(!needs_repeat(cnx, 5, 3, 1));
    CHECK(ack_direct(cnx, 0, 5, 3, 1, 1) == 0);
    CHECK(picoquic_sack_list_size(sack) == 1);
    picoquic_free(cnx->quic);

    /* An empty explicit-LEN frame must be consumed without hiding the next
     * STREAM or changing another stream's SACK. */
    cnx = connection(&now);
    picoquic_packet_t packet = {0};
    picoquic_packet_data_t packet_data = {0};
    packet.ptype = picoquic_packet_1rtt_protected;
    packet.pc = picoquic_packet_context_application;
    packet.length = frame(packet.bytes, sizeof(packet.bytes), 0, 0, 0, 0, 1);
    packet.length += frame(packet.bytes + packet.length, sizeof(packet.bytes) - packet.length,
        4, 9, 2, 0, 1);
    picoquic_process_ack_of_frames(cnx, &packet, &packet_data, 0, now);
    CHECK(picoquic_sack_list_is_empty(&picoquic_find_stream(cnx, 0)->sack_list));
    CHECK(range_is(picoquic_sack_first_item(&picoquic_find_stream(cnx, 4)->sack_list), 9, 10));
    picoquic_free(cnx->quic);
}

static void domain_cases(void) {
    case_name = "end-domain";
    for (int fin = 0; fin <= 1; fin++) {
        for (int explicit_length = 0; explicit_length <= 1; explicit_length++) {
            uint64_t now = 1000000;
            picoquic_cnx_t *cnx = connection(&now);
            picoquic_sack_list_t *sack = &picoquic_find_stream(cnx, 0)->sack_list;
            CHECK(ack_direct(cnx, 0, max_offset - 2, 2, fin, explicit_length) == 0);
            uint64_t last = max_offset - (fin ? 0 : 1);
            CHECK(range_is(picoquic_sack_first_item(sack), max_offset - 2, last));
            CHECK(!needs_repeat(cnx, max_offset - 2, 2, fin));
            int ret = ack_direct(cnx, 0, max_offset, 1, fin, explicit_length);
            fprintf(stderr, "DOMAIN FIN=%d LEN=%d max+1 ret=%d\n", fin, explicit_length, ret);
            CHECK(ret == -1);
            CHECK(ack_direct(cnx, 0, max_offset - 1, 3, fin, explicit_length) == -1);
            CHECK(picoquic_sack_list_size(sack) == 1);
            CHECK(range_is(picoquic_sack_first_item(sack), max_offset - 2, last));
            /* Domain validation must not depend on the stream still existing. */
            CHECK(ack_direct(cnx, 8, max_offset, 1, fin, explicit_length) == -1);
            CHECK(ack_direct(cnx, 0, max_offset, 0, 1, explicit_length) == 0);
            CHECK(range_is(picoquic_sack_last_item(sack), max_offset - 2, max_offset));
            picoquic_free(cnx->quic);
        }
    }
}

static void malformed_cases(void) {
    case_name = "truncated/varint";
    uint64_t now = 1000000;
    picoquic_cnx_t *cnx = connection(&now);
    picoquic_sack_list_t *sack = &picoquic_find_stream(cnx, 0)->sack_list;
    CHECK(ack_direct(cnx, 0, 0, 4, 0, 1) == 0);
    uint8_t bytes[64];
    size_t size = frame(bytes, sizeof(bytes), 0, max_offset - 2, 2, 1, 1);
    /* Keep the type byte: the production parser requires a nonempty frame. */
    for (size_t prefix = 1; prefix < size; prefix++) {
        size_t consumed = SIZE_MAX;
        CHECK(picoquic_process_ack_of_stream_frame(cnx, bytes, prefix, &consumed) == -1);
        CHECK(consumed <= prefix);
        CHECK(picoquic_sack_list_size(sack) == 1);
        CHECK(range_is(picoquic_sack_first_item(sack), 0, 3));
    }
    /* QUIC varints cannot encode a value above 2^62-1. An eight-byte maximum
     * length with no payload is rejected, not mistaken for an empty frame. */
    uint8_t huge_length[] = {0x0a, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    size_t consumed = SIZE_MAX;
    CHECK(picoquic_process_ack_of_stream_frame(cnx, huge_length, sizeof(huge_length), &consumed) == -1);
    CHECK(consumed <= sizeof(huge_length));
    CHECK(picoquic_sack_list_size(sack) == 1);
    CHECK(range_is(picoquic_sack_first_item(sack), 0, 3));
    picoquic_free(cnx->quic);
}

int main(void) {
    hole_case();
    empty_cases();
    fin_and_consumption_cases();
    domain_cases();
    malformed_cases();
    fprintf(stderr, "STREAM ACK ranges: %u failed checks\n", failures);
    return failures != 0;
}
