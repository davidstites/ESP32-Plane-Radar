// Accepts only addresses a radar on the user's own network can have: *.local names, RFC 1918
// IPv4 and link-local IPv4. Bare hosts only, so a crafted ?device= cannot redirect the
// hand-off (and the user's coordinates) to another site.

export const TOKEN_ALPHABET = '23456789ABCDEFGHJKMNPQRSTUVWXYZ';
const TOKEN_LENGTH = 6;

const LABEL = '[a-z0-9](?:[a-z0-9-]*[a-z0-9])?';
const LOCAL_NAME = new RegExp(`^${LABEL}(?:\\.${LABEL})*\\.local$`);
// No leading zeros: some parsers read "01" as octal, so "192.168.01.1" is ambiguous.
const IPV4 = /^(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})\.(0|[1-9]\d{0,2})$/;

function isLocalIpv4(host) {
  const match = IPV4.exec(host);
  if (!match) {
    return false;
  }
  const [a, b, c, d] = match.slice(1).map(Number);
  if (a > 255 || b > 255 || c > 255 || d > 255) {
    return false;
  }
  return a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) || (a === 169 && b === 254);
}

/** The normalised host, or null if it is not an address on the local network. */
export function parseDevice(input) {
  if (typeof input !== 'string') {
    return null;
  }
  const host = input.trim().toLowerCase();
  return LOCAL_NAME.test(host) || isLocalIpv4(host) ? host : null;
}

/** The upper-cased 6-character setup code (spaces and hyphens ignored), or null. */
export function parseToken(input) {
  if (typeof input !== 'string') {
    return null;
  }
  const token = input.replace(/[\s-]/g, '').toUpperCase();
  if (token.length !== TOKEN_LENGTH) {
    return null;
  }
  return [...token].every((ch) => TOKEN_ALPHABET.includes(ch)) ? token : null;
}
