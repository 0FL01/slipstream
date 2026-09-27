#include "slipstream_runtime.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

int ss_read_credentials(int fd, ss_credentials *c) {
    size_t n = 0;
    while (n < sizeof(c->bytes)) {
        ssize_t r = read(fd, c->bytes + n, sizeof(c->bytes) - n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) goto fail;
        n += (size_t)r;
    }
    uint8_t extra;
    ssize_t r;
    do { r = read(fd, &extra, 1); } while (r < 0 && errno == EINTR);
    if (r != 0) goto fail;
    for (n = 16; n < 80; n++) {
        if (!((c->bytes[n] >= '0' && c->bytes[n] <= '9') ||
              (c->bytes[n] >= 'a' && c->bytes[n] <= 'f'))) goto fail;
    }
    return 0;
fail:
    memset(c, 0, sizeof(*c));
    return -1;
}

static void consume(ss_stream *s, size_t n) {
    s->setup_len -= n;
    memmove(s->setup, s->setup + n, s->setup_len);
}

static int address_length(const uint8_t *p, size_t n) {
    if (!n) return 0;
    if (p[0] == 1) return 5;
    if (p[0] == 4) return 17;
    if (p[0] != 3) return -1;
    if (n < 2) return 0;
    if (p[1] == 0 || p[1] > 253) return -1;
    return 2 + p[1];
}

static void socks_reply(uint8_t *r, size_t *n, uint8_t code) {
    const uint8_t response[] = {5, code, 0, 1, 0, 0, 0, 0, 0, 0};
    memcpy(r, response, sizeof(response));
    *n = sizeof(response);
}

int ss_socks_step(ss_stream *s, const ss_credentials *c, uint8_t *open,
                  size_t *open_len, uint8_t *reply, size_t *reply_len) {
    uint8_t *p = s->setup;
    size_t n = s->setup_len;
    *open_len = *reply_len = 0;
    if (s->stage == 0) {
        if (n < 2) return 0;
        if (p[0] != 5 || p[1] == 0 || p[1] > 16) return -1;
        if (n < (size_t)2 + p[1]) return 0;
        bool auth = false;
        for (size_t i = 2; i < (size_t)2 + p[1]; i++) auth |= p[i] == 2;
        reply[0] = 5; reply[1] = auth ? 2 : 255; *reply_len = 2;
        consume(s, 2 + p[1]);
        s->stage = 1;
        return auth ? 0 : -1;
    }
    if (s->stage == 1) {
        if (n < 2) return 0;
        if (p[0] != 1 || p[1] != 32) return -1;
        if (n < 35) return 0;
        if (p[34] != 32) {
            reply[0] = 1; reply[1] = 1; *reply_len = 2;
            return -1;
        }
        if (n < 67) return 0;
        unsigned diff = 0;
        for (size_t i = 0; i < 32; i++) {
            diff |= p[2 + i] ^ c->bytes[16 + i];
            diff |= p[35 + i] ^ c->bytes[48 + i];
        }
        reply[0] = 1; reply[1] = diff ? 1 : 0; *reply_len = 2;
        consume(s, 67);
        s->stage = 2;
        return diff ? -1 : 0;
    }
    size_t prefix = s->stage == 2 ? 3 : 1;
    if (n < prefix + 1) return 0;
    if (s->stage == 2 && (p[0] != 5 || p[2] != 0)) return -1;
    if (s->stage == 2 && p[1] != 1) {
        socks_reply(reply, reply_len, 7); return -1;
    }
    if (s->stage == 3 && p[0] != 0) return -1;
    int alen = address_length(p + prefix, n - prefix);
    if (alen < 0) { socks_reply(reply, reply_len, 8); return -1; }
    if (!alen || n < prefix + (size_t)alen + 2) return 0;
    size_t total = prefix + (size_t)alen + 2;
    uint8_t *a = p + prefix;
    bool zero_port = p[total - 2] == 0 && p[total - 1] == 0;
    static const char magic[] = "sp.v2.udp-over-tcp.arpa";
    bool uot = s->stage == 2 && a[0] == 3 && a[1] == sizeof(magic) - 1 &&
               memcmp(a + 2, magic, sizeof(magic) - 1) == 0 && zero_port;
    if (zero_port && !uot) { socks_reply(reply, reply_len, 1); return -1; }
    if (s->stage == 2) socks_reply(reply, reply_len, 0);
    if (uot) {
        consume(s, total); s->stage = 3; return 0;
    }
    unsigned type = a[0] == 1 ? 0 : a[0] == 4 ? 1 : 2;
    open[0] = (uint8_t)(0x20 | (s->stage == 3 ? 8 : 0) | type << 1);
    memcpy(open + 1, c->bytes, 16);
    memcpy(open + 17, a + 1, (size_t)alen - 1 + 2);
    *open_len = 17 + (size_t)alen + 1;
    consume(s, total);
    s->stage = 4;
    return 1;
}

ss_phase ss_scheduler_phase(size_t streams, uint64_t last, uint64_t now, bool ready, bool blocked) {
    if (!streams) return SS_EMPTY;
    uint64_t elapsed = now >= last ? now - last : 0;
    if (ready || blocked || elapsed < 400000) return SS_BUSY;
    return elapsed < 1000000 ? SS_WARM : SS_QUIET;
}

size_t ss_poll_budget(ss_phase phase, size_t deficit, bool due, bool ready, bool blocked) {
    if (phase == SS_EMPTY || (phase == SS_BUSY && ready && !blocked)) return 0;
    return phase == SS_BUSY ? deficit : due ? 1 : 0;
}

static uint64_t add_saturating(uint64_t a, uint64_t b) {
    return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

uint64_t ss_receive_credit_target(uint64_t received, size_t pending, uint64_t advertised) {
    if (pending >= SS_RECEIVE_WINDOW) return 0;
    /* Grant early enough for DNS-delayed feedback, while advertising no more
     * than the existing window beyond bytes already drained to the socket. */
    uint64_t target = add_saturating(received, SS_RECEIVE_WINDOW - pending);
    return target > advertised && target - advertised >= SS_RECEIVE_WINDOW / 8 ? target : 0;
}

uint64_t ss_sparse_poll_deadline(ss_phase phase, uint64_t now, bool sent) {
    uint64_t interval = sent ? (phase == SS_WARM ? 400000 : 2000000) : 50000;
    return add_saturating(now, interval);
}

uint64_t ss_scheduler_timeout(ss_phase phase, uint64_t delay, uint64_t now,
    uint64_t last, uint64_t next_poll, bool fast_work) {
    if (!delay) delay = 1;
    if (phase == SS_BUSY || fast_work) return delay < 50000 ? delay : 50000;
    if (phase == SS_WARM || phase == SS_QUIET) {
        uint64_t deadline = next_poll;
        uint64_t warm_end = add_saturating(last, 1000000);
        if (phase == SS_WARM && warm_end < deadline) deadline = warm_end;
        uint64_t wait = deadline > now ? deadline - now : 1;
        if (wait < delay) delay = wait;
    }
    return delay;
}
