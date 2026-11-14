#!/usr/bin/env bash
# Regenerate data/x509_crt_bundle.bin from Mozilla's CA list (curl.se cacert.pem)
# using ESP-IDF v4.4.6's gen_crt_bundle.py — the IDF line Arduino core 2.0.14
# (espressif32@6.5.0) is built on, so the format matches its esp_crt_bundle.c.
# Roots in data/ca_extra/*.pem are added on top; see data/ca_extra/README.md.
set -euo pipefail
cd "$(dirname "$0")/.."

IDF_TAG="v4.4.6"
GEN_URL="https://raw.githubusercontent.com/espressif/esp-idf/${IDF_TAG}/components/mbedtls/esp_crt_bundle/gen_crt_bundle.py"
PEM_URL="https://curl.se/ca/cacert.pem"
OUT="data/x509_crt_bundle.bin"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

curl -sfL "$GEN_URL" -o "$work/gen_crt_bundle.py"
curl -sfL "$PEM_URL" -o "$work/cacert.pem"
python3 -m venv "$work/venv"
"$work/venv/bin/pip" install --quiet --disable-pip-version-check cryptography

cp data/ca_extra/*.pem "$work/"
(cd "$work" && venv/bin/python -I gen_crt_bundle.py --quiet --input ./*.pem)
mv "$work/x509_crt_bundle" "$OUT"

count=$(python3 -c "import sys; d=open(sys.argv[1],'rb').read(); print(int.from_bytes(d[:2],'big'))" "$OUT")
echo "Wrote $OUT: $count certificates, $(wc -c < "$OUT" | tr -d ' ') bytes"
echo "Source: $PEM_URL ($(grep -m1 '^## Certificate data from Mozilla as of' "$work/cacert.pem" | sed 's/^## //'))"
