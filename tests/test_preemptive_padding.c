#define main upstream_open_main
#include "test_open_stream_repeat.c"
#undef main

/* Tests include the real vendor fixtures without modifying their expectations. */
struct sink { uint8_t bytes[20000]; size_t length; unsigned fins; };
struct stats { size_t pads, stream_bytes; unsigned streams, acks, polls, challenges, responses; };
static unsigned authenticated, cases;
static uint8_t datum(size_t i) { return (uint8_t)((i*37+19)&255); }

static int sink_data(picoquic_cnx_t *c, uint64_t id, uint8_t *b, size_t n,
    picoquic_call_back_event_t event, void *ctx, void *stream_ctx) {
    (void)c; (void)stream_ctx; struct sink *s = ctx;
    if (event == picoquic_callback_stream_data || event == picoquic_callback_stream_fin) {
        assert(id == 0 && s->length+n <= sizeof(s->bytes));
        memcpy(s->bytes+s->length,b,n); s->length += n;
        s->fins += event == picoquic_callback_stream_fin;
    }
    return 0;
}

static picoquic_packet_context_t *context(picoquic_cnx_t *c) {
    return c->is_multipath_enabled ? &c->path[0]->pkt_ctx : &c->pkt_ctx[application];
}

static picoquic_cnx_t *sender(uint64_t *now, int mp, size_t mtu,
    uint32_t minimum, uint32_t multiple) {
    picoquic_cnx_t *c = ready_sender(now,mp,140000);
    c->path[0]->send_mtu = mtu; c->path[0]->cwin = 100000;
    /* Only fixture-specific cases override the configured policy; never global. */
    assert(c->padding_minsize == 39 && c->padding_multiple == 128);
    c->padding_minsize = minimum; c->padding_multiple = multiple;
    picoquic_update_pacing_data(c,c->path[0],1);
    return c;
}

static void add_data(picoquic_cnx_t *c, size_t off, size_t n, int fin) {
    uint8_t data[20000]; assert(n <= sizeof(data));
    for (size_t i=0; i<n; i++) data[i] = datum(off+i);
    assert(!picoquic_add_to_stream(c,0,data,n,fin));
}

static size_t prefix(picoquic_cnx_t *c, size_t off, uint8_t *frame) {
    if (!off) return 0;
    add_data(c,0,off,0);
    return stream_frame(c,frame,off+16);
}

static struct stats inspect(picoquic_packet_t *p, size_t off, size_t n, int fin) {
    struct stats s = {0};
    for (size_t i=p->offset; i<p->length;) {
        size_t used=0; int pure=0; uint64_t type;
        assert(picoquic_frames_varint_decode(p->bytes+i,p->bytes+p->length,&type));
        assert(!picoquic_skip_frame(p->bytes+i,p->length-i,&used,&pure) && used);
        if (!type) s.pads += used;
        else if (type >= picoquic_frame_type_stream_range_min &&
                 type <= picoquic_frame_type_stream_range_max) {
            uint64_t id,offset; size_t length,header; int f;
            assert(!picoquic_parse_stream_header(p->bytes+i,used,&id,&offset,&length,&f,&header));
            assert(id == 0 && offset == off && length == n && f == fin && header+n == used);
            for (size_t j=0; j<n; j++) assert(p->bytes[i+header+j] == datum(off+j));
            s.streams++; s.stream_bytes += used;
        } else if (type == picoquic_frame_type_ack || type == picoquic_frame_type_ack_ecn ||
                   type == picoquic_frame_type_path_ack || type == picoquic_frame_type_path_ack_ecn) s.acks++;
        else if (type == picoquic_frame_type_poll) s.polls++;
        else if (type == picoquic_frame_type_path_challenge) s.challenges++;
        else if (type == picoquic_frame_type_path_response) s.responses++;
        else { fprintf(stderr,"unexpected frame %llu\n",(unsigned long long)type); assert(0); }
        i += used;
    }
    return s;
}

static void padding(picoquic_cnx_t *c, picoquic_packet_t *p, struct stats s,
    size_t capacity, int forced) {
    size_t max = capacity < c->path[0]->send_mtu ? capacity : c->path[0]->send_mtu;
    max -= p->checksum_overhead;
    size_t raw = p->length-s.pads, target = c->padding_minsize;
    if (forced) target = max;
    else if (p->is_preemptive_repeat) {
        if (target < PICOQUIC_RESET_PACKET_MIN_SIZE) target = PICOQUIC_RESET_PACKET_MIN_SIZE;
    } else if (raw > target && c->padding_multiple) {
        size_t remainder = (raw-target)%c->padding_multiple;
        target = remainder ? raw+c->padding_multiple-remainder : raw;
    }
    if (target > max) target = max;
    if (target < raw) target = raw;
    assert(p->length == target && s.pads == target-raw);
}

/* Actual ready sender -> header unprotection -> actual AEAD decryption with AAD.
 * Also reject a modified authentication tag and (in MP mode) the wrong path IV.
 * Deliberately not a TLS handshake or incoming_packet/network acceptance test. */
static picoquic_packet_t *protected_prepare_path(picoquic_cnx_t *c, picoquic_path_t *path,
    uint64_t now, size_t capacity, int initial_flag, size_t *sent) {
    uint8_t buffer[1632], plain[1600]; uint8_t *wire=buffer+16;
    assert(capacity <= sizeof(buffer)-32);
    memset(buffer,0xcc,sizeof(buffer));
    uint64_t wake=UINT64_MAX; int initial=initial_flag;
    path->pacing.bucket_nanosec = 1000000000;
    picoquic_packet_t *p = picoquic_create_packet(c->quic); assert(p);
    *sent=0;
    assert(!picoquic_prepare_packet_ready(c,path,p,now,wire,capacity,sent,&wake,&initial));
    for (size_t i=0; i<16; i++) assert(buffer[i] == 0xcc);
    for (size_t i=16+capacity; i<sizeof(buffer); i++) assert(buffer[i] == 0xcc);
    if (!*sent) {
        assert(!p->is_queued_for_retransmit); picoquic_recycle_packet(c->quic,p); return NULL;
    }
    assert(*sent <= capacity && p->is_queued_for_retransmit && *sent == p->length+p->checksum_overhead);
    static const uint8_t secret[32]={1,2,3,4};
    void *dec=picoquic_setup_test_aead_context(0,secret,PICOQUIC_LABEL_QUIC_V1_KEY_BASE); assert(dec);
    picoquic_packet_header ph={0}; ph.ptype=picoquic_packet_1rtt_protected;
    ph.pn_offset=ph.offset=1+path->p_remote_cnxid->cnx_id.id_len;
    ph.payload_length=*sent-ph.offset;
    assert(!picoquic_remove_header_protection_inner(wire,*sent,plain,&ph,
        c->crypto_context[3].pn_enc,0,p->sequence_number ? p->sequence_number-1 : UINT64_MAX));
    assert(ph.pn64 == p->sequence_number && ph.offset == p->offset);
    size_t length = c->is_multipath_enabled ?
        picoquic_aead_decrypt_mp(plain+ph.offset,wire+ph.offset,*sent-ph.offset,
            path->unique_path_id,ph.pn64,plain,ph.offset,dec) :
        picoquic_aead_decrypt_generic(plain+ph.offset,wire+ph.offset,*sent-ph.offset,
            ph.pn64,plain,ph.offset,dec);
    assert(length == p->length-p->offset && !memcmp(plain+ph.offset,p->bytes+p->offset,length));
    wire[*sent-1] ^= 1;
    length = c->is_multipath_enabled ?
        picoquic_aead_decrypt_mp(plain+ph.offset,wire+ph.offset,*sent-ph.offset,
            path->unique_path_id,ph.pn64,plain,ph.offset,dec) :
        picoquic_aead_decrypt_generic(plain+ph.offset,wire+ph.offset,*sent-ph.offset,
            ph.pn64,plain,ph.offset,dec);
    assert(length > *sent); wire[*sent-1] ^= 1;
    if (c->is_multipath_enabled) {
        assert(picoquic_aead_decrypt_mp(plain+ph.offset,wire+ph.offset,*sent-ph.offset,
            path->unique_path_id+1,ph.pn64,plain,ph.offset,dec) > *sent);
    }
    picoquic_aead_free(dec); authenticated++;
    return p;
}

static picoquic_packet_t *protected_prepare(picoquic_cnx_t *c, uint64_t now,
    size_t capacity, int initial_flag, size_t *sent) {
    return protected_prepare_path(c,c->path[0],now,capacity,initial_flag,sent);
}

static void require_ack(picoquic_cnx_t *c, uint64_t now) {
    assert(!picoquic_record_pn_received(c,application,c->path[0]->p_local_cnxid,0,now));
    picoquic_set_ack_needed(c,now,application,c->path[0],1);
}

static void peer_once(picoquic_cnx_t *c, uint64_t now, const uint8_t *pre, size_t pre_n,
    const uint8_t *a, size_t an, const uint8_t *b, size_t bn,
    size_t total, int fin) {
    struct sink sink={0}; picoquic_cnx_t *peer=connection_mode(&now,0,c->is_multipath_enabled);
    context(peer)->send_sequence=1; picoquic_set_callback(peer,sink_data,&sink);
    if (pre_n) decode(peer,pre,pre_n,now);
    decode(peer,a,an,now+1); decode(peer,b,bn,now+2); decode(peer,a,an,now+3);
    assert(sink.length == total && sink.fins == (unsigned)fin);
    for (size_t i=0; i<total; i++) assert(sink.bytes[i] == datum(i));
    picoquic_free(peer->quic);
}

static void lifecycle(int mp, size_t mtu, size_t n, size_t off, int fin,
    int ack, uint32_t min, uint32_t multiple, int exact_capacity, int forced) {
    uint64_t now=1000000; picoquic_cnx_t *c=sender(&now,mp,mtu,min,multiple);
    uint8_t pre[20000], saved[1600], copied[1600]; size_t pre_n=prefix(c,off,pre);
    add_data(c,off,n,fin);
    size_t original_wire;
    picoquic_packet_t *original=protected_prepare(c,now,mtu,0,&original_wire); assert(original);
    assert(!original->is_preemptive_repeat && !original->was_preemptively_repeated);
    struct stats os=inspect(original,off,n,fin);
    assert(os.streams == 1 && !os.acks && !os.polls); padding(c,original,os,mtu,0);
    memcpy(saved,original->bytes,original->length); size_t saved_len=original->length;
    uint64_t pn=original->sequence_number, send_time=original->send_time;
    uint64_t flight=c->path[0]->bytes_in_transit, cwin=c->path[0]->cwin;
    assert(flight == original_wire && original->is_queued_for_retransmit);
    if (ack) require_ack(c,now+35001);
    size_t capacity=exact_capacity ? original->offset+os.stream_bytes+16 : mtu;
    assert(!exact_capacity || !ack);
    size_t copy_wire;
    picoquic_packet_t *copy=protected_prepare(c,now+35001,capacity,forced,&copy_wire); assert(copy);
    struct stats cs=inspect(copy,off,n,fin);
    assert(copy->is_preemptive_repeat && original->was_preemptively_repeated && c->nb_preemptive_repeat == 1);
    assert(cs.streams == 1 && cs.acks == (unsigned)ack && !cs.polls);
    padding(c,copy,cs,capacity,forced);
    assert(copy->sequence_number == pn+1 && copy->send_time == now+35001);
    assert(context(c)->pending_first == original && context(c)->pending_last == copy);
    assert(original->packet_next == copy && copy->packet_previous == original);
    assert(original->send_path == c->path[0] && original->sequence_number == pn && original->send_time == send_time);
    assert(original->length == saved_len && !memcmp(saved,original->bytes,saved_len));
    assert(c->path[0]->bytes_in_transit == flight+copy_wire && c->path[0]->cwin == cwin);
    size_t copy_len=copy->length-copy->offset; memcpy(copied,copy->bytes+copy->offset,copy_len);
    peer_once(c,now,pre,pre_n,copied,copy_len,saved+original->offset,saved_len-original->offset,off+n,fin);
    size_t none;
    assert(!protected_prepare(c,now+70002,mtu,0,&none) && !none && c->nb_preemptive_repeat == 1);
    assert(!memcmp(saved,original->bytes,saved_len) && context(c)->pending_first == original);
    size_t repair_wire=0, repair_pad=0;
    if (n == 45 && off == 0 && min == 39 && multiple == 128 && !exact_capacity && !forced && !ack) {
        uint64_t later=now+70002+2*c->path[0]->retransmit_timer;
        picoquic_packet_t *scratch=picoquic_create_packet(c->quic); assert(scratch);
        size_t header=0; uint64_t wake=UINT64_MAX;
        assert(!picoquic_retransmit_needed(c,application,c->path[0],later,&wake,scratch,mtu,&header));
        assert(picoquic_first_data_repeat_packet(c)); picoquic_recycle_packet(c->quic,scratch);
        picoquic_packet_t *repair=protected_prepare(c,later+1,mtu,0,&repair_wire); assert(repair);
        assert(!repair->is_preemptive_repeat && c->nb_preemptive_repeat == 1);
        struct stats rs=inspect(repair,off,n,fin); assert(rs.streams == 1 && !rs.acks);
        padding(c,repair,rs,mtu,0); repair_pad=rs.pads;
        peer_once(c,later,pre,pre_n,repair->bytes+repair->offset,repair->length-repair->offset,
            copied,copy_len,off+n,fin);
    }
    printf("LIFECYCLE mp=%d mtu=%zu n=%zu off=%zu fin=%d ack=%d policy=%u/%u cap=%zu forced=%d original=%zu/%zu copy=%zu/%zu repair=%zu/%zu wire/pad\n",
        mp,mtu,n,off,fin,ack,min,multiple,capacity,forced,original_wire,os.pads,copy_wire,cs.pads,repair_wire,repair_pad);
    cases++; picoquic_free(c->quic);
}

static void no_space(int mp, int ack) {
    uint64_t now=1000000; picoquic_cnx_t *c=sender(&now,mp,143,39,128);
    size_t n=ack ? 113 : 45; add_data(c,0,n,1);
    size_t sent; picoquic_packet_t *original=protected_prepare(c,now,143,0,&sent); assert(original);
    uint8_t saved[1600]; size_t saved_n=original->length; memcpy(saved,original->bytes,saved_n);
    struct stats os=inspect(original,0,n,1);
    size_t capacity=ack ? 143 : original->offset+os.stream_bytes+16-1;
    if (ack) require_ack(c,now+35001);
    picoquic_packet_t *a=protected_prepare(c,now+35001,capacity,0,&sent);
    if (ack) {
        assert(a && !a->is_preemptive_repeat); struct stats as=inspect(a,0,0,0);
        assert(as.acks == 1 && !as.streams); padding(c,a,as,capacity,0);
    } else assert(!a && !sent);
    assert(!original->was_preemptively_repeated && !c->nb_preemptive_repeat);
    assert(context(c)->preemptive_repeat_ptr == original && !memcmp(saved,original->bytes,saved_n));
    picoquic_packet_t *copy=protected_prepare(c,now+35002,143,0,&sent); assert(copy);
    struct stats cs=inspect(copy,0,n,1);
    assert(copy->is_preemptive_repeat && original->was_preemptively_repeated && cs.streams == 1 && !cs.acks);
    padding(c,copy,cs,143,0);
    peer_once(c,now,NULL,0,copy->bytes+copy->offset,copy->length-copy->offset,
        original->bytes+original->offset,original->length-original->offset,n,1);
    printf("DEFERRAL mp=%d ACK=%d first-cap=%zu retry=%zu retained/no truncation\n",mp,ack,capacity,sent);
    cases++; picoquic_free(c->quic);
}

static void fresh_priority(int mp) {
    uint64_t now=1000000; picoquic_cnx_t *c=sender(&now,mp,143,39,128);
    add_data(c,0,45,0); size_t sent;
    picoquic_packet_t *original=protected_prepare(c,now,143,0,&sent); assert(original);
    add_data(c,45,20,0); require_ack(c,now+35001);
    picoquic_packet_t *fresh=protected_prepare(c,now+35001,143,0,&sent); assert(fresh);
    struct stats fs=inspect(fresh,45,20,0);
    assert(!fresh->is_preemptive_repeat && !original->was_preemptively_repeated && !c->nb_preemptive_repeat);
    assert(fs.streams == 1 && fs.acks == 1); padding(c,fresh,fs,143,0);
    size_t fresh_wire=sent;
    picoquic_packet_t *copy=protected_prepare(c,now+35002,143,0,&sent); assert(copy);
    struct stats cs=inspect(copy,0,45,0); padding(c,copy,cs,143,0);
    assert(copy->is_preemptive_repeat && original->was_preemptively_repeated && !fresh->was_preemptively_repeated);
    uint8_t both[1600]; size_t bn=fresh->length-fresh->offset;
    memcpy(both,fresh->bytes+fresh->offset,bn);
    memcpy(both+bn,original->bytes+original->offset,original->length-original->offset);
    bn += original->length-original->offset;
    peer_once(c,now,NULL,0,copy->bytes+copy->offset,copy->length-copy->offset,both,bn,65,0);
    printf("FRESH mp=%d ACK+fresh=%zu normal policy; due copy=%zu only after fresh\n",mp,fresh_wire,sent);
    cases++; picoquic_free(c->quic);
}

static void control(int mp, int kind) {
    uint64_t now=1000000; picoquic_cnx_t *c=sender(&now,mp,1500,39,128);
    if (kind == 0) require_ack(c,now);
    if (kind == 1) c->is_poll_requested=1;
    if (kind == 2) { c->path[0]->challenge_verified=0; c->path[0]->challenge_time=0; }
    if (kind == 3) { c->path[0]->response_required=1; c->path[0]->challenge_response=0x01020304; }
    size_t sent; picoquic_packet_t *p=protected_prepare(c,now,1500,0,&sent); assert(p);
    struct stats s=inspect(p,0,0,0); assert(!p->is_preemptive_repeat && !s.streams);
    assert((kind != 0 || s.acks == 1) && (kind != 1 || s.polls == 1));
    assert((kind != 2 || s.challenges == 1) && (kind != 3 || s.responses == 1));
    padding(c,p,s,1500,kind >= 2);
    if (kind >= 2) assert(sent == 1500);
    printf("CONTROL mp=%d kind=%d wire=%zu padding=%zu normal/forced branch\n",mp,kind,sent,s.pads);
    cases++; picoquic_free(c->quic);
}

/* Real unlimited original at exact capacity: earlier copy-frame leading padding
 * remains untouched, so this policy cannot reclaim that padding. */
static void unlimited_control(int mp) {
    uint64_t now=1000000; picoquic_cnx_t *c=sender(&now,mp,143,39,128);
    size_t h=picoquic_predict_packet_header_length(c,picoquic_packet_1rtt_protected,context(c));
    size_t n=143-h-16-2; add_data(c,0,n,1); size_t sent;
    picoquic_packet_t *o=protected_prepare(c,now,143,0,&sent); assert(o && sent == 143);
    assert(picoquic_is_stream_frame_unlimited(o->bytes+o->offset));
    picoquic_packet_t *copy=protected_prepare(c,now+35001,143,0,&sent); assert(copy && sent == 143);
    struct stats cs=inspect(copy,0,n,1);
    assert(copy->is_preemptive_repeat && cs.streams == 1 && !cs.pads);
    peer_once(c,now,NULL,0,copy->bytes+copy->offset,copy->length-copy->offset,
        o->bytes+o->offset,o->length-o->offset,n,1);
    printf("UNLIMITED mp=%d n=%zu original/copy=143 unchanged exact-tail encoding\n",mp,n);
    cases++; picoquic_free(c->quic);
}

static void unlimited_slack(int mp, size_t mtu) {
    uint64_t now=1000000; picoquic_cnx_t *c=sender(&now,mp,mtu,39,128);
    size_t h=picoquic_predict_packet_header_length(c,picoquic_packet_1rtt_protected,context(c));
    add_data(c,0,45,1); size_t original_wire,copy_wire;
    /* Produce a genuine no-LEN original by limiting its actual output capacity,
     * then let the normal early-copy path use the larger MTU. */
    picoquic_packet_t *o=protected_prepare(c,now,h+2+45+16,0,&original_wire); assert(o);
    assert(picoquic_is_stream_frame_unlimited(o->bytes+o->offset));
    uint8_t saved[1600]; memcpy(saved,o->bytes,o->length);
    picoquic_packet_t *copy=protected_prepare(c,now+35001,mtu,0,&copy_wire); assert(copy);
    struct stats cs=inspect(copy,0,45,1);
    assert(copy->is_preemptive_repeat && copy_wire == mtu && cs.streams == 1);
    assert(cs.pads == mtu-16-copy->offset-47 && cs.stream_bytes == 47);
    assert(!memcmp(saved,o->bytes,o->length) && o->was_preemptively_repeated);
    assert(!memcmp(copy->bytes+copy->offset+cs.pads,o->bytes+o->offset,47));
    peer_once(c,now,NULL,0,copy->bytes+copy->offset,copy->length-copy->offset,
        o->bytes+o->offset,o->length-o->offset,45,1);
    printf("UNLIMITED_SLACK mp=%d mtu=%zu original=%zu copy=%zu pads=%zu earlier leading padding NOT reclaimed\n",
        mp,mtu,original_wire,copy_wire,cs.pads);
    cases++; picoquic_free(c->quic);
}

/* Observe the actual protected wire, then deny at reserve+wire-1 and admit at
 * reserve+wire. This exercises the real sender, not a duplicate size formula.
 * ACK prefixes, PN widths, both credit limits and cap-clamped/reset minima are
 * included. Unlimited frames retain their leading padding; forced packets must
 * still consume a full MTU. Denial must leave the original and cursor intact. */
static void credit_edge(int mp, uint64_t pn, int ack, int maximum, uint32_t minimum,
    size_t capacity, size_t n, int fin, int unlimited, int forced) {
    size_t observed=0;
    for (int pass=0; pass<3; pass++) {
        uint64_t now=1000000; picoquic_cnx_t *c=sender(&now,mp,143,minimum,128);
        picoquic_path_t *path=c->path[0]; picoquic_packet_context_t *ctx=context(c);
        ctx->send_sequence=pn; ctx->next_sequence_hole=UINT64_MAX;
        add_data(c,0,n,fin);
        size_t original_capacity=unlimited ?
            picoquic_predict_packet_header_length(c,picoquic_packet_1rtt_protected,ctx)+2+n+16 : 143;
        size_t wire;
        picoquic_packet_t *original=protected_prepare(c,now,original_capacity,0,&wire); assert(original);
        if (unlimited) assert(picoquic_is_stream_frame_unlimited(original->bytes+original->offset));
        uint8_t saved[1600]; size_t saved_n=original->length; memcpy(saved,original->bytes,saved_n);
        picoquic_packet_t *cursor=ctx->preemptive_repeat_ptr;
        uint64_t flight=path->bytes_in_transit, time=original->send_time, number=original->sequence_number;
        if (ack) require_ack(c,now+35001);
        uint64_t credit=pass ? 143+observed-(pass == 1) : 10000;
        uint64_t limit=flight+credit;
        path->cwin=maximum ? UINT64_MAX : limit;
        if (maximum) c->quic->cwin_max=limit;
        picoquic_packet_t *copy=protected_prepare(c,now+35001,capacity,forced,&wire);
        if (pass == 1) {
            assert(!copy || !copy->is_preemptive_repeat);
            assert(!original->was_preemptively_repeated && !c->nb_preemptive_repeat);
            assert(ctx->preemptive_repeat_ptr == cursor);
            if (copy) { struct stats s=inspect(copy,0,0,0); assert(!s.streams); padding(c,copy,s,capacity,forced); }
        } else {
            assert(copy && copy->is_preemptive_repeat && original->was_preemptively_repeated);
            assert(c->nb_preemptive_repeat == 1 && path->bytes_in_transit == flight+wire);
            struct stats s=inspect(copy,0,n,fin);
            assert(s.streams == 1 && s.acks == (unsigned)ack);
            if (unlimited) {
                assert(copy->length == capacity-copy->checksum_overhead && s.pads > 0);
                assert(s.pads == copy->length-copy->offset-(2+n));
                assert(picoquic_is_stream_frame_unlimited(copy->bytes+copy->offset+s.pads));
            } else padding(c,copy,s,capacity,forced);
            if (!pass) observed=wire;
            else assert(wire == observed && limit-path->bytes_in_transit == 143);
            if (unlimited || forced) assert(wire == 143);
            peer_once(c,now,NULL,0,copy->bytes+copy->offset,copy->length-copy->offset,
                saved+original->offset,saved_n-original->offset,n,fin);
        }
        assert(original->length == saved_n && !memcmp(saved,original->bytes,saved_n));
        assert(original->sequence_number == number && original->send_time == time);
        assert(ctx->pending_first == original && original->is_queued_for_retransmit);
        picoquic_free(c->quic);
    }
    printf("CREDIT mp=%d pn=%llu ACK=%d max=%d min=%u cap=%zu unlimited=%d forced=%d wire=%zu exact-reserve=143\n",
        mp,(unsigned long long)pn,ack,maximum,minimum,capacity,unlimited,forced,observed);
    cases++;
}

/* Fresh132 delivery, not just sent_offset: receive fresh ranges out of order,
 * then copies/originals/duplicate fresh, and require every byte exactly once. */
static void credit_fresh_delivery(int mp, uint64_t cwin, unsigned expected) {
    uint64_t now=1000000; picoquic_cnx_t *c=sender(&now,mp,143,39,128); size_t wire;
    add_data(c,0,45,0); picoquic_packet_t *a=protected_prepare(c,now,143,0,&wire); assert(a && wire == 143);
    add_data(c,45,45,0); picoquic_packet_t *b=protected_prepare(c,now+4000,143,0,&wire); assert(b && wire == 143);
    uint8_t sa[1600], sb[1600]; size_t an=a->length, bn=b->length;
    memcpy(sa,a->bytes,an); memcpy(sb,b->bytes,bn); c->path[0]->cwin=cwin;
    picoquic_packet_t *copies[2]={0}; unsigned count=0;
    for (unsigned i=0; i<2; i++) {
        uint64_t before=c->path[0]->bytes_in_transit;
        picoquic_packet_t *x=protected_prepare(c,now+35001+i*4000,143,0,&wire);
        if (x) {
            assert(x->is_preemptive_repeat && wire == (i ? 75u : 74u));
            assert(cwin-before >= 143+wire && cwin-c->path[0]->bytes_in_transit >= 143);
            copies[count++]=x;
        }
    }
    assert(count == expected && c->nb_preemptive_repeat == expected);
    add_data(c,90,132,0); picoquic_stream_head_t *stream=picoquic_find_stream(c,0);
    picoquic_packet_t *fresh[4]={0}; unsigned nf=0;
    for (unsigned i=0; i<4 && stream->send_queue; i++) {
        picoquic_packet_t *x=protected_prepare(c,now+40000+i,143,0,&wire);
        assert(x && !x->is_preemptive_repeat && c->path[0]->bytes_in_transit <= cwin); fresh[nf++]=x;
    }
    assert(!stream->send_queue && stream->sent_offset == 222);
    assert(a->length == an && b->length == bn && !memcmp(sa,a->bytes,an) && !memcmp(sb,b->bytes,bn));
    struct sink sink={0}; picoquic_cnx_t *peer=connection_mode(&now,0,mp);
    context(peer)->send_sequence=1; picoquic_set_callback(peer,sink_data,&sink);
    for (unsigned i=nf; i>0; i--) decode(peer,fresh[i-1]->bytes+fresh[i-1]->offset,
        fresh[i-1]->length-fresh[i-1]->offset,now+i);
    for (unsigned i=count; i>0; i--) decode(peer,copies[i-1]->bytes+copies[i-1]->offset,
        copies[i-1]->length-copies[i-1]->offset,now+10+i);
    decode(peer,sa+a->offset,an-a->offset,now+20); decode(peer,sb+b->offset,bn-b->offset,now+21);
    for (unsigned i=0; i<nf; i++) decode(peer,fresh[i]->bytes+fresh[i]->offset,
        fresh[i]->length-fresh[i]->offset,now+30+i);
    assert(sink.length == 222 && !sink.fins);
    for (size_t i=0; i<sink.length; i++) assert(sink.bytes[i] == datum(i));
    printf("CREDIT_FRESH mp=%d cwin=%llu copies=%u fresh132 peer-exactly-once flight=%llu\n",
        mp,(unsigned long long)cwin,count,(unsigned long long)c->path[0]->bytes_in_transit);
    cases++; picoquic_free(peer->quic); picoquic_free(c->quic);
}

/* A genuine second output path has its own PN/header, credit, queue and AEAD
 * path IV. The original's source path has NO credit; it must not be charged. */
static void cross_path_credit(int source_index, int maximum) {
    for (int delta=-1; delta<=0; delta++) {
        uint64_t now=1000000; picoquic_cnx_t *c=sender(&now,1,143,39,128);
        struct sockaddr_in address={0}; address.sin_family=AF_INET;
        address.sin_addr.s_addr=htonl(INADDR_LOOPBACK); address.sin_port=htons(54);
        assert(picoquic_create_path(c,now,(struct sockaddr *)&address,(struct sockaddr *)&address,1) == 1);
        picoquic_path_t *p1=c->path[1]; uint8_t cid[8]={0xa7}, secret[16]={0};
        assert(!picoquic_stash_remote_cnxid(c,0,1,0,sizeof(cid),cid,secret,NULL));
        assert(!picoquic_assign_peer_cnxid_to_path(c,1));
        p1->p_local_cnxid=picoquic_create_local_cnxid(c,1,NULL,now); assert(p1->p_local_cnxid);
        p1->challenge_verified=1; p1->response_required=0; p1->send_mtu=143;
        p1->latest_sent_time=now; p1->rtt_min=p1->smoothed_rtt=140000;
        p1->rtt_is_initialized=1; p1->retransmit_timer=1000000; p1->cwin=10000; p1->pacing.rate=1000000;
        picoquic_path_t *src=c->path[source_index], *out=c->path[!source_index];
        out->pkt_ctx.send_sequence=16; out->pkt_ctx.next_sequence_hole=UINT64_MAX;
        add_data(c,0,45,0); size_t wire;
        picoquic_packet_t *original=protected_prepare_path(c,src,now,143,0,&wire); assert(original && wire == 143);
        uint8_t saved[1600]; size_t sn=original->length; memcpy(saved,original->bytes,sn);
        picoquic_packet_t *cursor=src->pkt_ctx.preemptive_repeat_ptr;
        uint64_t flight=src->bytes_in_transit; src->cwin=flight;
        out->cwin=218+delta; /* 2-byte output PN: protected copy is 75 bytes. */
        if (maximum) { c->quic->cwin_max=out->cwin; out->cwin=UINT64_MAX; }
        picoquic_packet_t *copy=protected_prepare_path(c,out,now+35001,143,0,&wire);
        if (delta < 0) {
            assert(!copy && !wire && !original->was_preemptively_repeated && !c->nb_preemptive_repeat);
            assert(src->pkt_ctx.preemptive_repeat_ptr == cursor);
        } else {
            assert(copy && copy->is_preemptive_repeat && wire == 75 && copy->send_path == out);
            assert(original->was_preemptively_repeated && c->nb_preemptive_repeat == 1);
            assert(src->bytes_in_transit == flight && out->bytes_in_transit == 75);
            assert((maximum ? c->quic->cwin_max : out->cwin)-out->bytes_in_transit == 143);
            assert(out->pkt_ctx.pending_last == copy && src->pkt_ctx.pending_first == original);
            peer_once(c,now,NULL,0,copy->bytes+copy->offset,copy->length-copy->offset,
                original->bytes+original->offset,original->length-original->offset,45,0);
        }
        assert(original->length == sn && !memcmp(saved,original->bytes,sn));
        printf("CROSS_PATH source=%d max=%d credit=%d copy=%d source-flight=%llu output-flight=%llu\n",
            source_index,maximum,218+delta,delta == 0,
            (unsigned long long)src->bytes_in_transit,(unsigned long long)out->bytes_in_transit);
        cases++; picoquic_free(c->quic);
    }
}

int main(void) {
    printf("Early-copy padding reset-min=%d (plaintext packet minimum, before tag)\n",PICOQUIC_RESET_PACKET_MIN_SIZE);
    const size_t small[]={1,11,45,63,64,100};
    const size_t medium[]={255,512,1024};
    for (int mp=0; mp<=1; mp++) {
        for (int fin=0; fin<=1; fin++) {
            for (size_t i=0; i<sizeof(small)/sizeof(small[0]); i++) {
                lifecycle(mp,143,small[i],0,fin,0,39,128,0,0);
                lifecycle(mp,1500,small[i],64,fin,1,39,128,0,0);
            }
            lifecycle(mp,1500,45,0,fin,0,39,128,0,0);
            lifecycle(mp,143,45,0,fin,1,39,128,0,0);
            for (size_t i=0; i<sizeof(medium)/sizeof(medium[0]); i++)
                lifecycle(mp,1500,medium[i],16384,fin,0,39,128,0,0);
            lifecycle(mp,143,1,0,fin,0,0,128,0,0);
            lifecycle(mp,143,1,0,fin,0,0,128,1,0);
            lifecycle(mp,143,1,0,fin,0,0,0,0,0);
            lifecycle(mp,143,1,0,fin,0,80,128,0,0);
            lifecycle(mp,143,45,0,fin,0,300,128,0,0);
            lifecycle(mp,143,45,0,fin,0,39,128,1,0);
            lifecycle(mp,143,45,0,fin,0,39,128,0,1);
            lifecycle(mp,1500,45,0,fin,0,39,128,0,1);
            lifecycle(mp,143,45,0,fin,0,39,0,0,0);
            lifecycle(mp,143,0,7,1,0,39,128,0,0);
        }
        no_space(mp,0); no_space(mp,1); fresh_priority(mp);
        for (int kind=0; kind<4; kind++) control(mp,kind);
        unlimited_control(mp);
        unlimited_slack(mp,143); unlimited_slack(mp,1500);
        for (int maximum=0; maximum<=1; maximum++) {
            for (int ack=0; ack<=1; ack++) {
                const uint64_t pn[]={0,15,1023,262144};
                for (size_t i=0; i<sizeof(pn)/sizeof(pn[0]); i++)
                    credit_edge(mp,pn[i],ack,maximum,39,143,45,0,0,0);
                credit_edge(mp,0,ack,maximum,80,143,1,0,0,0);
                credit_edge(mp,0,ack,maximum,300,100,1,0,0,0);
                credit_edge(mp,0,ack,maximum,0,143,0,1,0,0);
            }
            credit_edge(mp,0,0,maximum,39,143,45,1,1,0);
            credit_edge(mp,0,0,maximum,39,143,45,0,0,1);
        }
        credit_fresh_delivery(mp,640,2); credit_fresh_delivery(mp,566,1);
    }
    for (int source_index=0; source_index<2; source_index++)
        for (int maximum=0; maximum<2; maximum++) cross_path_credit(source_index,maximum);
    printf("STRICT PASS cases=%u authenticated-and-tamper-rejected=%u real peer byte/FIN once\n",cases,authenticated);
    return 0;
}
