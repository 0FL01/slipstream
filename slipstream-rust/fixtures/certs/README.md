# Test TLS certificates

Only public test certificates are retained in this monorepo. Private fixture
keys are intentionally excluded. Generate a temporary pair as described in
[the architecture guide](../../../docs/architecture.md#test-certificates), and
set `CERT_DIR` for interop/benchmark scripts. Do not overwrite Android's public pin.

Files:
- `cert.pem`
- `key.pem` (external temporary fixture only)
- `alt_cert.pem` (mismatched cert for pinning tests)
- `alt_key.pem` (external temporary fixture only)

To generate your own cert/key pair:

```
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout key.pem -out cert.pem -days 365 \
  -subj "/CN=slipstream"
```

Pass custom paths with `--cert` and `--key` when running `slipstream-server`.
For client verification, pass the pinned server certificate via `--cert`.
