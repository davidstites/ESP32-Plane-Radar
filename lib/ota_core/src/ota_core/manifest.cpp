#include "ota_core/manifest.h"

#include <ArduinoJson.h>

#include <cstring>

namespace ota_core {

namespace {

constexpr uint32_t kMaxRollout = 100;
constexpr size_t kHashChars = 32;

// Last match wins, and unlike obj[key] this tells an explicit null from an absent key.
bool find(JsonObjectConst obj, const char* key, JsonVariantConst& found) {
  bool any = false;
  for (JsonPairConst kv : obj) {
    if (strcmp(kv.key().c_str(), key) == 0) {
      found = kv.value();
      any = true;
    }
  }
  return any;
}

U32Field u32(JsonVariantConst v) {
  if (v.is<uint32_t>()) return U32Field{true, v.as<uint32_t>()};
  return U32Field{false, 0};
}

// Copy a required string that must fit `cap` bytes including its NUL.
ParseError copyString(JsonObjectConst obj, const char* key, char* dest, size_t cap) {
  JsonVariantConst v;
  if (!find(obj, key, v) || v.isNull()) return ParseError::kMissingField;
  if (!v.is<const char*>()) return ParseError::kBadField;
  const char* s = v.as<const char*>();
  if (strlen(s) >= cap) return ParseError::kBadField;
  strcpy(dest, s);
  return ParseError::kNone;
}

bool lowerHex32(const char* s) {
  if (strlen(s) != kHashChars) return false;
  for (size_t i = 0; i < kHashChars; ++i) {
    const char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

ParseError parseCandidates(JsonVariantConst v, Manifest& out) {
  if (!v.is<JsonArrayConst>()) return ParseError::kBadField;
  JsonArrayConst arr = v.as<JsonArrayConst>();
  if (arr.size() > kMaxCandidates) return ParseError::kBadField;
  for (JsonVariantConst item : arr) {
    if (!item.is<const char*>()) return ParseError::kBadField;
    const char* s = item.as<const char*>();
    if (!lowerHex32(s)) return ParseError::kBadField;
    strcpy(out.candidates[out.candidateCount++], s);
  }
  return ParseError::kNone;
}

}  // namespace

ParseError parseManifest(const char* body, size_t len, Manifest& out) {
  memset(&out, 0, sizeof(out));
  out.rollout = kMaxRollout;
  if (len > kMaxManifestBytes) return ParseError::kTooLarge;

  JsonDocument doc;
  if (deserializeJson(doc, body, len) || !doc.is<JsonObjectConst>()) return ParseError::kNotJson;
  JsonObjectConst obj = doc.as<JsonObjectConst>();

  JsonVariantConst v;
  if (!find(obj, "build", v) || v.isNull()) return ParseError::kMissingField;
  out.build = u32(v);
  if (!find(obj, "min_build", v) || v.isNull()) return ParseError::kMissingField;
  out.minBuild = u32(v);

  ParseError e = copyString(obj, "sha256", out.sha256, sizeof(out.sha256));
  if (e == ParseError::kNone) e = copyString(obj, "channel", out.channel, sizeof(out.channel));
  if (e == ParseError::kNone) e = copyString(obj, "signature", out.signature, sizeof(out.signature));
  if (e == ParseError::kNone) e = copyString(obj, "firmware", out.firmware, sizeof(out.firmware));
  if (e != ParseError::kNone) return e;

  if (find(obj, "rollout", v)) {
    if (!v.is<uint32_t>() || v.as<uint32_t>() > kMaxRollout) return ParseError::kBadField;
    out.rollout = static_cast<uint8_t>(v.as<uint32_t>());
  }
  if (find(obj, "candidates", v)) {
    e = parseCandidates(v, out);
    if (e != ParseError::kNone) return e;
  }
  return ParseError::kNone;
}

}  // namespace ota_core
