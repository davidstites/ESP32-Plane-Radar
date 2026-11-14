#!/usr/bin/env python3
"""Check that an ESP-IDF x509 certificate bundle can validate a host's TLS chain.

Mirrors Arduino core 2.0.14 (mbedTLS 2.28 + esp_crt_bundle.c): the bundle is not
an mbedTLS trust store, so mbedTLS links the leaf to parents found only among the
certificates the server presents. The verify callback then looks up the issuer of
the top of that chain in the bundle (exact DER subject match) and checks its
signature with the bundle entry's public key (found by the same binary search). A root that signed a lower
certificate does not help if the server also sends a cross-signed copy above it.

Usage: check_ca_bundle.py [--host opendata.adsb.fi] [--bundle data/x509_crt_bundle.bin]
Requires Python >= 3.13 (ssl get_unverified_chain) and the cryptography package.
"""

import argparse
import socket
import ssl
import struct
import sys

from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa
from cryptography.hazmat.primitives.serialization import load_der_public_key


def load_bundle(path):
    data = open(path, "rb").read()
    (count,) = struct.unpack_from(">H", data, 0)
    pos = 2
    entries = []
    for _ in range(count):
        name_len, key_len = struct.unpack_from(">HH", data, pos)
        pos += 4
        subject = data[pos:pos + name_len]
        pos += name_len
        key = data[pos:pos + key_len]
        pos += key_len
        entries.append((subject, key))
    if pos != len(data):
        raise ValueError(f"bundle has {len(data) - pos} trailing bytes; format mismatch")
    return entries


def presented_chain(host):
    ctx = ssl.create_default_context()
    with socket.create_connection((host, 443), timeout=10) as sock:
        with ctx.wrap_socket(sock, server_hostname=host) as tls:
            return [x509.load_der_x509_certificate(c) for c in tls.get_unverified_chain()]


def signed_by(cert, public_key):
    try:
        if isinstance(public_key, rsa.RSAPublicKey):
            public_key.verify(cert.signature, cert.tbs_certificate_bytes, padding.PKCS1v15(),
                              cert.signature_hash_algorithm)
        elif isinstance(public_key, ec.EllipticCurvePublicKey):
            public_key.verify(cert.signature, cert.tbs_certificate_bytes,
                              ec.ECDSA(cert.signature_hash_algorithm))
        else:
            return False
    except Exception:
        return False
    return True


def chain_top(presented):
    """Follow parents among the presented certificates, as mbedTLS does with no trust store."""
    top = presented[0]
    used = {0}
    while True:
        for i, cand in enumerate(presented):
            if i in used or cand.subject != top.issuer:
                continue
            if signed_by(top, cand.public_key()):
                top = cand
                used.add(i)
                break
        else:
            return top


def device_lookup(entries, issuer):
    """esp_crt_verify_callback's binary search: memcmp over the entry's name length."""
    start, end = 0, len(entries) - 1
    middle = (end - start) // 2
    while start <= end:
        subject, key = entries[middle]
        probe = issuer[:len(subject)]
        if probe == subject:
            return key
        if probe < subject:
            end = middle - 1
        else:
            start = middle + 1
        middle = (start + end) // 2
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="opendata.adsb.fi")
    parser.add_argument("--bundle", default="data/x509_crt_bundle.bin")
    args = parser.parse_args()

    entries = load_bundle(args.bundle)
    presented = presented_chain(args.host)
    print(f"{args.bundle}: {len(entries)} certificates; {args.host} presented {len(presented)}")

    top = chain_top(presented)
    issuer = top.issuer.public_bytes()
    print(f"top of presented chain: '{top.subject.rfc4514_string()}', "
          f"issued by '{top.issuer.rfc4514_string()}'")
    key = device_lookup(entries, issuer)
    if key is not None and signed_by(top, load_der_public_key(key)):
        print("OK: issuer is in the bundle and its key verifies the signature")
        return 0

    print("FAIL: the device would reject this chain (issuer of the top certificate "
          "is not a bundle root, or its signature does not verify)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
