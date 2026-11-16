"""Unit tests for check_ca_bundle.py's model of mbedTLS chain building (no network).

Run: python3 -m unittest discover -s scripts/tests   (needs the cryptography package)
"""

import contextlib
import datetime
import io
import os
import struct
import sys
import tempfile
import unittest

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, ed25519, rsa
from cryptography.x509.oid import NameOID

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import check_ca_bundle as ccb  # noqa: E402


def name(cn):
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])


def new_key():
    return ec.generate_private_key(ec.SECP256R1())


def make_cert(subject, issuer, key, issuer_key, ca=True, path_length=None, key_usage=True,
              key_cert_sign=True):
    now = datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc)
    builder = (x509.CertificateBuilder()
               .subject_name(name(subject))
               .issuer_name(name(issuer))
               .public_key(key.public_key())
               .serial_number(x509.random_serial_number())
               .not_valid_before(now)
               .not_valid_after(now + datetime.timedelta(days=365))
               .add_extension(x509.BasicConstraints(ca=ca, path_length=path_length),
                              critical=True))
    if key_usage:
        builder = builder.add_extension(
            x509.KeyUsage(digital_signature=True, content_commitment=False,
                          key_encipherment=False, data_encipherment=False, key_agreement=False,
                          key_cert_sign=key_cert_sign, crl_sign=key_cert_sign,
                          encipher_only=False, decipher_only=False),
            critical=True)
    return builder.sign(issuer_key, hashes.SHA256())


def bundle_entries(*certs):
    """Bundle entries (subject DER, SPKI DER), sorted by subject as gen_crt_bundle.py does."""
    entries = [(c.subject.public_bytes(),
                c.public_key().public_bytes(serialization.Encoding.DER,
                                            serialization.PublicFormat.SubjectPublicKeyInfo))
               for c in certs]
    return sorted(entries)


class Pki:
    """root -> intermediate -> leaf, all valid."""

    def __init__(self):
        self.root_key, self.int_key, self.leaf_key = new_key(), new_key(), new_key()
        self.root = make_cert("Root", "Root", self.root_key, self.root_key)
        self.intermediate = make_cert("Int", "Root", self.int_key, self.root_key)
        self.leaf = make_cert("host", "Int", self.leaf_key, self.int_key, ca=False,
                              key_usage=False)


class LoadBundleTest(unittest.TestCase):
    def write(self, data):
        f = tempfile.NamedTemporaryFile(delete=False)
        f.write(data)
        f.close()
        self.addCleanup(os.unlink, f.name)
        return f.name

    def test_round_trip(self):
        data = struct.pack(">H", 1) + struct.pack(">HH", 2, 3) + b"ab" + b"xyz"
        self.assertEqual(ccb.load_bundle(self.write(data)), [(b"ab", b"xyz")])

    def test_trailing_bytes_rejected(self):
        with self.assertRaises(ValueError):
            ccb.load_bundle(self.write(struct.pack(">H", 0) + b"!"))


class FindParentTest(unittest.TestCase):
    def setUp(self):
        self.pki = Pki()

    def test_finds_valid_parent_later_in_chain(self):
        self.assertEqual(ccb.find_parent([self.pki.leaf, self.pki.intermediate], 0), (1, True))

    def test_ignores_parent_sent_before_child(self):
        # mbedTLS only searches child->next onward, so a parent listed earlier is invisible.
        presented = [self.pki.leaf, self.pki.root, self.pki.intermediate]
        self.assertEqual(ccb.find_parent(presented, 2), (None, False))

    def test_rejects_parent_without_ca_bit(self):
        not_ca = make_cert("Int", "Root", self.pki.int_key, self.pki.root_key, ca=False)
        self.assertEqual(ccb.find_parent([self.pki.leaf, not_ca], 0), (None, False))

    def test_rejects_parent_without_basic_constraints(self):
        builder = (x509.CertificateBuilder().subject_name(name("Int")).issuer_name(name("Root"))
                   .public_key(self.pki.int_key.public_key())
                   .serial_number(1)
                   .not_valid_before(datetime.datetime(2026, 1, 1))
                   .not_valid_after(datetime.datetime(2027, 1, 1)))
        bare = builder.sign(self.pki.root_key, hashes.SHA256())
        self.assertEqual(ccb.find_parent([self.pki.leaf, bare], 0), (None, False))

    def test_rejects_parent_whose_key_usage_lacks_cert_sign(self):
        no_sign = make_cert("Int", "Root", self.pki.int_key, self.pki.root_key,
                            key_cert_sign=False)
        self.assertEqual(ccb.find_parent([self.pki.leaf, no_sign], 0), (None, False))

    def test_accepts_ca_without_key_usage(self):
        no_ku = make_cert("Int", "Root", self.pki.int_key, self.pki.root_key, key_usage=False)
        self.assertEqual(ccb.find_parent([self.pki.leaf, no_ku], 0), (1, True))

    def test_path_length_limit(self):
        # Int2 (pathLen 0) may sign only end-entity certs, not Int.
        int2_key = new_key()
        int2 = make_cert("Int2", "Root", int2_key, self.pki.root_key, path_length=0)
        intermediate = make_cert("Int", "Int2", self.pki.int_key, int2_key)
        presented = [self.pki.leaf, intermediate, int2]
        self.assertEqual(ccb.find_parent(presented, 1), (None, False))
        int2_ok = make_cert("Int2", "Root", int2_key, self.pki.root_key, path_length=1)
        self.assertEqual(ccb.find_parent([self.pki.leaf, intermediate, int2_ok], 1), (2, True))

    def test_bad_signature_parent_is_fallback(self):
        impostor = make_cert("Int", "Root", new_key(), self.pki.root_key)
        self.assertEqual(ccb.find_parent([self.pki.leaf, impostor], 0), (1, False))

    def test_good_signature_preferred_over_earlier_fallback(self):
        impostor = make_cert("Int", "Root", new_key(), self.pki.root_key)
        presented = [self.pki.leaf, impostor, self.pki.intermediate]
        self.assertEqual(ccb.find_parent(presented, 0), (2, True))


class CertsNeedingBundleTest(unittest.TestCase):
    def setUp(self):
        self.pki = Pki()

    def test_top_of_chain_only(self):
        presented = [self.pki.leaf, self.pki.intermediate]
        self.assertEqual(ccb.certs_needing_bundle(presented), [self.pki.intermediate])

    def test_bad_signature_link_is_also_flagged(self):
        impostor = make_cert("Int", "Root", new_key(), self.pki.root_key)
        presented = [self.pki.leaf, impostor]
        self.assertEqual(ccb.certs_needing_bundle(presented), [self.pki.leaf, impostor])


class VerifyTest(unittest.TestCase):
    def setUp(self):
        self.pki = Pki()

    def test_passes_when_root_in_bundle(self):
        ok, flagged = ccb.verify([self.pki.leaf, self.pki.intermediate],
                                 bundle_entries(self.pki.root))
        self.assertTrue(ok)
        self.assertEqual(flagged, [self.pki.intermediate])

    def test_fails_when_root_missing(self):
        ok, _ = ccb.verify([self.pki.leaf, self.pki.intermediate],
                           bundle_entries(make_cert("Other", "Other", new_key(), new_key())))
        self.assertFalse(ok)

    def test_fails_when_bundle_key_does_not_verify(self):
        wrong_key = new_key()
        fake_root = make_cert("Root", "Root", wrong_key, wrong_key)
        ok, _ = ccb.verify([self.pki.leaf, self.pki.intermediate], bundle_entries(fake_root))
        self.assertFalse(ok)

    def test_bad_signature_link_fails_even_with_root_in_bundle(self):
        impostor = make_cert("Int", "Root", new_key(), self.pki.root_key)
        ok, _ = ccb.verify([self.pki.leaf, impostor], bundle_entries(self.pki.root))
        self.assertFalse(ok)

    def test_cross_signed_root_needs_its_signer(self):
        # adsb.fi's shape: leaf <- WE1 <- GTS Root R4 cross-signed by GlobalSign R1. Having
        # GTS Root R4 itself in the bundle does not help; GlobalSign R1 must be there.
        r1_key, r4_key, we1_key, leaf_key = new_key(), new_key(), new_key(), new_key()
        r1 = make_cert("GlobalSign R1", "GlobalSign R1", r1_key, r1_key)
        r4_self = make_cert("GTS Root R4", "GTS Root R4", r4_key, r4_key)
        r4_cross = make_cert("GTS Root R4", "GlobalSign R1", r4_key, r1_key)
        we1 = make_cert("WE1", "GTS Root R4", we1_key, r4_key)
        leaf = make_cert("opendata.adsb.fi", "WE1", leaf_key, we1_key, ca=False,
                         key_usage=False)
        presented = [leaf, we1, r4_cross]
        self.assertFalse(ccb.verify(presented, bundle_entries(r4_self))[0])
        self.assertTrue(ccb.verify(presented, bundle_entries(r4_self, r1))[0])


class SignedByTest(unittest.TestCase):
    def test_rsa_signature(self):
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        cert = make_cert("RSA Root", "RSA Root", key, key)
        self.assertTrue(ccb.signed_by(cert, key.public_key()))
        other = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        self.assertFalse(ccb.signed_by(cert, other.public_key()))

    def test_unsupported_key_type(self):
        # mbedTLS 2.28 on the device has no Ed25519 certificate support.
        key = new_key()
        cert = make_cert("Root", "Root", key, key)
        self.assertFalse(ccb.signed_by(cert, ed25519.Ed25519PrivateKey.generate().public_key()))


class MainTest(unittest.TestCase):
    def run_main(self, bundle_certs):
        pki = Pki()
        entries = bundle_entries(*bundle_certs(pki))
        data = struct.pack(">H", len(entries)) + b"".join(
            struct.pack(">HH", len(s), len(k)) + s + k for s, k in entries)
        f = tempfile.NamedTemporaryFile(delete=False)
        f.write(data)
        f.close()
        self.addCleanup(os.unlink, f.name)

        saved = ccb.presented_chain, sys.argv
        self.addCleanup(lambda: (setattr(ccb, "presented_chain", saved[0]),
                                 setattr(sys, "argv", saved[1])))
        ccb.presented_chain = lambda host: [pki.leaf, pki.intermediate]
        sys.argv = ["check_ca_bundle.py", "--bundle", f.name]
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = ccb.main()
        return code, out.getvalue()

    def test_ok(self):
        code, out = self.run_main(lambda pki: [pki.root])
        self.assertEqual(code, 0)
        self.assertIn("needs a bundle root: 'CN=Int', issued by 'CN=Root'", out)
        self.assertIn("OK:", out)

    def test_fail(self):
        code, out = self.run_main(lambda pki: [])
        self.assertEqual(code, 1)
        self.assertIn("FAIL:", out)


class DeviceLookupTest(unittest.TestCase):
    def test_finds_each_entry_by_binary_search(self):
        roots = [make_cert(f"Root {i}", f"Root {i}", k, k) for i, k in
                 enumerate(new_key() for _ in range(5))]
        entries = bundle_entries(*roots)
        for subject, key in entries:
            self.assertEqual(ccb.device_lookup(entries, subject), key)
        self.assertIsNone(ccb.device_lookup(entries, name("Missing").public_bytes()))


if __name__ == "__main__":
    unittest.main()
