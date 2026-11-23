#include "metar_parse/metar_parser.h"

#include <cstdio>
#include <cstring>

namespace metar_parse {

namespace {

constexpr char kPathPrefix[] = "/api/data/metar?ids=";
constexpr char kPathSuffix[] = "&format=json&hours=2";
constexpr size_t kIcaoLen = 4;

bool alreadySeen(const Station* stations, size_t count, const char* icao) {
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(stations[i].icao, icao) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

Category categoryFromText(const char* text) {
  if (text == nullptr) {
    return Category::kUnknown;
  }
  if (strcmp(text, "VFR") == 0) {
    return Category::kVfr;
  }
  if (strcmp(text, "MVFR") == 0) {
    return Category::kMvfr;
  }
  if (strcmp(text, "IFR") == 0) {
    return Category::kIfr;
  }
  if (strcmp(text, "LIFR") == 0) {
    return Category::kLifr;
  }
  return Category::kUnknown;
}

void buildMetarFilter(JsonDocument& filter) {
  filter[0]["icaoId"] = true;
  filter[0]["fltCat"] = true;
}

ParseResult extractStations(const JsonDocument& doc, Station* out, size_t capacity) {
  if (!doc.is<JsonArrayConst>()) {
    return {false, 0, "METAR response is not an array"};
  }

  size_t count = 0;
  for (JsonObjectConst report : doc.as<JsonArrayConst>()) {
    if (count >= capacity) {
      break;
    }
    const char* icao = report["icaoId"] | "";
    const size_t len = strlen(icao);
    if (len == 0 || len > kIcaoLen || alreadySeen(out, count, icao)) {
      continue;
    }
    memcpy(out[count].icao, icao, len + 1);
    out[count].category = categoryFromText(report["fltCat"] | static_cast<const char*>(nullptr));
    ++count;
  }
  return {true, count, nullptr};
}

bool buildRequestPath(char* out, size_t out_size, const char* const* icaos, size_t count) {
  if (out_size > 0) {
    out[0] = '\0';
  }
  if (count == 0) {
    return false;
  }

  size_t len = 0;
  const auto append = [&](const char* text) {
    const size_t n = strlen(text);
    if (len + n + 1 > out_size) {
      return false;
    }
    memcpy(out + len, text, n + 1);
    len += n;
    return true;
  };

  bool fits = append(kPathPrefix);
  for (size_t i = 0; i < count && fits; ++i) {
    fits = (i == 0 || append(",")) && append(icaos[i]);
  }
  fits = fits && append(kPathSuffix);
  if (!fits && out_size > 0) {
    out[0] = '\0';
  }
  return fits;
}

}  // namespace metar_parse
