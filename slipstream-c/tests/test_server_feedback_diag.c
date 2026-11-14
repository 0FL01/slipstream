/* Positive control: prove the observer sees a genuine pacing/control denial. */
#include "server_feedback_diag.c"
#include <assert.h>

int main(void) {
    picoquic_quic_t quic = {0};
    picoquic_cnx_t cnx = {0};
    picoquic_path_t path = {0};
    cnx.quic = &quic; cnx.cnx_state = picoquic_state_ready;
    cnx.ack_ctx[picoquic_packet_context_application].act[0].ack_needed = 1;
    path.cwin = UINT64_MAX; path.send_mtu = 900;
    uint64_t now = 1000, next = UINT64_MAX;
    picoquic_pacing_init(&path.pacing, now);
    for (int i = 0; i < 16; i++) {
        assert(__wrap_picoquic_is_sending_authorized_by_pacing(&cnx, &path, now, &next));
        picoquic_update_pacing_after_send(&path, path.send_mtu, now);
    }
    assert(!__wrap_picoquic_is_sending_authorized_by_pacing(&cnx, &path, now, &next));
    assert(checks == 17 && denied == 1 && denied_control == 1);
    assert(packet_ns_min == 1 && packet_ns_max == 1 && bucket_max == 16);
    /* One microsecond refills the bucket; normal receive batches have 10 slots. */
    assert(__wrap_picoquic_is_sending_authorized_by_pacing(&cnx, &path, now + 1, &next));
    assert(path.pacing.bucket_nanosec == 16);
    return 0;
}
