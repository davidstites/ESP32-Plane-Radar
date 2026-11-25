"""Offline tests for deploy.py. Run: python3 -m unittest discover -s scripts/tests"""

import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import deploy  # noqa: E402
import ota_release as rel  # noqa: E402


class Headers(dict):
    def get(self, key, default=None):
        return super().get(key, default)


def ok(body, kind="application/octet-stream", length=True):
    h = Headers({"Content-Type": kind})
    if length:
        h["Content-Length"] = str(len(body))
    return 200, h, body


class ReleaseEnv(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = self.tmp.name
        self.key = os.path.join(self.dir, "key.pem")
        with contextlib.redirect_stdout(io.StringIO()):
            rel.main(["keygen", "--out", self.key])
        self.rel = os.path.join(self.dir, "release")
        os.makedirs(os.path.join(self.rel, "rc"))
        self.image = b"image bytes " * 50 + b"PLANERADAR-BUILD=8\0"
        with open(os.path.join(self.rel, "firmware-8.bin"), "wb") as f:
            f.write(self.image)
        self.sign("manifest.json", "prod")
        self.sign("rc/6ed9777f98c4a6e271181c6b16b87843.json", "rc", "--device-hash",
                  "6ed9777f98c4a6e271181c6b16b87843")

    def sign(self, out, channel, *extra):
        rel.main(["sign", "--key", self.key, "--firmware", os.path.join(self.rel, "firmware-8.bin"),
                  "--build", "8", "--min-build", "0", "--channel", channel, "--out",
                  os.path.join(self.rel, out)] + list(extra))

    def manifest(self):
        with open(os.path.join(self.rel, "manifest.json"), "rb") as f:
            return f.read()

    def live(self, **overrides):
        """What the server would return for a correct deployment, with per-URL overrides."""
        with open(os.path.join(self.rel, "rc/6ed9777f98c4a6e271181c6b16b87843.json"), "rb") as f:
            rc = f.read()
        table = {
            f"{deploy.FIRMWARE_URL}/manifest.json": ok(self.manifest(), "application/json"),
            f"{deploy.BASE_URL}{rel.DEFAULT_BASE}/firmware-8.bin": ok(self.image),
            f"{deploy.FIRMWARE_URL}/rc/6ed9777f98c4a6e271181c6b16b87843.json": ok(rc, "application/json"),
        }
        table.update(overrides)
        return lambda url: table.get(url, (404, Headers(), b""))


class ReleaseChecks(ReleaseEnv):
    def test_a_consistent_release_has_no_problems(self):
        self.assertEqual(deploy.problems_in_release(self.rel), [])

    def test_missing_manifest(self):
        os.remove(os.path.join(self.rel, "manifest.json"))
        self.assertIn("is missing", deploy.problems_in_release(self.rel)[0])

    def test_unreadable_manifest(self):
        with open(os.path.join(self.rel, "manifest.json"), "w") as f:
            f.write("{not json")
        self.assertIn("unreadable manifest", deploy.problems_in_release(self.rel)[0])

    def test_firmware_path_outside_the_base(self):
        m = json.loads(self.manifest())
        m["firmware"] = "/elsewhere/firmware-8.bin"
        with open(os.path.join(self.rel, "manifest.json"), "w") as f:
            json.dump(m, f)
        self.assertIn("outside", deploy.problems_in_release(self.rel)[0])

    def test_missing_image(self):
        os.remove(os.path.join(self.rel, "firmware-8.bin"))
        self.assertTrue(all("missing" in p for p in deploy.problems_in_release(self.rel)))

    def test_image_that_changed_after_signing(self):
        with open(os.path.join(self.rel, "firmware-8.bin"), "ab") as f:
            f.write(b"x")
        self.assertIn("does not match", deploy.problems_in_release(self.rel)[0])

    def test_a_release_without_an_rc_directory_is_fine(self):
        os.remove(os.path.join(self.rel, "rc/6ed9777f98c4a6e271181c6b16b87843.json"))
        os.rmdir(os.path.join(self.rel, "rc"))
        self.assertEqual(deploy.problems_in_release(self.rel), [])


class VerifyFirmware(ReleaseEnv):
    def run_verify(self, fetcher, key=None):
        with mock.patch.object(deploy, "fetch", fetcher):
            return deploy.verify_firmware(self.rel, key)

    def test_a_correct_deployment_verifies_with_and_without_the_key(self):
        self.assertEqual(self.run_verify(self.live()), [])
        self.assertEqual(self.run_verify(self.live(), self.key), [])

    def test_manifest_not_found(self):
        self.assertEqual(self.run_verify(lambda url: (404, Headers(), b"")), ["manifest.json: HTTP 404"])

    def test_manifest_unreadable(self):
        fetcher = self.live(**{f"{deploy.FIRMWARE_URL}/manifest.json": ok(b"nope")})
        self.assertIn("unreadable", self.run_verify(fetcher)[0])

    def test_image_missing_on_the_server(self):
        fetcher = self.live(**{f"{deploy.BASE_URL}{rel.DEFAULT_BASE}/firmware-8.bin": (404, Headers(), b"")})
        self.assertIn("HTTP 404", self.run_verify(fetcher)[0])

    def test_image_truncated_or_corrupt_or_without_length(self):
        url = f"{deploy.BASE_URL}{rel.DEFAULT_BASE}/firmware-8.bin"
        bad = self.run_verify(self.live(**{url: ok(self.image[:-1] + b"X")}))
        self.assertEqual(len(bad), 1)
        self.assertIn("sha256", bad[0])
        chunked = self.run_verify(self.live(**{url: ok(self.image, length=False)}))
        self.assertIn("Content-Length", chunked[0])

    def test_signature_that_does_not_verify(self):
        m = json.loads(self.manifest())
        m["build"] = 9
        fetcher = self.live(**{f"{deploy.FIRMWARE_URL}/manifest.json": ok(json.dumps(m).encode())})
        self.assertIn("signature", self.run_verify(fetcher, self.key)[0])

    def test_manifest_missing_signature_fields_fail_the_signature_check(self):
        m = json.loads(self.manifest())
        del m["signature"]
        fetcher = self.live(**{f"{deploy.FIRMWARE_URL}/manifest.json": ok(json.dumps(m).encode())})
        self.assertIn("signature", self.run_verify(fetcher, self.key)[0])

    def test_rc_manifest_missing_or_different_on_the_server(self):
        url = f"{deploy.FIRMWARE_URL}/rc/6ed9777f98c4a6e271181c6b16b87843.json"
        self.assertIn("rc/", self.run_verify(self.live(**{url: (404, Headers(), b"")}))[0])
        self.assertIn("rc/", self.run_verify(self.live(**{url: ok(b"{}")}))[0])


class VerifySetup(unittest.TestCase):
    def table(self, **overrides):
        t = {}
        for folder, _, names in os.walk(deploy.SETUP_SRC):
            for name in names:
                rel_path = os.path.relpath(os.path.join(folder, name), deploy.SETUP_SRC).replace(os.sep, "/")
                kind = deploy.CONTENT_TYPES.get(os.path.splitext(name)[1], "application/octet-stream")
                t[f"{deploy.SETUP_URL}/{rel_path}"] = ok(b"x", kind)
        t[deploy.SETUP_URL] = (301, Headers({"Location": "https://metarly.app/setup/"}), b"")
        t.update(overrides)
        return lambda url: t.get(url, (404, Headers(), b""))

    def run_verify(self, fetcher):
        with mock.patch.object(deploy, "fetch", fetcher):
            return deploy.verify_setup()

    def test_a_correct_deployment_verifies(self):
        self.assertEqual(self.run_verify(self.table()), [])

    def test_a_missing_file(self):
        failures = self.run_verify(self.table(**{f"{deploy.SETUP_URL}/app.js": (404, Headers(), b"")}))
        self.assertEqual(failures, ["app.js: HTTP 404"])

    def test_a_wrong_content_type(self):
        failures = self.run_verify(self.table(**{f"{deploy.SETUP_URL}/app.js": ok(b"x", "text/plain")}))
        self.assertIn("Content-Type", failures[0])

    def test_the_redirect_to_the_trailing_slash(self):
        failures = self.run_verify(self.table(**{deploy.SETUP_URL: ok(b"x", "text/html")}))
        self.assertIn("301", failures[0])


class Fetch(unittest.TestCase):
    def test_returns_status_headers_and_body(self):
        resp = mock.MagicMock()
        resp.__enter__.return_value = resp
        resp.status, resp.headers = 200, {"a": "b"}
        resp.read.return_value = b"body"
        opener = mock.MagicMock()
        opener.open.return_value = resp
        with mock.patch("urllib.request.build_opener", return_value=opener):
            self.assertEqual(deploy.fetch("https://x"), (200, {"a": "b"}, b"body"))

    def test_http_errors_are_returned_not_raised(self):
        err = deploy.urllib.error.HTTPError("https://x", 404, "nf", {"h": "v"}, io.BytesIO(b"gone"))
        opener = mock.MagicMock()
        opener.open.side_effect = err
        with mock.patch("urllib.request.build_opener", return_value=opener):
            self.assertEqual(deploy.fetch("https://x"), (404, {"h": "v"}, b"gone"))

    def test_an_unreachable_host_exits(self):
        opener = mock.MagicMock()
        opener.open.side_effect = deploy.urllib.error.URLError("dns")
        with mock.patch("urllib.request.build_opener", return_value=opener):
            with self.assertRaises(SystemExit):
                deploy.fetch("https://x")

    def test_redirects_are_not_followed(self):
        self.assertIsNone(deploy.NoRedirect().redirect_request(None, None, 301, "", {}, "https://y"))


class Commands(ReleaseEnv):
    def test_rsync_is_a_dry_run_without_yes_and_real_with_it(self):
        with mock.patch("subprocess.run") as run:
            run.return_value.returncode = 0
            deploy.rsync("a", "host:/b/", False)
            deploy.rsync("a", "host:/b/", True)
        dry, real = (c.args[0] for c in run.call_args_list)
        self.assertEqual(dry, ["rsync", "-av", "-n", "a/", "host:/b/"])
        self.assertEqual(real, ["rsync", "-av", "a/", "host:/b/"])

    def test_rsync_failure_exits(self):
        with mock.patch("subprocess.run") as run:
            run.return_value.returncode = 12
            with self.assertRaises(SystemExit):
                deploy.rsync("a", "host:/b/", True)

    def test_setup_dry_run_does_not_verify_and_yes_does(self):
        with mock.patch.object(deploy, "rsync") as rsync, mock.patch.object(deploy, "verify_setup") as ver, \
                contextlib.redirect_stdout(io.StringIO()):
            ver.return_value = []
            deploy.main(["setup"])
            ver.assert_not_called()
            deploy.main(["setup", "--yes"])
        self.assertEqual([c.args[2] for c in rsync.call_args_list], [False, True])
        ver.assert_called_once()

    def test_setup_that_fails_verification_exits(self):
        with mock.patch.object(deploy, "rsync"), mock.patch.object(deploy, "verify_setup") as ver, \
                contextlib.redirect_stdout(io.StringIO()):
            ver.return_value = ["app.js: HTTP 404"]
            with self.assertRaises(SystemExit):
                deploy.main(["setup", "--yes"])

    def test_firmware_refuses_a_bad_release_before_uploading(self):
        os.remove(os.path.join(self.rel, "firmware-8.bin"))
        with mock.patch.object(rel, "main") as publish:
            with self.assertRaises(SystemExit):
                deploy.main(["firmware", "--dir", self.rel])
        publish.assert_not_called()

    def test_firmware_publishes_then_verifies_only_with_yes(self):
        with mock.patch.object(rel, "main") as publish, mock.patch.object(deploy, "verify_firmware") as ver, \
                contextlib.redirect_stdout(io.StringIO()):
            ver.return_value = []
            deploy.main(["firmware", "--dir", self.rel])
            ver.assert_not_called()
            deploy.main(["firmware", "--dir", self.rel, "--yes", "--key", self.key])
        dry, real = (c.args[0] for c in publish.call_args_list)
        self.assertNotIn("--yes", dry)
        self.assertIn("--yes", real)
        self.assertEqual(real[real.index("--dest") + 1], deploy.FIRMWARE_DEST)
        ver.assert_called_once_with(self.rel, self.key)

    def test_verify_subcommands_route_to_the_right_check(self):
        with mock.patch.object(deploy, "verify_setup", return_value=[]) as vs, \
                mock.patch.object(deploy, "verify_firmware", return_value=[]) as vf, \
                contextlib.redirect_stdout(io.StringIO()):
            deploy.main(["verify", "setup"])
            deploy.main(["verify", "firmware", "--dir", self.rel])
        vs.assert_called_once()
        vf.assert_called_once_with(self.rel, None)

    def test_destinations_point_at_the_agreed_server_paths(self):
        self.assertEqual(deploy.SETUP_DEST, "dstites@metarly.app:/var/www/metarly_html/setup/")
        self.assertEqual(deploy.FIRMWARE_DEST, "dstites@metarly.app:/var/www/metarly_html/ota/planeradar/")
        self.assertEqual(deploy.FIRMWARE_URL, "https://metarly.app/ota/planeradar")


if __name__ == "__main__":
    unittest.main()
