#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "../support/fixture.h"
#include "../support/png_builder.h"
#include "nexrad_decode/precip_grid.h"
#include "nexrad_decode/radar_image.h"

using nexrad::DecodeResult;
using nexrad::Grid;
using nexrad::kGridDim;
using nexrad::Level;
using png_builder::Spec;

namespace {

// Palette used by the synthetic images: index -> (r, g, b). Index 0 is transparent via tRNS.
//   0 transparent, 1 green (light), 2 yellow (moderate), 3 red (heavy), 4 blue (light)
Spec baseSpec() {
  Spec spec;
  spec.palette = {0, 0, 0, 20, 200, 20, 220, 210, 10, 230, 20, 30, 60, 90, 220};
  spec.transparency = {0};
  spec.pixels.assign(static_cast<size_t>(kGridDim) * kGridDim, 0);
  return spec;
}

Level expectedLevel(uint8_t index) {
  switch (index) {
    case 1: return Level::kLight;
    case 2: return Level::kModerate;
    case 3: return Level::kHeavy;
    case 4: return Level::kLight;
    default: return Level::kNone;
  }
}

/**
 * A repeatable pattern that varies down and across and uses palette indices 0..distinct-1
 * (5 uses the whole synthetic palette; a narrower bit depth can hold fewer).
 */
void fillPattern(Spec* spec, int distinct = 5) {
  for (int y = 0; y < kGridDim; ++y) {
    for (int x = 0; x < kGridDim; ++x) {
      spec->pixels[static_cast<size_t>(y) * kGridDim + x] =
          static_cast<uint8_t>((x * 3 + y * 7 + (x * y) % 5) % distinct);
    }
  }
}

DecodeResult decode(std::vector<uint8_t>* png, Grid* grid,
                    size_t scratch_len = nexrad::kScratchBytes) {
  std::vector<uint8_t> scratch(nexrad::kScratchBytes);
  return nexrad::decodePalettePng(png->data(), png->size(), scratch.data(), scratch_len,
                                  png_builder::zlibInflate, grid);
}

void expectGridMatchesPixels(const Grid& grid, const Spec& spec) {
  for (int y = 0; y < kGridDim; ++y) {
    for (int x = 0; x < kGridDim; ++x) {
      ASSERT_EQ(grid.get(x, y), expectedLevel(spec.pixels[static_cast<size_t>(y) * kGridDim + x]))
          << "cell " << x << "," << y;
    }
  }
}

}  // namespace

// ---- classifyColor ---------------------------------------------------------------------

TEST(NexradClassify, TransparentIsNone) {
  EXPECT_EQ(nexrad::classifyColor(0, 255, 0, 0), Level::kNone);
  EXPECT_EQ(nexrad::classifyColor(15, 255, 0, 0), Level::kNone);
  EXPECT_NE(nexrad::classifyColor(16, 255, 0, 0), Level::kNone);
}

TEST(NexradClassify, GreensCyansAndBluesAreLight) {
  EXPECT_EQ(nexrad::classifyColor(255, 20, 200, 20), Level::kLight);
  EXPECT_EQ(nexrad::classifyColor(255, 100, 190, 220), Level::kLight);
  EXPECT_EQ(nexrad::classifyColor(255, 150, 255, 0), Level::kLight);  // red at the threshold
}

TEST(NexradClassify, YellowsAndOrangesAreModerate) {
  EXPECT_EQ(nexrad::classifyColor(255, 220, 210, 10), Level::kModerate);
  EXPECT_EQ(nexrad::classifyColor(255, 255, 144, 0), Level::kModerate);
  EXPECT_EQ(nexrad::classifyColor(255, 151, 120, 0), Level::kModerate);  // green at the threshold
}

TEST(NexradClassify, RedsAndMagentasAreHeavy) {
  EXPECT_EQ(nexrad::classifyColor(255, 230, 20, 30), Level::kHeavy);
  EXPECT_EQ(nexrad::classifyColor(255, 255, 0, 255), Level::kHeavy);
  EXPECT_EQ(nexrad::classifyColor(255, 151, 119, 0), Level::kHeavy);
}

// ---- Grid ------------------------------------------------------------------------------

TEST(NexradGrid, StartsEmptyAndHoldsEachLevel) {
  Grid grid;
  EXPECT_EQ(grid.count(Level::kNone), static_cast<size_t>(kGridDim) * kGridDim);
  grid.set(0, 0, Level::kLight);
  grid.set(1, 0, Level::kModerate);
  grid.set(2, 0, Level::kHeavy);
  grid.set(3, 0, Level::kHeavy);
  EXPECT_EQ(grid.get(0, 0), Level::kLight);
  EXPECT_EQ(grid.get(1, 0), Level::kModerate);
  EXPECT_EQ(grid.get(2, 0), Level::kHeavy);
  EXPECT_EQ(grid.get(4, 0), Level::kNone);
  EXPECT_EQ(grid.count(Level::kHeavy), 2u);
}

TEST(NexradGrid, SettingACellReplacesItWithoutDisturbingNeighbours) {
  Grid grid;
  grid.set(5, 5, Level::kHeavy);
  grid.set(6, 5, Level::kLight);
  grid.set(5, 5, Level::kModerate);
  EXPECT_EQ(grid.get(5, 5), Level::kModerate);
  EXPECT_EQ(grid.get(6, 5), Level::kLight);
  grid.set(5, 5, Level::kNone);
  EXPECT_EQ(grid.get(5, 5), Level::kNone);
}

TEST(NexradGrid, OutOfRangeIsNoneAndIgnored) {
  Grid grid;
  grid.set(-1, 0, Level::kHeavy);
  grid.set(0, -1, Level::kHeavy);
  grid.set(kGridDim, 0, Level::kHeavy);
  grid.set(0, kGridDim, Level::kHeavy);
  EXPECT_EQ(grid.count(Level::kNone), static_cast<size_t>(kGridDim) * kGridDim);
  EXPECT_EQ(grid.get(-1, 0), Level::kNone);
  EXPECT_EQ(grid.get(0, kGridDim), Level::kNone);
  EXPECT_EQ(grid.get(kGridDim, kGridDim - 1), Level::kNone);
  EXPECT_EQ(grid.get(kGridDim - 1, -1), Level::kNone);
}

TEST(NexradGrid, ClearEmptiesEverything) {
  Grid grid;
  grid.set(10, 10, Level::kHeavy);
  grid.set(kGridDim - 1, kGridDim - 1, Level::kLight);
  grid.clear();
  EXPECT_EQ(grid.count(Level::kNone), static_cast<size_t>(kGridDim) * kGridDim);
}

TEST(NexradGrid, MaskOutsideDiscKeepsOnlyTheRoundCenter) {
  Grid grid;
  for (int y = 0; y < kGridDim; ++y) {
    for (int x = 0; x < kGridDim; ++x) {
      grid.set(x, y, Level::kLight);
    }
  }
  grid.maskOutsideDisc(50);
  EXPECT_EQ(grid.get(60, 60), Level::kLight);   // center
  EXPECT_EQ(grid.get(110, 60), Level::kLight);  // exactly on the radius
  EXPECT_EQ(grid.get(111, 60), Level::kNone);
  EXPECT_EQ(grid.get(0, 0), Level::kNone);      // corner
  EXPECT_EQ(grid.get(60, 9), Level::kNone);
  EXPECT_EQ(grid.get(60, 10), Level::kLight);
  const double disc_cells = M_PI * 50.0 * 50.0;
  EXPECT_NEAR(static_cast<double>(grid.count(Level::kLight)), disc_cells, disc_cells * 0.05);
}

// ---- decodePalettePng ------------------------------------------------------------------

TEST(NexradDecode, RecordedRadarImageDecodesToTheExpectedLevels) {
  const std::string file = readFixture("nexrad_120_8bit.png");
  ASSERT_FALSE(file.empty());
  std::vector<uint8_t> png(file.begin(), file.end());
  Grid grid;
  ASSERT_EQ(decode(&png, &grid), DecodeResult::kOk);
  // Counted independently with a Python decoder over the same file.
  EXPECT_EQ(grid.count(Level::kNone), 12354u);
  EXPECT_EQ(grid.count(Level::kLight), 2035u);
  EXPECT_EQ(grid.count(Level::kModerate), 11u);
  EXPECT_EQ(grid.count(Level::kHeavy), 0u);
}

TEST(NexradDecode, MapsEveryPixelThroughThePalette) {
  Spec spec = baseSpec();
  fillPattern(&spec);
  std::vector<uint8_t> png = png_builder::build(spec);
  Grid grid;
  ASSERT_EQ(decode(&png, &grid), DecodeResult::kOk);
  expectGridMatchesPixels(grid, spec);
}

TEST(NexradDecode, UndoesEveryRowFilter) {
  for (uint8_t filter = 0; filter <= 4; ++filter) {
    Spec spec = baseSpec();
    fillPattern(&spec);
    spec.row_filters.assign(kGridDim, filter);
    std::vector<uint8_t> png = png_builder::build(spec);
    Grid grid;
    ASSERT_EQ(decode(&png, &grid), DecodeResult::kOk) << "filter " << int(filter);
    expectGridMatchesPixels(grid, spec);
  }
}

TEST(NexradDecode, UndoesRowFiltersThatChangeFromRowToRow) {
  Spec spec = baseSpec();
  fillPattern(&spec);
  for (int y = 0; y < kGridDim; ++y) {
    spec.row_filters.push_back(static_cast<uint8_t>(y % 5));
  }
  std::vector<uint8_t> png = png_builder::build(spec);
  Grid grid;
  ASSERT_EQ(decode(&png, &grid), DecodeResult::kOk);
  expectGridMatchesPixels(grid, spec);
}

TEST(NexradDecode, ReadsEveryPaletteBitDepth) {
  // The server writes 1, 2, 4 or 8 bits per pixel, whichever fits the colors in the image.
  for (uint8_t depth : {1, 2, 4, 8}) {
    for (uint8_t filter = 0; filter <= 4; ++filter) {
      Spec spec = baseSpec();
      spec.depth = depth;
      fillPattern(&spec, depth == 1 ? 2 : (depth == 2 ? 4 : 5));
      spec.row_filters.assign(kGridDim, filter);
      std::vector<uint8_t> png = png_builder::build(spec);
      Grid grid;
      ASSERT_EQ(decode(&png, &grid), DecodeResult::kOk)
          << "depth " << int(depth) << " filter " << int(filter);
      expectGridMatchesPixels(grid, spec);
    }
  }
}

TEST(NexradDecode, NarrowDepthsNeedLessScratch) {
  Spec spec = baseSpec();
  spec.depth = 1;
  fillPattern(&spec, 2);
  const size_t needed = static_cast<size_t>(kGridDim) * ((kGridDim + 7) / 8 + 1);
  std::vector<uint8_t> png = png_builder::build(spec);
  Grid grid;
  EXPECT_EQ(decode(&png, &grid, needed), DecodeResult::kOk);
  std::vector<uint8_t> again = png_builder::build(spec);
  EXPECT_EQ(decode(&again, &grid, needed - 1), DecodeResult::kScratchTooSmall);
}

TEST(NexradDecode, RecordedClearSkyOneBitImageIsAllNone) {
  const std::string file = readFixture("nexrad_120_1bit_clear.png");
  ASSERT_FALSE(file.empty());
  std::vector<uint8_t> png(file.begin(), file.end());
  Grid grid;
  grid.set(7, 7, Level::kHeavy);  // must be overwritten by the decode
  ASSERT_EQ(decode(&png, &grid), DecodeResult::kOk);
  EXPECT_EQ(grid.count(Level::kNone), static_cast<size_t>(kGridDim) * kGridDim);
}

TEST(NexradDecode, RecordedFourBitImageDecodesToTheExpectedLevels) {
  const std::string file = readFixture("nexrad_120_4bit.png");
  ASSERT_FALSE(file.empty());
  std::vector<uint8_t> png(file.begin(), file.end());
  Grid grid;
  ASSERT_EQ(decode(&png, &grid), DecodeResult::kOk);
  // Counted independently with a Python decoder over the same file.
  EXPECT_EQ(grid.count(Level::kNone), 14384u);
  EXPECT_EQ(grid.count(Level::kLight), 16u);
  EXPECT_EQ(grid.count(Level::kModerate), 0u);
  EXPECT_EQ(grid.count(Level::kHeavy), 0u);
}

TEST(NexradDecode, JoinsImageDataSplitAcrossManyChunks) {
  Spec spec = baseSpec();
  fillPattern(&spec);
  spec.idat_piece = 97;
  std::vector<uint8_t> png = png_builder::build(spec);
  Grid grid;
  ASSERT_EQ(decode(&png, &grid), DecodeResult::kOk);
  expectGridMatchesPixels(grid, spec);
}

TEST(NexradDecode, MissingTransparencyTableMeansEveryColorIsOpaque) {
  Spec spec = baseSpec();
  spec.transparency.clear();
  fillPattern(&spec);
  std::vector<uint8_t> png = png_builder::build(spec);
  Grid grid;
  ASSERT_EQ(decode(&png, &grid), DecodeResult::kOk);
  // Index 0 is black, opaque and not red: light, not none.
  EXPECT_NE(grid.count(Level::kLight), 0u);
  for (int y = 0; y < kGridDim; ++y) {
    for (int x = 0; x < kGridDim; ++x) {
      ASSERT_NE(grid.get(x, y), Level::kNone);
    }
  }
}

TEST(NexradDecode, PixelsPastTheEndOfThePaletteAreNone) {
  Spec spec = baseSpec();
  spec.palette.resize(6);  // only indices 0 and 1 exist
  spec.pixels.assign(spec.pixels.size(), 7);
  std::vector<uint8_t> png = png_builder::build(spec);
  Grid grid;
  grid.set(3, 3, Level::kHeavy);
  ASSERT_EQ(decode(&png, &grid), DecodeResult::kOk);
  EXPECT_EQ(grid.count(Level::kNone), static_cast<size_t>(kGridDim) * kGridDim);
}

TEST(NexradDecode, ToleratesATrailingPartialPaletteEntryAndNoIend) {
  Spec spec = baseSpec();
  spec.palette.push_back(9);  // a stray byte after the last whole entry
  spec.omit_iend = true;
  fillPattern(&spec);
  std::vector<uint8_t> png = png_builder::build(spec);
  Grid grid;
  ASSERT_EQ(decode(&png, &grid), DecodeResult::kOk);
  expectGridMatchesPixels(grid, spec);
}

TEST(NexradDecode, RejectsAnythingThatIsNotAPng) {
  Grid grid;
  std::vector<uint8_t> text = {'h', 't', 't', 'p', ' ', '5', '0', '0', ' ', 'e', 'r', 'r', 'o', 'r'};
  EXPECT_EQ(decode(&text, &grid), DecodeResult::kBadSignature);
  std::vector<uint8_t> tiny = {0x89, 'P', 'N'};
  EXPECT_EQ(decode(&tiny, &grid), DecodeResult::kBadSignature);
}

TEST(NexradDecode, RejectsAChunkThatRunsPastTheEnd) {
  Spec spec = baseSpec();
  std::vector<uint8_t> png = png_builder::build(spec);
  png.resize(png.size() - 20);  // drops IEND and cuts into the end of the image data
  Grid grid;
  EXPECT_EQ(decode(&png, &grid), DecodeResult::kTruncated);
}

TEST(NexradDecode, RejectsAnImageOfTheWrongSize) {
  Spec spec = baseSpec();
  spec.width = 60;
  spec.height = 60;
  spec.pixels.assign(60 * 60, 1);
  std::vector<uint8_t> png = png_builder::build(spec);
  Grid grid;
  EXPECT_EQ(decode(&png, &grid), DecodeResult::kBadHeader);

  Spec tall = baseSpec();
  tall.height = 121;
  tall.pixels.assign(static_cast<size_t>(kGridDim) * 121, 1);
  std::vector<uint8_t> tall_png = png_builder::build(tall);
  EXPECT_EQ(decode(&tall_png, &grid), DecodeResult::kBadHeader);
}

TEST(NexradDecode, RejectsAMissingOrShortHeader) {
  Grid grid;
  Spec none = baseSpec();
  none.omit_ihdr = true;
  std::vector<uint8_t> no_header = png_builder::build(none);
  EXPECT_EQ(decode(&no_header, &grid), DecodeResult::kBadHeader);

  Spec shortened = baseSpec();
  shortened.short_ihdr = true;
  std::vector<uint8_t> short_header = png_builder::build(shortened);
  EXPECT_EQ(decode(&short_header, &grid), DecodeResult::kBadHeader);
}

TEST(NexradDecode, RejectsFormatsOtherThanPalettePng) {
  Grid grid;
  const auto rejected = [&](auto tweak) {
    Spec spec = baseSpec();
    tweak(spec);
    std::vector<uint8_t> png = png_builder::build(spec);
    return decode(&png, &grid);
  };
  EXPECT_EQ(rejected([](Spec& s) { s.depth = 16; }), DecodeResult::kUnsupportedFormat);
  EXPECT_EQ(rejected([](Spec& s) { s.depth = 3; }), DecodeResult::kUnsupportedFormat);
  EXPECT_EQ(rejected([](Spec& s) { s.depth = 0; }), DecodeResult::kUnsupportedFormat);
  EXPECT_EQ(rejected([](Spec& s) { s.color_type = 6; }), DecodeResult::kUnsupportedFormat);
  EXPECT_EQ(rejected([](Spec& s) { s.interlace = 1; }), DecodeResult::kUnsupportedFormat);
  EXPECT_EQ(rejected([](Spec& s) { s.compression = 1; }), DecodeResult::kUnsupportedFormat);
  EXPECT_EQ(rejected([](Spec& s) { s.filter_method = 1; }), DecodeResult::kUnsupportedFormat);
}

TEST(NexradDecode, RejectsAPngWithoutPaletteOrImageData) {
  Grid grid;
  Spec no_palette = baseSpec();
  no_palette.omit_palette = true;
  std::vector<uint8_t> a = png_builder::build(no_palette);
  EXPECT_EQ(decode(&a, &grid), DecodeResult::kMissingData);

  Spec no_data = baseSpec();
  no_data.omit_idat = true;
  std::vector<uint8_t> b = png_builder::build(no_data);
  EXPECT_EQ(decode(&b, &grid), DecodeResult::kMissingData);
}

TEST(NexradDecode, RejectsTooSmallAScratchBuffer) {
  Spec spec = baseSpec();
  std::vector<uint8_t> png = png_builder::build(spec);
  Grid grid;
  EXPECT_EQ(decode(&png, &grid, nexrad::kScratchBytes - 1), DecodeResult::kScratchTooSmall);
}

TEST(NexradDecode, RejectsImageDataThatWillNotInflate) {
  Spec corrupt = baseSpec();
  fillPattern(&corrupt);
  corrupt.corrupt_data = true;
  std::vector<uint8_t> a = png_builder::build(corrupt);
  Grid grid;
  EXPECT_EQ(decode(&a, &grid), DecodeResult::kInflateFailed);

  Spec short_data = baseSpec();
  short_data.drop_rows = 3;  // the stream holds fewer rows than the header claims
  std::vector<uint8_t> b = png_builder::build(short_data);
  EXPECT_EQ(decode(&b, &grid), DecodeResult::kInflateFailed);
}

TEST(NexradDecode, RejectsAnUnknownRowFilter) {
  Spec spec = baseSpec();
  spec.row_filters = {0, 9};  // row 1 claims filter type 9
  std::vector<uint8_t> png = png_builder::build(spec);
  Grid grid;
  EXPECT_EQ(decode(&png, &grid), DecodeResult::kBadFilter);
}

TEST(NexradDecode, LeavesTheGridUntouchedWhenDecodingFails) {
  Grid grid;
  for (int y = 0; y < kGridDim; ++y) {
    for (int x = 0; x < kGridDim; ++x) {
      grid.set(x, y, Level::kModerate);
    }
  }
  Spec bad_filter = baseSpec();
  fillPattern(&bad_filter);
  bad_filter.row_filters = {0, 0, 0, 7};
  std::vector<uint8_t> a = png_builder::build(bad_filter);
  ASSERT_EQ(decode(&a, &grid), DecodeResult::kBadFilter);

  Spec corrupt = baseSpec();
  corrupt.corrupt_data = true;
  std::vector<uint8_t> b = png_builder::build(corrupt);
  ASSERT_EQ(decode(&b, &grid), DecodeResult::kInflateFailed);

  EXPECT_EQ(grid.count(Level::kModerate), static_cast<size_t>(kGridDim) * kGridDim);
}

TEST(NexradDecode, EveryResultHasADescription) {
  const DecodeResult all[] = {DecodeResult::kOk,           DecodeResult::kBadSignature,
                              DecodeResult::kTruncated,    DecodeResult::kBadHeader,
                              DecodeResult::kUnsupportedFormat, DecodeResult::kMissingData,
                              DecodeResult::kScratchTooSmall,   DecodeResult::kInflateFailed,
                              DecodeResult::kBadFilter};
  for (DecodeResult r : all) {
    EXPECT_GT(strlen(nexrad::describe(r)), 0u);
    EXPECT_STRNE(nexrad::describe(r), "unknown");
  }
  EXPECT_STREQ(nexrad::describe(static_cast<DecodeResult>(99)), "unknown");
}

// ---- viewBounds / buildRequestPath -----------------------------------------------------

TEST(NexradBounds, AtTheEquatorGroundMetersAreMercatorMeters) {
  // outer 10 km at 100 px, image reaches 120 px: 12 km each way.
  const nexrad::Bounds b = nexrad::viewBounds(0.0, 0.0, 10.0f, 100, 120);
  EXPECT_NEAR(b.min_x, -12000.0, 1e-6);
  EXPECT_NEAR(b.max_x, 12000.0, 1e-6);
  EXPECT_NEAR(b.min_y, -12000.0, 1e-6);
  EXPECT_NEAR(b.max_y, 12000.0, 1e-6);
}

TEST(NexradBounds, MercatorStretchesByOneOverCosLatitude) {
  const nexrad::Bounds equator = nexrad::viewBounds(0.0, 0.0, 10.0f, 100, 100);
  const nexrad::Bounds sixty = nexrad::viewBounds(60.0, 0.0, 10.0f, 100, 100);
  EXPECT_NEAR(sixty.max_x - sixty.min_x, 2.0 * (equator.max_x - equator.min_x), 1e-3);
}

TEST(NexradBounds, CentersOnTheMercatorPositionOfTheView) {
  // Dallas/Fort Worth, from a live request: the center of the box the server accepted.
  const nexrad::Bounds b = nexrad::viewBounds(32.8998, -97.0403, 16.0f, 107, 120);
  const double center_x = (b.min_x + b.max_x) / 2.0;
  const double center_y = (b.min_y + b.max_y) / 2.0;
  EXPECT_NEAR(center_x, -97.0403 * M_PI / 180.0 * 6378137.0, 1e-3);
  EXPECT_NEAR(center_y, 3882011.5, 2.0);
}

TEST(NexradBounds, ClampsLatitudesMercatorCannotRepresent) {
  const nexrad::Bounds north = nexrad::viewBounds(90.0, 0.0, 10.0f, 100, 100);
  const nexrad::Bounds south = nexrad::viewBounds(-90.0, 0.0, 10.0f, 100, 100);
  EXPECT_TRUE(std::isfinite(north.max_y));
  EXPECT_TRUE(std::isfinite(south.min_y));
  EXPECT_GT(north.min_y, 0.0);
  EXPECT_LT(south.max_y, 0.0);
}

TEST(NexradRequest, BuildsTheMapRequestForTheBounds) {
  const nexrad::Bounds b = {-10850396.4, 3834092.0, -10754558.0, 3929931.6};
  char path[512];
  ASSERT_TRUE(nexrad::buildRequestPath(path, sizeof(path), b));
  const std::string p = path;
  EXPECT_EQ(p.rfind("/cgi-bin/wms/nexrad/n0q.cgi?", 0), 0u);
  EXPECT_NE(p.find("BBOX=-10850396,3834092,-10754558,3929932&"), std::string::npos);
  EXPECT_NE(p.find("WIDTH=120&HEIGHT=120"), std::string::npos);
  EXPECT_NE(p.find("FORMAT=image/png%3B%20mode%3D8bit"), std::string::npos);
  EXPECT_NE(p.find("TRANSPARENT=TRUE"), std::string::npos);
  EXPECT_NE(p.find("SRS=EPSG:3857"), std::string::npos);
}

TEST(NexradRequest, FailsAndEmptiesWhenItDoesNotFit) {
  const nexrad::Bounds b = {0, 0, 1, 1};
  char path[40] = "junk";
  EXPECT_FALSE(nexrad::buildRequestPath(path, sizeof(path), b));
  EXPECT_STREQ(path, "");
}

TEST(NexradRequest, ZeroSizeBufferIsNeverWritten) {
  const nexrad::Bounds b = {0, 0, 1, 1};
  char guard[4] = {'x', 'x', 'x', 'x'};
  EXPECT_FALSE(nexrad::buildRequestPath(guard, 0, b));
  EXPECT_EQ(guard[0], 'x');
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
