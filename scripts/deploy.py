#!/usr/bin/env python3
"""Deploy the Plane Radar web assets to metarly.app and check the result.

  setup     rsync site/setup/ to /var/www/metarly_html/setup/ (the QR/setup page)
  firmware  rsync a release directory to /var/www/metarly_html/ota/planeradar/ (manifests and images)
  verify    fetch what is live and check it: setup (every file, content types, redirect) or
            firmware (manifest, image length and sha256, optional signature)

Both deploy commands are a dry run (rsync -n) unless given --yes, and with --yes they verify
the live site afterwards. Nothing is ever deleted on the server. `firmware` refuses a release
directory whose manifests do not match the images beside them; signing and the upload order
(images before manifests) live in ota_release.py.
"""

import argparse
import base64
import hashlib
import json
import os
import posixpath
import subprocess
import sys
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ota_release as rel  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SSH_HOST = "dstites@metarly.app"
WEB_ROOT = "/var/www/metarly_html"
BASE_URL = "https://metarly.app"
SETUP_SRC = os.path.join(ROOT, "site", "setup")
SETUP_DEST = f"{SSH_HOST}:{WEB_ROOT}/setup/"
SETUP_URL = f"{BASE_URL}/setup"
FIRMWARE_DEST = f"{SSH_HOST}:{WEB_ROOT}{rel.DEFAULT_BASE}/"
FIRMWARE_URL = f"{BASE_URL}{rel.DEFAULT_BASE}"

CONTENT_TYPES = {
    ".html": "text/html",
    ".js": "javascript",
    ".css": "text/css",
    ".json": "json",
}


def die(message):
    sys.exit(f"deploy: {message}")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def fetch(url):
    """GET without following redirects. Returns (status, headers, body bytes)."""
    opener = urllib.request.build_opener(NoRedirect)
    try:
        with opener.open(url, timeout=30) as resp:
            return resp.status, resp.headers, resp.read()
    except urllib.error.HTTPError as err:
        return err.code, err.headers, err.read()
    except urllib.error.URLError as err:
        die(f"cannot reach {url}: {err.reason}")


def rsync(src, dest, yes):
    argv = ["rsync", "-av"] + ([] if yes else ["-n"]) + [src.rstrip("/") + "/", dest]
    if subprocess.run(argv).returncode != 0:
        die("rsync failed")


def problems_in_release(directory):
    """Why this release directory must not be published, as a list of messages."""
    found = []
    manifests = [os.path.join(directory, "manifest.json")]
    rc_dir = os.path.join(directory, "rc")
    if os.path.isdir(rc_dir):
        manifests += [os.path.join(rc_dir, n) for n in sorted(os.listdir(rc_dir)) if n.endswith(".json")]
    if not os.path.exists(manifests[0]):
        return [f"{manifests[0]} is missing"]
    prefix = rel.DEFAULT_BASE + "/"
    for path in manifests:
        try:
            with open(path) as f:
                m = json.load(f)
            firmware = m["firmware"]
            sha = m["sha256"]
        except (OSError, ValueError, KeyError) as err:
            found.append(f"{path}: unreadable manifest ({err})")
            continue
        if not firmware.startswith(prefix):
            found.append(f"{path}: firmware path {firmware} is outside {prefix}")
            continue
        image = os.path.join(directory, firmware[len(prefix):])
        if not os.path.exists(image):
            found.append(f"{path}: image {image} is missing")
        elif rel.file_sha(image).hex() != sha:
            found.append(f"{path}: {image} does not match the manifest's sha256")
    return found


def verify_setup():
    failures = []
    for folder, _, names in os.walk(SETUP_SRC):
        for name in sorted(names):
            local = os.path.join(folder, name)
            rel_path = os.path.relpath(local, SETUP_SRC).replace(os.sep, "/")
            status, headers, _ = fetch(f"{SETUP_URL}/{rel_path}")
            wanted = CONTENT_TYPES.get(os.path.splitext(name)[1])
            kind = headers.get("Content-Type", "")
            if status != 200:
                failures.append(f"{rel_path}: HTTP {status}")
            elif wanted and wanted not in kind:
                failures.append(f"{rel_path}: Content-Type {kind!r}, expected {wanted}")
    status, headers, _ = fetch(SETUP_URL)
    if status != 301 or not (headers.get("Location") or "").rstrip().endswith("/setup/"):
        failures.append(f"/setup: expected a 301 to /setup/, got {status}")
    return failures


def verify_firmware(directory, key_path=None):
    failures = []
    status, _, body = fetch(f"{FIRMWARE_URL}/manifest.json")
    if status != 200:
        return [f"manifest.json: HTTP {status}"]
    try:
        m = json.loads(body)
        sha = bytes.fromhex(m["sha256"])
        firmware = m["firmware"]
    except (ValueError, KeyError) as err:
        return [f"manifest.json: unreadable ({err})"]
    status, headers, image = fetch(BASE_URL + firmware)
    if status != 200:
        failures.append(f"{firmware}: HTTP {status}")
    else:
        if headers.get("Content-Length") != str(len(image)):
            failures.append(f"{firmware}: Content-Length {headers.get('Content-Length')} != {len(image)} bytes")
        if hashlib.sha256(image).digest() != sha:
            failures.append(f"{firmware}: sha256 does not match the manifest")
    if key_path:
        try:
            payload = rel.build_payload(sha, m["build"], m["min_build"], m["channel"], None)
            rel.load_key(key_path).public_key().verify(base64.b64decode(m["signature"], validate=True), payload)
        except (rel.InvalidSignature, ValueError, KeyError):
            failures.append("manifest.json: signature does not verify against --key")
    rc_dir = os.path.join(directory, "rc")
    for name in sorted(os.listdir(rc_dir)) if os.path.isdir(rc_dir) else []:
        with open(os.path.join(rc_dir, name), "rb") as f:
            local = f.read()
        status, _, live = fetch(f"{FIRMWARE_URL}/rc/{name}")
        if status != 200 or live != local:
            failures.append(f"rc/{name}: live copy is missing or differs (HTTP {status})")
    return failures


def report(failures, what):
    if failures:
        for line in failures:
            print(f"  FAIL {line}")
        die(f"{what} verification failed")
    print(f"{what}: verified")


def cmd_setup(args):
    rsync(SETUP_SRC, SETUP_DEST, args.yes)
    if args.yes:
        report(verify_setup(), "setup")
    else:
        print("Dry run above; add --yes to deploy.")


def cmd_firmware(args):
    bad = problems_in_release(args.dir)
    if bad:
        die("refusing to publish:\n  " + "\n  ".join(bad))
    rel.main(["publish", "--dest", FIRMWARE_DEST, "--dir", args.dir] + (["--yes"] if args.yes else []))
    if args.yes:
        report(verify_firmware(args.dir, args.key), "firmware")


def cmd_verify(args):
    if args.what == "setup":
        report(verify_setup(), "setup")
    else:
        report(verify_firmware(args.dir, args.key), "firmware")


def parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)

    s = sub.add_parser("setup")
    s.add_argument("--yes", action="store_true", help="really deploy (default is a dry run)")
    s.set_defaults(func=cmd_setup)

    f = sub.add_parser("firmware")
    f.add_argument("--dir", default="release", help="directory with manifest.json, images and rc/")
    f.add_argument("--key", help="release key; also checks the live manifest's signature")
    f.add_argument("--yes", action="store_true", help="really deploy (default is a dry run)")
    f.set_defaults(func=cmd_firmware)

    v = sub.add_parser("verify")
    v.add_argument("what", choices=["setup", "firmware"])
    v.add_argument("--dir", default="release")
    v.add_argument("--key")
    v.set_defaults(func=cmd_verify)
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
