# Extra trust anchors

Roots added to the Mozilla list when `scripts/update_ca_bundle.sh` builds
`data/x509_crt_bundle.bin`.

- `globalsign_root_ca.pem` — GlobalSign Root CA (R1), SHA-256
  `EB:D4:10:40:E4:BB:3E:C7:42:C9:E3:81:D3:1E:F2:A4:1A:48:B6:68:5C:96:E7:CE:F3:C1:DF:6C:D4:33:1C:99`,
  expires 2028-01-28. Source: http://secure.globalsign.com/cacert/root-r1.crt

  Mozilla no longer ships this root, but adsb.fi (Google Trust Services) presents
  `GTS Root R4` cross-signed by it. Arduino core 2.0.14's bundle verifier
  (`esp_crt_bundle.c`) builds the chain from the presented certificates and only
  looks up the issuer of the top one, so without this root every handshake fails.
  `scripts/check_ca_bundle.py` reproduces that lookup. Remove this file once
  adsb.fi stops sending the cross-signed certificate.
