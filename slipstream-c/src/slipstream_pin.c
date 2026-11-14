#include "slipstream_runtime.h"
#include <picotls.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <string.h>

typedef struct {
    ptls_verify_certificate_t super;
    unsigned char *der;
    size_t der_len;
    EVP_PKEY *key;
} ss_verifier;

static const uint16_t algorithms[] = {
    0x0807, 0x0808, 0x0403, 0x0503, 0x0603,
    0x0804, 0x0805, 0x0806, 0x0809, 0x080a, 0x080b,
    0x0401, 0x0501, 0x0601, 0xffff
};

static int verify_sign(void *ctx, uint16_t algo, ptls_iovec_t data, ptls_iovec_t sign) {
    ss_verifier *v = ctx;
    if (!v) return -1;
    /* picotls invokes the callback with empty vectors to release per-handshake state. */
    if (!data.base && !data.len && !sign.base && !sign.len) return 0;
    if (!data.base || !sign.base) return -1;
    int key_type = EVP_PKEY_base_id(v->key), padding = 0;
    const EVP_MD *md = NULL;
    switch (algo) {
    case 0x0807: if (key_type != EVP_PKEY_ED25519) return -1; break;
    case 0x0808: if (key_type != EVP_PKEY_ED448) return -1; break;
    case 0x0403: case 0x0503: case 0x0603:
        if (key_type != EVP_PKEY_EC) return -1;
        md = algo == 0x0403 ? EVP_sha256() : algo == 0x0503 ? EVP_sha384() : EVP_sha512();
        break;
    case 0x0401: case 0x0501: case 0x0601:
        if (key_type != EVP_PKEY_RSA) return -1;
        padding = RSA_PKCS1_PADDING;
        md = algo == 0x0401 ? EVP_sha256() : algo == 0x0501 ? EVP_sha384() : EVP_sha512();
        break;
    case 0x0804: case 0x0805: case 0x0806: case 0x0809: case 0x080a: case 0x080b:
        if (key_type != EVP_PKEY_RSA && key_type != EVP_PKEY_RSA_PSS) return -1;
        padding = RSA_PKCS1_PSS_PADDING;
        md = (algo == 0x0804 || algo == 0x0809) ? EVP_sha256() :
             (algo == 0x0805 || algo == 0x080a) ? EVP_sha384() : EVP_sha512();
        break;
    default: return -1;
    }
    EVP_MD_CTX *m = EVP_MD_CTX_new();
    EVP_PKEY_CTX *k = NULL;
    int ok = m && EVP_DigestVerifyInit(m, &k, md, NULL, v->key) == 1;
    if (ok && padding) ok = EVP_PKEY_CTX_set_rsa_padding(k, padding) == 1;
    if (ok && padding == RSA_PKCS1_PSS_PADDING)
        ok = EVP_PKEY_CTX_set_rsa_pss_saltlen(k, RSA_PSS_SALTLEN_DIGEST) == 1 &&
             EVP_PKEY_CTX_set_rsa_mgf1_md(k, md) == 1;
    if (ok) ok = EVP_DigestVerify(m, sign.base, sign.len, data.base, data.len) == 1;
    EVP_MD_CTX_free(m);
    return ok ? 0 : -1;
}

static int verify_cert(ptls_verify_certificate_t *self, ptls_t *tls, const char *name,
    int (**sign_cb)(void *, uint16_t, ptls_iovec_t, ptls_iovec_t), void **sign_ctx,
    ptls_iovec_t *certs, size_t count) {
    (void)tls; (void)name;
    ss_verifier *v = (ss_verifier *)self;
    if (!count || !certs || certs[0].len != v->der_len ||
        !certs[0].base || memcmp(certs[0].base, v->der, v->der_len)) return -1;
    *sign_cb = verify_sign;
    *sign_ctx = v;
    return 0;
}

static void verifier_free(ptls_verify_certificate_t *self) {
    ss_verifier *v = (ss_verifier *)self;
    EVP_PKEY_free(v->key);
    OPENSSL_free(v->der);
    free(v);
}

int ss_pin_certificate(picoquic_quic_t *quic, const char *path) {
    BIO *bio = BIO_new_file(path, "r");
    if (!bio) return -1;
    X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    X509 *extra = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!cert || extra) { X509_free(cert); X509_free(extra); return -1; }
    ss_verifier *v = calloc(1, sizeof(*v));
    if (!v) { X509_free(cert); return -1; }
    v->key = X509_get_pubkey(cert);
    int n = i2d_X509(cert, &v->der);
    X509_free(cert);
    if (n <= 0 || !v->key) { verifier_free(&v->super); return -1; }
    v->der_len = (size_t)n;
    v->super.cb = verify_cert;
    v->super.algos = algorithms;
    picoquic_set_verify_certificate_callback(quic, &v->super, verifier_free);
    return 0;
}
