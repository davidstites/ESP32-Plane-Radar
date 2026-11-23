#pragma once

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

/** Builds small 8-bit palette PNGs (and zlib-based inflate) for the radar decoder tests. */
namespace png_builder {

inline bool zlibInflate(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_len) {
  uLongf produced = out_len;
  return uncompress(out, &produced, in, in_len) == Z_OK && produced == out_len;
}

struct Spec {
  uint32_t width = 120;
  uint32_t height = 120;
  uint8_t depth = 8;
  uint8_t color_type = 3;
  uint8_t compression = 0;
  uint8_t filter_method = 0;
  uint8_t interlace = 0;
  std::vector<uint8_t> palette;       // r, g, b triples
  std::vector<uint8_t> transparency;  // tRNS alpha values
  std::vector<uint8_t> pixels;        // palette index per pixel, width * height
  std::vector<uint8_t> row_filters;   // PNG filter type per row; missing rows use 0
  size_t idat_piece = 0;              // split the compressed data into IDATs of this size
  bool omit_palette = false;
  bool omit_idat = false;
  bool omit_ihdr = false;
  bool short_ihdr = false;
  bool omit_iend = false;
  bool corrupt_data = false;
  int drop_rows = 0;  // encode this many rows fewer than the header claims
};

inline void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x >> 24));
  v.push_back(static_cast<uint8_t>(x >> 16));
  v.push_back(static_cast<uint8_t>(x >> 8));
  v.push_back(static_cast<uint8_t>(x));
}

inline void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& body) {
  put32(png, static_cast<uint32_t>(body.size()));
  for (int i = 0; i < 4; ++i) png.push_back(static_cast<uint8_t>(type[i]));
  png.insert(png.end(), body.begin(), body.end());
  put32(png, 0);  // CRC: the decoder does not check it
}

inline uint8_t predictor(int type, uint8_t left, uint8_t up, uint8_t up_left) {
  switch (type) {
    case 1: return left;
    case 2: return up;
    case 3: return static_cast<uint8_t>((left + up) / 2);
    case 4: {
      const int p = left + up - up_left;
      const int pa = std::abs(p - left), pb = std::abs(p - up), pc = std::abs(p - up_left);
      return (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : up_left);
    }
    default: return 0;
  }
}

inline std::vector<uint8_t> build(const Spec& spec) {
  const int rows = static_cast<int>(spec.height) - spec.drop_rows;
  // The header may claim a depth no PNG decoder here supports (to test rejecting it); the
  // pixel data is then irrelevant, so pack it as 8-bit.
  const bool packable = spec.depth == 1 || spec.depth == 2 || spec.depth == 4 || spec.depth == 8;
  const int depth = packable ? spec.depth : 8;
  const size_t row_bytes = (static_cast<size_t>(spec.width) * depth + 7) / 8;

  // Pack the palette indices, most significant bits first, into whole bytes per row.
  std::vector<uint8_t> packed_rows(static_cast<size_t>(rows) * row_bytes, 0);
  for (int y = 0; y < rows; ++y) {
    for (uint32_t x = 0; x < spec.width; ++x) {
      const uint8_t index = spec.pixels[static_cast<size_t>(y) * spec.width + x];
      const size_t bit = static_cast<size_t>(x) * depth;
      const int shift = 8 - depth - static_cast<int>(bit & 7);
      packed_rows[static_cast<size_t>(y) * row_bytes + (bit >> 3)] |=
          static_cast<uint8_t>(index << shift);
    }
  }

  std::vector<uint8_t> filtered;
  for (int y = 0; y < rows; ++y) {
    const int type = y < static_cast<int>(spec.row_filters.size()) ? spec.row_filters[y] : 0;
    filtered.push_back(static_cast<uint8_t>(type));
    for (size_t x = 0; x < row_bytes; ++x) {
      auto at = [&](long xx, int yy) -> uint8_t {
        return (xx < 0 || yy < 0) ? 0 : packed_rows[static_cast<size_t>(yy) * row_bytes + xx];
      };
      const uint8_t left = at(static_cast<long>(x) - 1, y);
      const uint8_t up = at(static_cast<long>(x), y - 1);
      const uint8_t up_left = at(static_cast<long>(x) - 1, y - 1);
      filtered.push_back(static_cast<uint8_t>(at(static_cast<long>(x), y) -
                                              predictor(type, left, up, up_left)));
    }
  }
  uLongf bound = compressBound(filtered.size());
  std::vector<uint8_t> packed(bound);
  compress2(packed.data(), &bound, filtered.data(), filtered.size(), 6);
  packed.resize(bound);
  if (spec.corrupt_data) {
    for (size_t i = 2; i < packed.size(); ++i) packed[i] ^= 0xA5;
  }

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  if (!spec.omit_ihdr) {
    std::vector<uint8_t> ihdr;
    put32(ihdr, spec.width);
    put32(ihdr, spec.height);
    ihdr.push_back(spec.depth);
    ihdr.push_back(spec.color_type);
    ihdr.push_back(spec.compression);
    ihdr.push_back(spec.filter_method);
    ihdr.push_back(spec.interlace);
    if (spec.short_ihdr) ihdr.resize(8);
    chunk(png, "IHDR", ihdr);
  }
  if (!spec.omit_palette) chunk(png, "PLTE", spec.palette);
  if (!spec.transparency.empty()) chunk(png, "tRNS", spec.transparency);
  if (!spec.omit_idat) {
    const size_t piece = spec.idat_piece == 0 ? packed.size() : spec.idat_piece;
    for (size_t at = 0; at < packed.size(); at += piece) {
      const size_t n = std::min(piece, packed.size() - at);
      chunk(png, "IDAT", std::vector<uint8_t>(packed.begin() + at, packed.begin() + at + n));
    }
  }
  if (!spec.omit_iend) chunk(png, "IEND", {});
  return png;
}

}  // namespace png_builder
