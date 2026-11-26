#!/usr/bin/env python3
"""Sign and publish Plane Radar firmware releases.

  keygen   create the release private key and print its public half as a C array
  sign     sign a firmware image into a prod or per-board rc manifest
  promote  re-sign a tested rc image as prod (same bytes, same build)
  publish  rsync the manifests and images to the server (dry run unless --yes)

The signed payload is 91 bytes:
  "PLANERADAR-OTA-v1\\0" (18) || sha256 (32) || build (4, BE) || min_build (4, BE)
  || channel (1: prod 0x01, rc 0x02) || id (32)
For prod the id is zeros; for rc it is the board's published 16-byte device hash followed
by 16 zero bytes. rollout and candidates are not signed; the device treats them as advice.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import struct
import subprocess
import sys

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

PREFIX = b"PLANERADAR-OTA-v1\0"
CHANNELS = {"prod": 0x01, "rc": 0x02}
MAX_BUILD = 999999
MAX_CANDIDATES = 8
MAX_MANIFEST_BYTES = 1024
HASH_RE = re.compile(r"^[0-9a-f]{32}$")
DEFAULT_BASE = "/ota/planeradar"


def die(message):
    sys.exit(f"ota_release: {message}")


def build_payload(sha, build, min_build, channel, device_hash):
    """The bytes that get signed. `device_hash` is 32 hex characters, required for rc only."""
    if channel == "rc":
        device_id = bytes.fromhex(device_hash) + bytes(16)
    else:
        device_id = bytes(32)
    payload = PREFIX + sha + struct.pack(">II", build, min_build) + bytes([CHANNELS[channel]]) + device_id
    assert len(payload) == 91
    return payload


def load_key(path):
    with open(path, "rb") as f:
        return serialization.load_pem_private_key(f.read(), None)


def file_sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).digest()


def write_manifest(path, manifest):
    text = json.dumps(manifest, separators=(",", ":"))
    if len(text.encode()) > MAX_MANIFEST_BYTES:
        die(f"manifest is {len(text)} bytes; the device accepts at most {MAX_MANIFEST_BYTES}")
    with open(path, "w") as f:
        f.write(text + "\n")


def cmd_keygen(args):
    key = Ed25519PrivateKey.generate()
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                            serialization.NoEncryption())
    try:
        fd = os.open(args.out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        die(f"{args.out} already exists; refusing to overwrite a release key")
    with os.fdopen(fd, "wb") as f:
        f.write(pem)
    pub = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    rows = ",\n  ".join(", ".join(f"0x{b:02x}" for b in pub[i:i + 8]) for i in range(0, 32, 8))
    print(f"Wrote {args.out} (mode 0600). Back it up; losing it means no more updates.")
    print("Public key for lib/ota_core/src/ota_core/keys.h:")
    print(f"  {rows}")


def check_hash(value, what):
    if not HASH_RE.match(value):
        die(f"{what} must be 32 lowercase hex characters")


MARKER_RE = re.compile(rb"PLANERADAR-BUILD=([0-9]+)\0")


def check_build_marker(path, build):
    """The board reports the build compiled into the image, so the signed number must match it."""
    with open(path, "rb") as f:
        found = {int(m) for m in MARKER_RE.findall(f.read())}
    if found != {build}:
        reported = ", ".join(str(b) for b in sorted(found)) or "none"
        die(f"{path} reports build {reported}, not {build}; rebuild with -DPLANE_RADAR_BUILD={build}")


PATH_RE = re.compile(r"^/[A-Za-z0-9._/-]*$")


def check_firmware_path(path):
    """Mirror ota_core::isAllowedPath so a manifest the board would refuse is never published."""
    parts = path.split("/")[1:]
    ok = (2 <= len(path) <= 96 and PATH_RE.match(path)
          and all(p != ".." for p in parts) and "" not in parts[:-1])
    if not ok:
        die(f"--firmware-path {path!r} would be refused by the board (letters, digits, . _ - /; "
            "absolute, no '..' or '//', 2-96 bytes)")


def cmd_sign(args):
    if not 0 <= args.build <= MAX_BUILD:
        die(f"--build must be 0..{MAX_BUILD}")
    if args.min_build > args.build or args.min_build < 0:
        die("--min-build must be between 0 and --build")
    if args.channel == "rc":
        if not args.device_hash:
            die("--channel rc needs --device-hash")
        check_hash(args.device_hash, "--device-hash")
    elif args.device_hash:
        die("--device-hash only applies to --channel rc")
    if args.rollout is not None and not 0 <= args.rollout <= 100:
        die("--rollout must be 0..100")
    candidates = args.candidate or []
    if len(candidates) > MAX_CANDIDATES:
        die(f"at most {MAX_CANDIDATES} candidates")
    for c in candidates:
        check_hash(c, "--candidate")

    check_build_marker(args.firmware, args.build)
    firmware_path = args.firmware_path or f"{DEFAULT_BASE}/{os.path.basename(args.firmware)}"
    check_firmware_path(firmware_path)
    sha = file_sha(args.firmware)
    payload = build_payload(sha, args.build, args.min_build, args.channel, args.device_hash)
    signature = base64.b64encode(load_key(args.key).sign(payload)).decode()
    manifest = {
        "build": args.build,
        "min_build": args.min_build,
        "sha256": sha.hex(),
        "channel": args.channel,
        "signature": signature,
        "firmware": firmware_path,
    }
    if args.rollout is not None:
        manifest["rollout"] = args.rollout
    if candidates:
        manifest["candidates"] = candidates
    write_manifest(args.out, manifest)


def cmd_promote(args):
    with open(args.rc_manifest) as f:
        rc = json.load(f)
    if rc.get("channel") != "rc":
        die("--rc-manifest is not an rc manifest")
    check_hash(args.device_hash, "--device-hash")
    sha = file_sha(args.firmware)
    if sha.hex() != rc["sha256"]:
        die("firmware does not match the rc manifest's sha256; promote the exact bytes that were tested")
    rc_payload = build_payload(sha, rc["build"], rc["min_build"], "rc", args.device_hash)
    try:
        load_key(args.key).public_key().verify(base64.b64decode(rc["signature"], validate=True), rc_payload)
    except (InvalidSignature, ValueError, KeyError):
        die("the rc manifest's signature is not valid for this key and --device-hash; "
            "refusing to promote something we did not sign")
    firmware_path = args.firmware_path or rc["firmware"]
    check_firmware_path(firmware_path)
    payload = build_payload(sha, rc["build"], rc["min_build"], "prod", None)
    manifest = {
        "build": rc["build"],
        "min_build": rc["min_build"],
        "sha256": rc["sha256"],
        "channel": "prod",
        "signature": base64.b64encode(load_key(args.key).sign(payload)).decode(),
        "firmware": firmware_path,
    }
    write_manifest(args.out, manifest)


def cmd_publish(args):
    src = args.dir.rstrip("/") + "/"
    dest = args.dest.rstrip("/") + "/"
    base = ["rsync", "-av"]  # macOS rsync rejects --chmod; the server dirs are setgid www-data
    # Images first: a manifest must never be visible before the image it points at.
    # Allow-lists ending in a catch-all exclude: nothing else in the directory (a private key,
    # a stray file) can ever reach the public host.
    steps = [
        base + ["--include=firmware-*.bin", "--exclude=*", src, dest],
        base + ["--include=manifest.json", "--include=rc/", "--include=rc/*.json", "--exclude=*", src, dest],
    ]
    if not args.yes:
        for argv in steps:
            print("Dry run (add --yes to publish):", " ".join(argv))
        return
    for argv in steps:
        if subprocess.run(argv).returncode != 0:
            die("rsync failed")


def parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)

    k = sub.add_parser("keygen")
    k.add_argument("--out", required=True)
    k.set_defaults(func=cmd_keygen)

    s = sub.add_parser("sign")
    s.add_argument("--key", required=True)
    s.add_argument("--firmware", required=True)
    s.add_argument("--build", type=int, required=True)
    s.add_argument("--min-build", type=int, required=True)
    s.add_argument("--channel", choices=sorted(CHANNELS), required=True)
    s.add_argument("--device-hash")
    s.add_argument("--rollout", type=int)
    s.add_argument("--candidate", action="append")
    s.add_argument("--firmware-path", help=f"absolute URL path of the image (default {DEFAULT_BASE}/<file name>)")
    s.add_argument("--out", required=True)
    s.set_defaults(func=cmd_sign)

    r = sub.add_parser("promote")
    r.add_argument("--key", required=True)
    r.add_argument("--rc-manifest", required=True)
    r.add_argument("--device-hash", required=True, help="the board the rc manifest was signed for")
    r.add_argument("--firmware", required=True)
    r.add_argument("--firmware-path")
    r.add_argument("--out", required=True)
    r.set_defaults(func=cmd_promote)

    u = sub.add_parser("publish")
    u.add_argument("--dest", required=True)
    u.add_argument("--dir", required=True)
    u.add_argument("--yes", action="store_true")
    u.set_defaults(func=cmd_publish)
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
