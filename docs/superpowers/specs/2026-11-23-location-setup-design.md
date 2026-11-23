# Location setup via metarly.app

Replaces raw lat/lon entry. Users do not know their coordinates, and a home is rarely near an ICAO airport, so address search and "use my current location" are the primary paths.

## Why a hosted page

`navigator.geolocation` needs a secure context. The device portal is plain HTTP (`http://plane-radar.local`), and the C3 cannot host TLS (~30 KB free heap). A static HTTPS page on `metarly.app` does the geolocation and geocoding in the browser, then **navigates** (not `fetch`, which is mixed content) to the device.

## Device flow

1. Wi-Fi join screen (existing yellow screen, AP `PlaneRadar-Setup`; the AP portal collects Wi-Fi only in practice).
2. After joining, with no saved location: **QR screen**. The QR encodes `https://metarly.app/setup?device=<ip>&token=<code>`. The screen also shows `metarly.app/setup`, the IP and the code, for desk use without a phone. **No skip option**: the default location is Amsterdam (52.3676, 4.9041), an upstream placeholder, and silently showing it is worse than a clear "finish setup" screen.
3. Radar screen as soon as a location is saved. A saved location on boot skips the QR screen. BOOT long-press already clears location and Wi-Fi, which returns to this flow.

## Device endpoint

`GET /location?lat=&lon=&token=` on the WiFiManager web server. The token is a per-boot random 6-character code. Wrong or missing token: 403, nothing saved. Bad coordinates: 400, nothing saved. Success saves via the existing validation and replies with a short HTML page. The token defends against drive-by cross-site navigation to `plane-radar.local`, not against peers on the LAN (the portal itself is unauthenticated).

## Portal

Latitude/longitude move behind an "Advanced" disclosure. Small inline dark CSS (about 2-3 KB, no external assets because AP mode has no internet). While on the LAN the portal shows a "Set location with address search" link built from the same URL, so location can be changed later.

## Page (`site/`, second plan)

- Address search: Nominatim first, Photon as fallback, one search per button press.
- "Use my current location".
- ICAO / IATA / airport-name lookup against a trimmed `airports.json` built from OurAirports by the existing build script; airport codes match locally before the geocoder is called.
- Manual lat/lon.
- Shows the match type (building, street, postcode, city) before sending.
- `device` parameter accepted only for `*.local`, RFC 1918 and link-local addresses.
- Device address and code can be typed in for the desk case.

## Decisions

- The setup page URL is a constant in `config.h`; forks are not supported.
- Two PRs: firmware first (testable on the C3 with a hand-built URL), then `site/`.
