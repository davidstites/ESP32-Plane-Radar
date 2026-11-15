#!/usr/bin/env python3
"""Check that an ESP-IDF x509 certificate bundle can validate a host's TLS chain.

Mirrors Arduino core 2.0.14 (mbedTLS 2.28 + esp_crt_bundle.c): the bundle is not
an mbedTLS trust store, so mbedTLS builds the chain only from the certificates the
server presents, searching for each certificate's parent among those sent after it
(x509_crt_find_parent_in). A parent must match the issuer name, be a CA, allow
certificate signing if it has keyUsage, and respect pathLenConstraint; one with a
bad signature is used only as a fallback, and the child is marked not trusted. The
verify callback then looks up the issuer of every not-trusted certificate (always
including the top of the chain) in the bundle (exact DER subject match) and checks
its signature with that entry's public key. A root that signed a lower certificate
does not help if the server also sends a cross-signed copy above it.

Not modeled: mbedTLS's case-insensitive name comparison and its self-issued
certificate allowance in path lengths. Connects with TLS 1.2, the device's only version.

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
    with open(path, "rb") as f:
        data = f.read()
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
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2
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


def can_sign_certificates(cert):
    """x509_crt_check_parent: basicConstraints CA, and keyCertSign if keyUsage is present."""
    try:
        if not cert.extensions.get_extension_for_class(x509.BasicConstraints).value.ca:
            return False
    except x509.ExtensionNotFound:
        return False
    try:
        return cert.extensions.get_extension_for_class(x509.KeyUsage).value.key_cert_sign
    except x509.ExtensionNotFound:
        return True


def path_length_allows(parent, path_cnt):
    """pathLenConstraint caps the CA certificates below parent (path_cnt = child's depth)."""
    constraints = parent.extensions.get_extension_for_class(x509.BasicConstraints).value
    path_length = constraints.path_length
    return path_length is None or path_cnt <= path_length


def find_parent(presented, child_index):
    """(parent index, signature good) among certificates after the child; (None, False) if none.

    The first name-matching CA with a bad signature is kept as a fallback, as mbedTLS does.
    """
    child = presented[child_index]
    fallback = None
    for i in range(child_index + 1, len(presented)):
        cand = presented[i]
        if cand.subject != child.issuer or not can_sign_certificates(cand):
            continue
        if not path_length_allows(cand, child_index):
            continue
        if signed_by(child, cand.public_key()):
            return i, True
        if fallback is None:
            fallback = i
    return fallback, False


def certs_needing_bundle(presented):
    """Certificates mbedTLS marks not trusted: the chain top plus children of fallback parents."""
    flagged = []
    index = 0
    while True:
        parent, good = find_parent(presented, index)
        if parent is None or not good:
            flagged.append(presented[index])
        if parent is None:
            return flagged
        index = parent


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


def verify(presented, entries):
    """(ok, flagged): ok when the bundle clears every certificate mbedTLS marks not trusted."""
    flagged = certs_needing_bundle(presented)
    for cert in flagged:
        key = device_lookup(entries, cert.issuer.public_bytes())
        if key is None or not signed_by(cert, load_der_public_key(key)):
            return False, flagged
    return True, flagged


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="opendata.adsb.fi")
    parser.add_argument("--bundle", default="data/x509_crt_bundle.bin")
    args = parser.parse_args()

    entries = load_bundle(args.bundle)
    presented = presented_chain(args.host)
    print(f"{args.bundle}: {len(entries)} certificates; {args.host} presented {len(presented)}")

    ok, flagged = verify(presented, entries)
    for cert in flagged:
        print(f"needs a bundle root: '{cert.subject.rfc4514_string()}', "
              f"issued by '{cert.issuer.rfc4514_string()}'")
    if ok:
        print("OK: each issuer is in the bundle and its key verifies the signature")
        return 0

    print("FAIL: the device would reject this chain (an issuer above is not a bundle "
          "root, or its signature does not verify)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
