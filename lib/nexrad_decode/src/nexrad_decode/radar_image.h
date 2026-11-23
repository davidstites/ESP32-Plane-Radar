#pragma once

#include <cstddef>
#include <cstdint>

#include "nexrad_decode/precip_grid.h"

namespace nexrad {

enum class DecodeResult {
  kOk,
  kBadSignature,
  kTruncated,
  kBadHeader,
  kUnsupportedFormat,
  kMissingData,
  kScratchTooSmall,
  kInflateFailed,
  kBadFilter,
};

/** Short description of a result, for logs. */
const char* describe(DecodeResult result);

/**
 * Inflate a zlib stream (in, in_len) into exactly out_len bytes at out. Returns false on
 * any error or if the stream does not produce exactly out_len bytes. The firmware passes
 * miniz's decompressor; the host tests pass zlib's.
 */
using InflateFn = bool (*)(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_len);

/** Scratch bytes decodePalettePng() needs at most: a filter byte plus kGridDim 8-bit pixels per row. */
constexpr size_t kScratchBytes = static_cast<size_t>(kGridDim) * (kGridDim + 1);

/**
 * Decode a kGridDim x kGridDim palette PNG (the map server's "image/png; mode=8bit", which it
 * writes at 1, 2, 4 or 8 bits per pixel, whichever fits the colors) into grid, classifying
 * each pixel's palette color. png is modified: the IDAT chunks are
 * joined in place so they can be inflated in one piece. grid is written only on success.
 */
DecodeResult decodePalettePng(uint8_t* png, size_t png_len, uint8_t* scratch, size_t scratch_len,
                              InflateFn inflate, Grid* grid);

/** Web Mercator (EPSG:3857) rectangle in meters. */
struct Bounds {
  double min_x;
  double min_y;
  double max_x;
  double max_y;
};

/**
 * The map area the radar image must cover so that image pixel (px, py) lines up with screen
 * pixel (px, py): the view's center and range. outer_km lies outer_radius_px from the screen
 * center, and the image reaches half_extent_px from it on each side.
 */
Bounds viewBounds(double center_lat, double center_lon, float outer_km, int outer_radius_px,
                  int half_extent_px);

/**
 * Write the radar map request path for the bounds into out. Returns false, leaving out empty,
 * if it does not fit.
 */
bool buildRequestPath(char* out, size_t out_size, const Bounds& bounds);

}  // namespace nexrad
