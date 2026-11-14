/* Exercise the actual private picotls callbacks, including signature faults. */
#include "../src/slipstream_pin.c"
#include <assert.h>
#include <openssl/ec.h>

static void signatures(int type, uint16_t algo, const EVP_MD *md, int padding) {
    EVP_PKEY_CTX *key_ctx = EVP_PKEY_CTX_new_id(type, NULL);
    assert(key_ctx && EVP_PKEY_keygen_init(key_ctx) == 1);
    if (type == EVP_PKEY_RSA) assert(EVP_PKEY_CTX_set_rsa_keygen_bits(key_ctx, 2048) == 1);
    if (type == EVP_PKEY_EC) assert(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(key_ctx, NID_X9_62_prime256v1) == 1);
    EVP_PKEY *key = NULL;
    assert(EVP_PKEY_keygen(key_ctx, &key) == 1); EVP_PKEY_CTX_free(key_ctx);
    ss_verifier v = {0}; v.key = key;
    const uint8_t message[] = "synthetic CertificateVerify transcript";
    uint8_t signature[512]; size_t length = sizeof(signature);
    EVP_MD_CTX *ctx = EVP_MD_CTX_new(); EVP_PKEY_CTX *sign_ctx = NULL;
    assert(ctx && EVP_DigestSignInit(ctx, &sign_ctx, md, NULL, key) == 1);
    if (padding) assert(EVP_PKEY_CTX_set_rsa_padding(sign_ctx, padding) == 1);
    if (padding == RSA_PKCS1_PSS_PADDING) {
        assert(EVP_PKEY_CTX_set_rsa_pss_saltlen(sign_ctx, RSA_PSS_SALTLEN_DIGEST) == 1);
        assert(EVP_PKEY_CTX_set_rsa_mgf1_md(sign_ctx, md) == 1);
    }
    assert(EVP_DigestSign(ctx, signature, &length, message, sizeof(message)) == 1);
    EVP_MD_CTX_free(ctx);
    ptls_iovec_t data = ptls_iovec_init(message, sizeof(message));
    ptls_iovec_t sign = ptls_iovec_init(signature, length);
    assert(verify_sign(&v, algo, data, sign) == 0);
    signature[0] ^= 1;
    assert(verify_sign(&v, algo, data, sign) != 0);
    signature[0] ^= 1;
    assert(verify_sign(&v, algo, ptls_iovec_init(message, sizeof(message)-1), sign) != 0);
    assert(verify_sign(&v, 0xfffe, data, sign) != 0);
    assert(verify_sign(&v, algo, data, ptls_iovec_init(NULL, 1)) != 0);
    assert(verify_sign(&v, algo, ptls_iovec_init(NULL, 0), ptls_iovec_init(NULL, 0)) == 0);
    assert(verify_sign(NULL, algo, data, sign) != 0);

    X509 *cert = X509_new(); assert(cert);
    assert(X509_set_version(cert, 2) == 1 && X509_set_pubkey(cert, key) == 1);
    assert(ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) == 1);
    assert(X509_gmtime_adj(X509_getm_notBefore(cert), 0));
    assert(X509_gmtime_adj(X509_getm_notAfter(cert), 60));
    X509_NAME *name = X509_get_subject_name(cert);
    assert(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        (const unsigned char *)"synthetic", -1, -1, 0) == 1);
    assert(X509_set_issuer_name(cert, name) == 1);
    assert(X509_sign(cert, key, md) > 0);
    int der_len = i2d_X509(cert, &v.der); assert(der_len > 0);
    v.der_len = (size_t)der_len;
    ptls_iovec_t leaf = ptls_iovec_init(v.der, v.der_len);
    int (*callback)(void *, uint16_t, ptls_iovec_t, ptls_iovec_t) = NULL;
    void *callback_ctx = NULL;
    assert(verify_cert(&v.super, NULL, "ignored", &callback, &callback_ctx, &leaf, 1) == 0);
    assert(callback == verify_sign && callback_ctx == &v);
    assert(verify_cert(&v.super, NULL, "ignored", &callback, &callback_ctx, &leaf, 0) != 0);
    uint8_t *other = OPENSSL_malloc(v.der_len); assert(other);
    memcpy(other, v.der, v.der_len); other[v.der_len - 1] ^= 1;
    leaf = ptls_iovec_init(other, v.der_len);
    assert(verify_cert(&v.super, NULL, "ignored", &callback, &callback_ctx, &leaf, 1) != 0);
    OPENSSL_free(other); OPENSSL_free(v.der); X509_free(cert); EVP_PKEY_free(key);
}

int main(void) {
    signatures(EVP_PKEY_RSA, 0x0804, EVP_sha256(), RSA_PKCS1_PSS_PADDING);
    signatures(EVP_PKEY_RSA, 0x0401, EVP_sha256(), RSA_PKCS1_PADDING);
    signatures(EVP_PKEY_EC, 0x0403, EVP_sha256(), 0);
    signatures(EVP_PKEY_ED25519, 0x0807, NULL, 0);
    return 0;
}
