#!/usr/bin/env python3
"""Throwaway OTA test rig for hardware acceptance runs (docs/ota-acceptance.md).

Builds firmware that trusts a throwaway release key and a throwaway CA, points it at a local
HTTPS server on this machine, and serves signed manifests from it. Nothing here touches the
real release key or metarly.app; images built by this script cannot take real updates.

  ota_rig.py init  --host 192.168.4.67     keys and certificates (once)
  ota_rig.py build 52 [--window 1800]      throwaway image at that build number
  ota_rig.py upload 52                     flash that image over USB
  ota_rig.py serve 53 [--rate 20000]       sign image 53 and serve it (foreground)
  ota_rig.py capture 3000 serial.log       reconnecting serial log (seconds, file)

Typical run: build 52 and 53, upload 52, serve 53, reset the board, wait for the install.
Everything lives in the work directory (default release/ota-rig, git-ignored). The image is
built from a detached worktree of HEAD plus your uncommitted changes.
"""
import argparse
import http.server
import os
import re
import shutil
import ssl
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PORT_DEFAULT = 8443
SERIAL_DEFAULT = "/dev/cu.usbmodem101"


def run(cmd, **kw):
    kw.setdefault("check", True)
    return subprocess.run([str(c) for c in cmd], **kw)


def work(args):
    return Path(args.work).resolve()


def cmd_init(args):
    w = work(args)
    w.mkdir(parents=True, exist_ok=True)
    host = args.host
    ext = w / "srv.ext"
    # mbedtls 2.x (the Arduino core) ignores IP SANs, so the address goes in as a dNSName.
    ext.write_text(f"subjectAltName=DNS:{host}\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature\n"
                   "extendedKeyUsage=serverAuth\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid\n")
    run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", w / "ca.key"])
    run(["openssl", "req", "-x509", "-new", "-key", w / "ca.key", "-subj", "/CN=Plane Radar Test CA", "-days", "30",
         "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,cRLSign",
         "-addext", "subjectKeyIdentifier=hash", "-out", w / "ca.pem"])
    run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", w / "srv.key"])
    run(["openssl", "req", "-new", "-key", w / "srv.key", "-subj", f"/CN={host}", "-out", w / "srv.csr"])
    run(["openssl", "x509", "-req", "-in", w / "srv.csr", "-CA", w / "ca.pem", "-CAkey", w / "ca.key",
         "-CAcreateserial", "-days", "30", "-extfile", ext, "-out", w / "srv.pem"])
    run(["openssl", "genpkey", "-algorithm", "ED25519", "-out", w / "release.pem"])
    (w / "host").write_text(f"{host}:{args.port}")
    print(f"rig initialised in {w} for {host}:{args.port}")


def patch_tree(wt, w):
    """Make the worktree trust the throwaway release key and CA and talk to the local server."""
    from cryptography import x509
    from cryptography.hazmat.primitives import serialization as ser

    key = ser.load_pem_private_key((w / "release.pem").read_bytes(), None)
    pub = key.public_key().public_bytes(ser.Encoding.Raw, ser.PublicFormat.Raw)
    keys_h = wt / "lib/ota_core/src/ota_core/keys.h"
    text = keys_h.read_text()
    arr = ", ".join(f"0x{b:02x}" for b in pub)
    text, n = re.subn(r"(kPrimary\[kKeyBytes\] = \{)[^}]*\}", lambda m: m.group(1) + arr + "}", text, count=1)
    assert n == 1, "kPrimary not found in keys.h"
    keys_h.write_text(text)

    # CA bundle: u16 count, then per entry u16 subject length, u16 key length, subject DER,
    # SPKI DER; all big-endian, sorted by subject.
    bundle = wt / "data/x509_crt_bundle.bin"
    raw = bundle.read_bytes()
    count = struct.unpack(">H", raw[:2])[0]
    ents, o = [], 2
    for _ in range(count):
        sl, kl = struct.unpack(">HH", raw[o:o + 4])
        ents.append((raw[o + 4:o + 4 + sl], raw[o + 4 + sl:o + 4 + sl + kl]))
        o += 4 + sl + kl
    ca = x509.load_pem_x509_certificate((w / "ca.pem").read_bytes())
    ents.append((ca.subject.public_bytes(),
                 ca.public_key().public_bytes(ser.Encoding.DER, ser.PublicFormat.SubjectPublicKeyInfo)))
    ents.sort(key=lambda e: e[0])
    bundle.write_bytes(struct.pack(">H", len(ents)) + b"".join(
        struct.pack(">HH", len(s), len(k)) + s + k for s, k in ents))


def cmd_build(args, upload=False):
    w = work(args)
    wt = w / "wt"
    if not wt.exists():
        run(["git", "-C", ROOT, "worktree", "add", "--detach", wt, "HEAD"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run(["git", "-C", wt, "checkout", "-q", "--detach", subprocess.check_output(["git", "-C", ROOT, "rev-parse", "HEAD"], text=True).strip()])
    run(["git", "-C", wt, "checkout", "-q", "--", "."])
    diff = subprocess.run(["git", "-C", ROOT, "diff", "HEAD", "--", "."], capture_output=True, check=True).stdout
    if diff:
        run(["git", "-C", wt, "apply"], input=diff)
    patch_tree(wt, w)
    cfg = wt / "include/config.h"
    text = cfg.read_text()
    host = (w / "host").read_text().strip()
    text, n = re.subn(r'kOtaHost\[\] = "[^"]*"', f'kOtaHost[] = "{host}"', text)
    assert n == 1, "kOtaHost not found"
    text, n = re.subn(r"kOtaConfirmAfterMs = [^;]*;", f"kOtaConfirmAfterMs = {args.window}UL * 1000UL;", text)
    assert n == 1, "kOtaConfirmAfterMs not found"
    cfg.write_text(text)
    env = dict(os.environ, PLATFORMIO_BUILD_FLAGS=f"-DPLANE_RADAR_BUILD={args.build}")
    cmd = ["pio", "run", "-e", "supermini"] + (["-t", "upload", "--upload-port", args.port_serial] if upload else [])
    run(cmd, cwd=wt, env=env)
    shutil.copy(wt / ".pio/build/supermini/firmware.bin", w / f"fw{args.build}.bin")
    print(f"built {w / f'fw{args.build}.bin'}")


def cmd_serve(args):
    w = work(args)
    www = w / "www"
    shutil.rmtree(www, ignore_errors=True)
    www.mkdir()
    fw = w / f"fw{args.build}.bin"
    shutil.copy(fw, www / fw.name)
    run([sys.executable, ROOT / "scripts/ota_release.py", "sign", "--key", w / "release.pem", "--firmware", fw,
         "--build", args.build, "--min-build", 0, "--channel", "prod", "--out", www / "manifest.json"],
        stdout=subprocess.DEVNULL)
    rate = args.rate

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"  # no chunking: the device reads the body raw

        def log_message(self, fmt, *a):
            print(time.strftime("%H:%M:%S"), fmt % a, flush=True)

        def do_GET(self):
            p = self.path.split("?")[0]
            name = "manifest.json" if p.endswith("manifest.json") else os.path.basename(p)
            f = www / name
            if not f.is_file():
                self.send_response(404)
                self.end_headers()
                return
            data = f.read_bytes()
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            sent, step = 0, 4096
            try:
                while sent < len(data):
                    self.wfile.write(data[sent:sent + step])
                    self.wfile.flush()
                    sent += step
                    if rate and name.endswith(".bin"):
                        time.sleep(step / rate)  # throttle so a cable pull can land mid-download
            except OSError as e:
                print(time.strftime("%H:%M:%S"), "client gone at", sent, e, flush=True)

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(w / "srv.pem", w / "srv.key")
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2  # the device's mbedtls is 2.x
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    print(f"serving build {args.build} on :{args.port} (rate {rate or 'unlimited'} B/s)", flush=True)
    srv.serve_forever()


def cmd_capture(args):
    import serial  # pyserial

    end = time.time() + args.seconds
    out = open(args.log, "a")
    s = None
    while time.time() < end:  # a power cut re-enumerates USB, so reopen the port as needed
        try:
            if s is None:
                s = serial.Serial(args.port_serial, 115200, timeout=0.5)
            line = s.readline()
            if line:
                out.write(time.strftime("%H:%M:%S ") + line.decode(errors="replace"))
                out.flush()
        except Exception:
            s = None
            time.sleep(0.3)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", default=str(ROOT / "release/ota-rig"))
    ap.add_argument("--port-serial", default=SERIAL_DEFAULT)
    sub = ap.add_subparsers(dest="cmd", required=True)
    i = sub.add_parser("init")
    i.add_argument("--host", required=True, help="this machine's LAN address, as the board reaches it")
    i.add_argument("--port", type=int, default=PORT_DEFAULT)
    for name in ("build", "upload"):
        b = sub.add_parser(name)
        b.add_argument("build", type=int)
        b.add_argument("--window", type=int, default=60, help="confirmation window in seconds (default 60)")
    s = sub.add_parser("serve")
    s.add_argument("build", type=int)
    s.add_argument("--port", type=int, default=PORT_DEFAULT)
    s.add_argument("--rate", type=int, default=0, help="image bytes per second, 0 = unthrottled")
    c = sub.add_parser("capture")
    c.add_argument("seconds", type=float)
    c.add_argument("log")
    args = ap.parse_args()
    {"init": cmd_init, "build": cmd_build, "upload": lambda a: cmd_build(a, True), "serve": cmd_serve,
     "capture": cmd_capture}[args.cmd](args)


if __name__ == "__main__":
    main()
