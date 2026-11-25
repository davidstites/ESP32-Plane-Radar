"""Offline tests for ota_release.py. Run: python3 -m unittest discover -s scripts/tests"""

import base64
import contextlib
import hashlib
import io
import json
import os
import re
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import make_ota_test_vector as vec  # noqa: E402
import ota_release as rel  # noqa: E402

HASH = "6ed9777f98c4a6e271181c6b16b87843"
OTHER = "0123456789abcdef0123456789abcdef"


class Env(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = self.tmp.name
        self.key = os.path.join(self.dir, "release.pem")
        with contextlib.redirect_stdout(io.StringIO()):
            rel.main(["keygen", "--out", self.key])
        self.fw = os.path.join(self.dir, "fw.bin")
        with open(self.fw, "wb") as f:
            f.write(b"firmware bytes " * 100 + b"PLANERADAR-BUILD=8\0")
        with open(self.fw, "rb") as f:
            self.sha = hashlib.sha256(f.read()).digest()

    def path(self, name):
        return os.path.join(self.dir, name)

    def sign(self, *extra, out="m.json", channel="prod", build="8"):
        args = ["sign", "--key", self.key, "--firmware", self.fw, "--build", build, "--min-build", "5",
                "--channel", channel, "--out", self.path(out)]
        rel.main(args + list(extra))
        with open(self.path(out)) as f:
            return json.load(f)

    def public(self):
        with open(self.key, "rb") as f:
            k = serialization.load_pem_private_key(f.read(), None)
        return k.public_key()


class KeygenTests(Env):
    def test_refuses_to_overwrite_and_is_mode_0600(self):
        self.assertEqual(stat.S_IMODE(os.stat(self.key).st_mode), 0o600)
        with open(self.key, "rb") as f:
            before = f.read()
        with self.assertRaises(SystemExit):
            rel.main(["keygen", "--out", self.key])
        with open(self.key, "rb") as f:
            self.assertEqual(f.read(), before)

    def test_prints_the_public_key_as_a_c_array(self):
        out = self.path("second.pem")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rel.main(["keygen", "--out", out])
        text = buf.getvalue()
        raw = re.findall(r"0x([0-9a-f]{2})", text)
        self.assertEqual(len(raw), 32)
        with open(out, "rb") as f:
            pub = serialization.load_pem_private_key(f.read(), None).public_key().public_bytes(
                serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        self.assertEqual(bytes.fromhex("".join(raw)), pub)


class SignTests(Env):
    def test_signed_payload_matches_declared_layout(self):
        m = self.sign()
        payload = (b"PLANERADAR-OTA-v1\0" + self.sha + struct.pack(">II", 8, 5) + b"\x01" + bytes(32))
        self.assertEqual(len(payload), 91)
        self.public().verify(base64.b64decode(m["signature"]), payload)  # raises if wrong
        self.assertEqual(m["build"], 8)
        self.assertEqual(m["min_build"], 5)
        self.assertEqual(m["sha256"], self.sha.hex())
        self.assertEqual(m["channel"], "prod")

    def test_rc_manifest_binds_device_hash(self):
        m = self.sign("--device-hash", HASH, channel="rc")
        good = (b"PLANERADAR-OTA-v1\0" + self.sha + struct.pack(">II", 8, 5) + b"\x02"
                + bytes.fromhex(HASH) + bytes(16))
        self.public().verify(base64.b64decode(m["signature"]), good)
        other = good[:59] + bytes.fromhex(OTHER) + bytes(16)
        with self.assertRaises(Exception):
            self.public().verify(base64.b64decode(m["signature"]), other)

    def test_rc_requires_a_device_hash_and_prod_forbids_one(self):
        with self.assertRaises(SystemExit):
            self.sign(channel="rc")
        with self.assertRaises(SystemExit):
            self.sign("--device-hash", HASH, channel="prod")
        with self.assertRaises(SystemExit):
            self.sign("--device-hash", "XYZ", channel="rc")

    def test_firmware_path_defaults_to_the_image_name_under_the_base(self):
        m = self.sign()
        self.assertEqual(m["firmware"], "/planeradar/fw.bin")
        m = self.sign("--firmware-path", "/planeradar/firmware-8.bin")
        self.assertEqual(m["firmware"], "/planeradar/firmware-8.bin")

    def test_rollout_and_candidates_are_unsigned_extras(self):
        m = self.sign("--rollout", "25", "--candidate", HASH, "--candidate", OTHER)
        self.assertEqual(m["rollout"], 25)
        self.assertEqual(m["candidates"], [HASH, OTHER])
        base = self.sign()
        self.assertEqual(m["signature"], base["signature"])  # extras do not change the signature
        self.assertNotIn("rollout", base)

    def test_rollout_must_be_0_to_100(self):
        for bad in ("101", "-1"):
            with self.assertRaises(SystemExit):
                self.sign("--rollout", bad)

    def test_candidate_list_rejects_more_than_eight_and_bad_hex(self):
        nine = []
        for i in range(9):
            nine += ["--candidate", f"{i:032x}"]
        with self.assertRaises(SystemExit):
            self.sign(*nine)
        for bad in ("ABCDEF0123456789abcdef0123456789", "abc", "g" * 32):
            with self.assertRaises(SystemExit):
                self.sign("--candidate", bad)

    def test_manifest_stays_under_cap_with_eight_candidates(self):
        eight = []
        for i in range(8):
            eight += ["--candidate", f"{i:032x}"]
        self.sign(*eight, "--rollout", "100", "--firmware-path", "/" + "a" * 95)
        self.assertLessEqual(os.path.getsize(self.path("m.json")), 1024)

    def test_build_range_and_min_build(self):
        with self.assertRaises(SystemExit):
            self.sign(build="1000000")
        with self.assertRaises(SystemExit):
            rel.main(["sign", "--key", self.key, "--firmware", self.fw, "--build", "4", "--min-build", "5",
                      "--channel", "prod", "--out", self.path("x.json")])


class BuildMarkerTests(Env):
    def test_refuses_a_build_number_the_image_does_not_report(self):
        with self.assertRaises(SystemExit):
            self.sign(build="9")

    def test_refuses_an_image_without_a_build_marker(self):
        with open(self.fw, "wb") as f:
            f.write(b"firmware bytes " * 100)
        with self.assertRaises(SystemExit):
            self.sign()

    def test_refuses_an_image_with_conflicting_markers(self):
        with open(self.fw, "ab") as f:
            f.write(b"PLANERADAR-BUILD=9\0")
        with self.assertRaises(SystemExit):
            self.sign()


class PromoteTests(Env):
    def promote(self, firmware=None, rc="rc.json", out="prod.json"):
        rel.main(["promote", "--key", self.key, "--rc-manifest", self.path(rc), "--firmware",
                  firmware or self.fw, "--out", self.path(out)])
        with open(self.path(out)) as f:
            return json.load(f)

    def test_keeps_sha_and_changes_channel_and_signature(self):
        rc = self.sign("--device-hash", HASH, channel="rc", out="rc.json")
        prod = self.promote()
        self.assertEqual(prod["sha256"], rc["sha256"])
        self.assertEqual(prod["build"], rc["build"])
        self.assertEqual(prod["min_build"], rc["min_build"])
        self.assertEqual(prod["channel"], "prod")
        self.assertNotEqual(prod["signature"], rc["signature"])
        self.assertEqual(prod["signature"], self.sign(out="direct.json")["signature"])
        self.assertNotIn("candidates", prod)

    def test_refuses_a_different_firmware(self):
        self.sign("--device-hash", HASH, channel="rc", out="rc.json")
        other = self.path("other.bin")
        with open(other, "wb") as f:
            f.write(b"different")
        with self.assertRaises(SystemExit):
            self.promote(firmware=other)
        self.assertFalse(os.path.exists(self.path("prod.json")))

    def test_refuses_a_prod_manifest_as_input(self):
        self.sign(out="rc.json")  # actually prod
        with self.assertRaises(SystemExit):
            self.promote()


class PublishTests(Env):
    def test_without_yes_does_not_run_rsync(self):
        with mock.patch("subprocess.run") as run, contextlib.redirect_stdout(io.StringIO()):
            rel.main(["publish", "--dest", "host:/srv/planeradar", "--dir", self.dir])
        run.assert_not_called()

    def test_with_yes_uploads_images_before_manifests(self):
        with mock.patch("subprocess.run") as run:
            run.return_value.returncode = 0
            rel.main(["publish", "--dest", "host:/srv/planeradar", "--dir", self.dir, "--yes"])
        self.assertEqual(run.call_count, 2)
        images, manifests = (c.args[0] for c in run.call_args_list)
        for argv in (images, manifests):
            self.assertEqual(argv[0], "rsync")
            self.assertEqual(argv[-1], "host:/srv/planeradar/")
            self.assertTrue(argv[-2].endswith("/"))
        self.assertIn("--exclude=*.json", images)
        self.assertIn("--include=*.json", manifests)
        self.assertIn("--exclude=*", manifests)

    def test_rsync_failure_exits_nonzero(self):
        with mock.patch("subprocess.run") as run:
            run.return_value.returncode = 23
            with self.assertRaises(SystemExit):
                rel.main(["publish", "--dest", "h:/p", "--dir", self.dir, "--yes"])
        run.assert_called_once()  # a failed image upload never reaches the manifests


class CoreVectorTests(unittest.TestCase):
    def test_release_tool_matches_the_core_vector(self):
        signer = vec.key(b"planeradar ota test key")
        sha = hashlib.sha256(b"planeradar test firmware image").digest()
        payload = rel.build_payload(sha, 8, 5, "prod", None)
        self.assertEqual(vec.b64sig(signer, payload), vec.b64sig(signer, vec.payload(sha, 8, 5, 0x01, bytes(32))))
        device_id = hashlib.sha256(vec.DEVID_PREFIX + bytes.fromhex("a1b2c3d4e5f6")).digest()
        rc = rel.build_payload(sha, 8, 5, "rc", device_id[:16].hex())
        self.assertEqual(rc, vec.payload(sha, 8, 5, 0x02, vec.rc_id(device_id)))


if __name__ == "__main__":
    unittest.main()
