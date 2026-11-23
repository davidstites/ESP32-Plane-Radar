#include "nexrad_decode/radar_image.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nexrad {

namespace {

constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
constexpr size_t kChunkOverhead = 12;  // length + type + CRC
constexpr size_t kIhdrLength = 13;
constexpr size_t kMaxPaletteEntries = 256;
constexpr double kEarthRadiusM = 6378137.0;
constexpr double kMaxMercatorLatDeg = 85.0;
constexpr double kPi = 3.14159265358979323846;

const char kRequestFormat[] =
    "/cgi-bin/wms/nexrad/n0q.cgi?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap"
    "&LAYERS=nexrad-n0q-900913&STYLES=&SRS=EPSG:3857&BBOX=%.0f,%.0f,%.0f,%.0f"
    "&WIDTH=%d&HEIGHT=%d&FORMAT=image/png%%3B%%20mode%%3D8bit&TRANSPARENT=TRUE";

uint32_t be32(const uint8_t* p) {
  return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
}

uint8_t paeth(uint8_t left, uint8_t up, uint8_t up_left) {
  const int estimate = left + up - up_left;
  const int d_left = std::abs(estimate - left);
  const int d_up = std::abs(estimate - up);
  const int d_up_left = std::abs(estimate - up_left);
  if (d_left <= d_up && d_left <= d_up_left) {
    return left;
  }
  return d_up <= d_up_left ? up : up_left;
}

bool supportedDepth(uint8_t depth) {
  return depth == 1 || depth == 2 || depth == 4 || depth == 8;
}

/**
 * Undo the per-row PNG filters in place; false on an unknown filter type. Rows are row_bytes
 * long; filters work on whole bytes, so this is the same for every palette bit depth.
 */
bool unfilter(uint8_t* scratch, size_t row_bytes) {
  const size_t stride = row_bytes + 1;
  for (int y = 0; y < kGridDim; ++y) {
    uint8_t* row = scratch + static_cast<size_t>(y) * stride + 1;
    const uint8_t* prev = y > 0 ? row - stride : nullptr;
    const uint8_t type = row[-1];
    if (type > 4) {
      return false;
    }
    for (size_t x = 0; x < row_bytes; ++x) {
      const uint8_t left = x > 0 ? row[x - 1] : 0;
      const uint8_t up = prev != nullptr ? prev[x] : 0;
      const uint8_t up_left = (x > 0 && prev != nullptr) ? prev[x - 1] : 0;
      switch (type) {
        case 1: row[x] = static_cast<uint8_t>(row[x] + left); break;
        case 2: row[x] = static_cast<uint8_t>(row[x] + up); break;
        case 3: row[x] = static_cast<uint8_t>(row[x] + (left + up) / 2); break;
        case 4: row[x] = static_cast<uint8_t>(row[x] + paeth(left, up, up_left)); break;
        default: break;
      }
    }
  }
  return true;
}

}  // namespace

const char* describe(DecodeResult result) {
  switch (result) {
    case DecodeResult::kOk: return "ok";
    case DecodeResult::kBadSignature: return "not a PNG";
    case DecodeResult::kTruncated: return "PNG cut short";
    case DecodeResult::kBadHeader: return "unexpected image size";
    case DecodeResult::kUnsupportedFormat: return "not a palette PNG";
    case DecodeResult::kMissingData: return "PNG has no palette or image data";
    case DecodeResult::kScratchTooSmall: return "decode buffer too small";
    case DecodeResult::kInflateFailed: return "image data would not decompress";
    case DecodeResult::kBadFilter: return "bad PNG row filter";
  }
  return "unknown";
}

DecodeResult decodePalettePng(uint8_t* png, size_t png_len, uint8_t* scratch, size_t scratch_len,
                              InflateFn inflate, Grid* grid) {
  if (png_len < sizeof(kSignature) || memcmp(png, kSignature, sizeof(kSignature)) != 0) {
    return DecodeResult::kBadSignature;
  }

  bool have_ihdr = false;
  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t depth = 0;
  uint8_t color_type = 0;
  uint8_t compression = 0;
  uint8_t filter = 0;
  uint8_t interlace = 0;
  const uint8_t* palette = nullptr;
  size_t palette_len = 0;
  const uint8_t* alphas = nullptr;
  size_t alphas_len = 0;
  bool have_idat = false;
  size_t idat_start = 0;
  size_t idat_len = 0;

  size_t pos = sizeof(kSignature);
  bool saw_end = false;
  while (!saw_end && pos + kChunkOverhead <= png_len) {
    const uint32_t chunk_len = be32(png + pos);
    if (chunk_len > png_len - pos - kChunkOverhead) {
      return DecodeResult::kTruncated;
    }
    const uint8_t* type = png + pos + 4;
    uint8_t* body = png + pos + 8;
    if (memcmp(type, "IHDR", 4) == 0) {
      if (chunk_len < kIhdrLength) {
        return DecodeResult::kBadHeader;
      }
      have_ihdr = true;
      width = be32(body);
      height = be32(body + 4);
      depth = body[8];
      color_type = body[9];
      compression = body[10];
      filter = body[11];
      interlace = body[12];
    } else if (memcmp(type, "PLTE", 4) == 0) {
      palette = body;
      palette_len = chunk_len;
    } else if (memcmp(type, "tRNS", 4) == 0) {
      alphas = body;
      alphas_len = chunk_len;
    } else if (memcmp(type, "IDAT", 4) == 0) {
      if (!have_idat) {
        have_idat = true;
        idat_start = pos + 8;
        idat_len = chunk_len;
      } else {
        // Join the pieces: slide this one down against the end of the previous.
        memmove(png + idat_start + idat_len, body, chunk_len);
        idat_len += chunk_len;
      }
    } else if (memcmp(type, "IEND", 4) == 0) {
      saw_end = true;
    }
    pos += kChunkOverhead + chunk_len;
  }

  if (!have_ihdr || width != static_cast<uint32_t>(kGridDim) ||
      height != static_cast<uint32_t>(kGridDim)) {
    return DecodeResult::kBadHeader;
  }
  if (!supportedDepth(depth) || color_type != 3 || compression != 0 || filter != 0 ||
      interlace != 0) {
    return DecodeResult::kUnsupportedFormat;
  }
  if (palette == nullptr || !have_idat) {
    return DecodeResult::kMissingData;
  }
  // The server writes the smallest palette depth that fits: a clear-sky image is 1-bit.
  const size_t row_bytes = (static_cast<size_t>(kGridDim) * depth + 7) / 8;
  const size_t decoded_bytes = static_cast<size_t>(kGridDim) * (row_bytes + 1);
  if (scratch_len < decoded_bytes) {
    return DecodeResult::kScratchTooSmall;
  }
  if (!inflate(png + idat_start, idat_len, scratch, decoded_bytes)) {
    return DecodeResult::kInflateFailed;
  }
  if (!unfilter(scratch, row_bytes)) {
    return DecodeResult::kBadFilter;
  }

  Level level_of[kMaxPaletteEntries] = {};
  const size_t entries = palette_len / 3 < kMaxPaletteEntries ? palette_len / 3 : kMaxPaletteEntries;
  for (size_t i = 0; i < entries; ++i) {
    const uint8_t alpha = i < alphas_len ? alphas[i] : 255;
    level_of[i] = classifyColor(alpha, palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]);
  }

  const unsigned mask = (1u << depth) - 1;
  for (int y = 0; y < kGridDim; ++y) {
    const uint8_t* row = scratch + static_cast<size_t>(y) * (row_bytes + 1) + 1;
    for (int x = 0; x < kGridDim; ++x) {
      const size_t bit = static_cast<size_t>(x) * depth;
      const unsigned shift = 8 - depth - static_cast<unsigned>(bit & 7);
      grid->set(x, y, level_of[(row[bit >> 3] >> shift) & mask]);
    }
  }
  return DecodeResult::kOk;
}

Bounds viewBounds(double center_lat, double center_lon, float outer_km, int outer_radius_px,
                  int half_extent_px) {
  double lat = center_lat;
  if (lat > kMaxMercatorLatDeg) {
    lat = kMaxMercatorLatDeg;
  } else if (lat < -kMaxMercatorLatDeg) {
    lat = -kMaxMercatorLatDeg;
  }
  const double lat_rad = lat * kPi / 180.0;
  const double center_x = center_lon * kPi / 180.0 * kEarthRadiusM;
  const double center_y = kEarthRadiusM * std::log(std::tan(kPi / 4.0 + lat_rad / 2.0));
  // Mercator stretches distances by 1/cos(lat); ground meters per pixel is outer_km / radius.
  const double half_m = static_cast<double>(outer_km) * 1000.0 * half_extent_px /
                        outer_radius_px / std::cos(lat_rad);
  return {center_x - half_m, center_y - half_m, center_x + half_m, center_y + half_m};
}

bool buildRequestPath(char* out, size_t out_size, const Bounds& bounds) {
  if (out_size == 0) {
    return false;
  }
  const int n = snprintf(out, out_size, kRequestFormat, bounds.min_x, bounds.min_y, bounds.max_x,
                         bounds.max_y, kGridDim, kGridDim);
  if (n < 0 || static_cast<size_t>(n) >= out_size) {
    out[0] = '\0';
    return false;
  }
  return true;
}

}  // namespace nexrad
