#include "ui/runway_overlay.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <radar_geo/radar_geo.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "data/large_airports.h"
#include "hardware/display_font.h"
#include "hardware/panel_color.h"
#include "services/metar_client.h"
#include "services/radar_location.h"
#include "ui/radar_projection.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::runway {
namespace {

constexpr size_t kMaxAirportLabels = 32;

bool s_in_range[data::large_airports::kAirportCount];
bool s_label_pending[data::large_airports::kAirportCount];

// The runways and labels in range only change with the center or the range preset,
// so they are projected once and replayed every frame instead of rescanning the dataset.
struct CachedRunway {
  int16_t x0, y0, x1, y1;
};

struct CachedLabel {
  const char* ident;
  const char* icao;
  int16_t x, y;
};

/** Runways beyond this many in the view are not drawn. */
constexpr size_t kMaxCachedRunways = 128;

CachedRunway s_cached_runways[kMaxCachedRunways];
size_t s_cached_runway_count = 0;
// The network task reads the cached labels to know which airports to ask about; the render
// loop rebuilds them. This lock covers rebuilds and snapshots (the render loop alone reads
// them to draw, so drawing needs no lock).
StaticSemaphore_t s_cache_lock_storage;
SemaphoreHandle_t s_cache_lock = xSemaphoreCreateMutexStatic(&s_cache_lock_storage);

constexpr int kLabelPadX = 2;
constexpr int kLabelPadY = 1;

CachedLabel s_cached_labels[kMaxAirportLabels];
size_t s_cached_label_count = 0;
radar_geo::ViewKey s_cached_view = {};
bool s_cache_valid = false;

bool s_runway_label_ready = false;
bool s_runway_label_use_vlw = false;
float s_runway_label_vlw_size = 0.38f;
const lgfx::GFXfont* s_runway_label_gfx = &fonts::FreeSansBold12pt7b;

void initRunwayLabelStyle(lgfx::LGFXBase& gfx) {
  if (s_runway_label_ready) {
    return;
  }

  const int target = radar::kRunwayLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_runway_label_use_vlw = true;
    s_runway_label_vlw_size = displayFontSmoothSizeForHeight(gfx, target);
  } else {
    s_runway_label_gfx = &fonts::FreeSansBold12pt7b;
    s_runway_label_use_vlw = false;
  }
  s_runway_label_ready = true;
}

void applyRunwayLabelStyle(lgfx::LGFXBase& gfx) {
  if (s_runway_label_use_vlw) {
    displayFontSetSmoothSize(gfx, s_runway_label_vlw_size);
  } else {
    displayFontSetBitmap(gfx, s_runway_label_gfx);
  }
}

float e7ToDeg(int32_t e7) { return static_cast<float>(e7) * 1e-7f; }

bool segmentIntersectsDisc(int x0, int y0, int x1, int y1) {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int r = radar::kGridOuterRadius;
  const int r_sq = r * r;

  if (radar::distSqFromCenter(x0, y0) <= r_sq || radar::distSqFromCenter(x1, y1) <= r_sq) {
    return true;
  }

  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int fx = x0 - cx;
  const int fy = y0 - cy;
  const int a = dx * dx + dy * dy;
  if (a == 0) {
    return false;
  }
  const int b = 2 * (fx * dx + fy * dy);
  const int c = fx * fx + fy * fy - r_sq;
  int disc = b * b - 4 * a * c;
  if (disc < 0) {
    return false;
  }
  disc = static_cast<int>(sqrtf(static_cast<float>(disc)));
  const float inv2a = 1.0f / (2.0f * static_cast<float>(a));
  const float t0 = (-static_cast<float>(b) - disc) * inv2a;
  const float t1 = (-static_cast<float>(b) + disc) * inv2a;
  return (t0 >= 0.0f && t0 <= 1.0f) || (t1 >= 0.0f && t1 <= 1.0f);
}

uint16_t flightCategoryColor(metar_parse::Category category) {
  switch (category) {
    case metar_parse::Category::kVfr:
      return panelColor565(radar::kFcVfrR, radar::kFcVfrG, radar::kFcVfrB);
    case metar_parse::Category::kMvfr:
      return panelColor565(radar::kFcMvfrR, radar::kFcMvfrG, radar::kFcMvfrB);
    case metar_parse::Category::kIfr:
      return panelColor565(radar::kFcIfrR, radar::kFcIfrG, radar::kFcIfrB);
    case metar_parse::Category::kLifr:
      return panelColor565(radar::kFcLifrR, radar::kFcLifrG, radar::kFcLifrB);
    case metar_parse::Category::kUnknown:
      break;
  }
  return 0;
}

void drawBoldRunwayLabel(lgfx::LGFXBase& gfx, const char* ident, const char* icao, int mx,
                         int my) {
  const int tw = gfx.textWidth(ident);
  const int th = gfx.fontHeight();

  gfx.setTextDatum(textdatum_t::bottom_center);
  const int left = mx - tw / 2 - kLabelPadX - radar::kRunwayLabelDotSlotPx;
  const int top = my - th - kLabelPadY;
  gfx.fillRect(left, top, tw + kLabelPadX * 2 + radar::kRunwayLabelDotSlotPx, th + kLabelPadY,
               radar::kColorBackground);

  const metar_parse::Category category = services::metar::category(icao);
  if (category != metar_parse::Category::kUnknown) {
    gfx.fillCircle(left + radar::kRunwayLabelDotSlotPx / 2, top + (th + kLabelPadY) / 2 + 1,
                   radar::kRunwayLabelDotRadiusPx, flightCategoryColor(category));
  }

  gfx.setTextColor(radar::kColorRunwayLabel, radar::kColorBackground);
  gfx.drawString(ident, mx - 1, my);
  gfx.drawString(ident, mx + 1, my);
  gfx.drawString(ident, mx, my);
}

bool projectRunway(const data::large_airports::Runway& rw, CachedRunway* out) {
  const float le_lat = e7ToDeg(rw.le_lat_e7);
  const float le_lon = e7ToDeg(rw.le_lon_e7);
  const float he_lat = e7ToDeg(rw.he_lat_e7);
  const float he_lon = e7ToDeg(rw.he_lon_e7);

  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  radar::latLonToScreen(le_lat, le_lon, &x0, &y0);
  radar::latLonToScreen(he_lat, he_lon, &x1, &y1);

  if (!segmentIntersectsDisc(x0, y0, x1, y1)) {
    return false;
  }

  radar::clipPointToOuterRing(x0, y0, &x1, &y1);
  radar::clipPointToOuterRing(x1, y1, &x0, &y0);

  *out = {static_cast<int16_t>(x0), static_cast<int16_t>(y0), static_cast<int16_t>(x1),
          static_cast<int16_t>(y1)};
  return true;
}

void offsetLabelFromCenter(int ax, int ay, int* lx, int* ly) {
  const int dx = ax - radar::kCenterX;
  const int dy = ay - radar::kCenterY;
  const float len = sqrtf(static_cast<float>(dx * dx + dy * dy));
  const int gap = radar::kRunwayLabelGapPx;
  if (len < 1.0f) {
    *lx = ax;
    *ly = ay - gap;
    return;
  }
  *lx = ax + static_cast<int>(lroundf(dx / len * static_cast<float>(gap)));
  *ly = ay + static_cast<int>(lroundf(dy / len * static_cast<float>(gap)));
}

void clipPointOntoOuterRing(int* x, int* y) {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int r = radar::kGridOuterRadius;
  const int dx = *x - cx;
  const int dy = *y - cy;
  const int d_sq = dx * dx + dy * dy;
  const int r_sq = r * r;
  if (d_sq <= r_sq || d_sq == 0) {
    return;
  }
  const float scale = static_cast<float>(r) / sqrtf(static_cast<float>(d_sq));
  *x = cx + static_cast<int>(lroundf(static_cast<float>(dx) * scale));
  *y = cy + static_cast<int>(lroundf(static_cast<float>(dy) * scale));
}

void cacheAirportLabel(const data::large_airports::Airport& ap) {
  int ax = 0;
  int ay = 0;
  radar::latLonToScreen(e7ToDeg(ap.lat_e7), e7ToDeg(ap.lon_e7), &ax, &ay);
  clipPointOntoOuterRing(&ax, &ay);

  int lx = 0;
  int ly = 0;
  offsetLabelFromCenter(ax, ay, &lx, &ly);
  s_cached_labels[s_cached_label_count++] = {radar_geo::airportDisplayIdent(ap.ident), ap.ident,
                                             static_cast<int16_t>(lx),
                                             static_cast<int16_t>(ly)};
}

radar_geo::ViewKey currentView() {
  return {services::location::lat(), services::location::lon(), radar::rangeCurrent().ring3_km};
}

/**
 * Order labels nearest the center first, then keep each one that fits without covering a
 * label already kept, trying the spot below and above it before dropping the label.
 * (A dropped airport still has its runways drawn.) Runs once per view, not per frame.
 */
void resolveLabelOverlaps(lgfx::LGFXBase& gfx) {
  for (size_t i = 1; i < s_cached_label_count; ++i) {
    const CachedLabel moving = s_cached_labels[i];
    const int moving_dist = radar::distSqFromCenter(moving.x, moving.y);
    size_t j = i;
    while (j > 0 &&
           radar::distSqFromCenter(s_cached_labels[j - 1].x, s_cached_labels[j - 1].y) >
               moving_dist) {
      s_cached_labels[j] = s_cached_labels[j - 1];
      --j;
    }
    s_cached_labels[j] = moving;
  }

  const int th = gfx.fontHeight();
  const int h = th + kLabelPadY;
  radar_geo::Rect placed[kMaxAirportLabels];
  size_t placed_count = 0;
  size_t kept = 0;
  for (size_t i = 0; i < s_cached_label_count; ++i) {
    CachedLabel label = s_cached_labels[i];
    const int tw = gfx.textWidth(label.ident);
    const int w = tw + kLabelPadX * 2 + radar::kRunwayLabelDotSlotPx;
    const int left = label.x - tw / 2 - kLabelPadX - radar::kRunwayLabelDotSlotPx;
    const int top = label.y - h;
    const radar_geo::Rect candidates[] = {{left, top, w, h}, {left, top + h, w, h},
                                          {left, top - h, w, h}};
    const int pick = radar_geo::firstClearCandidate(candidates, 3, placed, placed_count);
    if (pick < 0) {
      continue;
    }
    label.y = candidates[pick].y + h;
    placed[placed_count++] = candidates[pick];
    s_cached_labels[kept++] = label;
  }
  s_cached_label_count = kept;
}

void rebuildCache(lgfx::LGFXBase& gfx, const radar_geo::ViewKey& view) {
  xSemaphoreTake(s_cache_lock, portMAX_DELAY);
  const float radius_km = radar::fetchRadiusKm();

  s_cached_runway_count = 0;
  s_cached_label_count = 0;
  for (size_t i = 0; i < data::large_airports::kAirportCount; ++i) {
    s_in_range[i] = false;
    s_label_pending[i] = false;
  }

  for (size_t i = 0; i < data::large_airports::kRunwayCount; ++i) {
    const auto& rw = data::large_airports::kRunways[i];
    const uint16_t ap_idx = rw.airport_idx;
    if (!s_in_range[ap_idx]) {
      const auto& ap = data::large_airports::kAirports[ap_idx];
      float dx_km = 0.0f;
      float dy_km = 0.0f;
      float dist_km = 0.0f;
      radar::offsetKmFromCenter(e7ToDeg(ap.lat_e7), e7ToDeg(ap.lon_e7), &dx_km, &dy_km,
                                &dist_km);
      s_in_range[ap_idx] = (dist_km <= radius_km);
    }
    if (!s_in_range[ap_idx]) {
      continue;
    }
    CachedRunway line;
    if (!projectRunway(rw, &line)) {
      continue;
    }
    if (s_cached_runway_count < kMaxCachedRunways) {
      s_cached_runways[s_cached_runway_count++] = line;
    }
    if (!s_label_pending[ap_idx] && s_cached_label_count < kMaxAirportLabels) {
      s_label_pending[ap_idx] = true;
      cacheAirportLabel(data::large_airports::kAirports[ap_idx]);
    }
  }

  resolveLabelOverlaps(gfx);

  s_cached_view = view;
  s_cache_valid = true;
  xSemaphoreGive(s_cache_lock);
}

}  // namespace

void drawLargeAirportRunways(lgfx::LGFXBase& gfx) {
  if (!radar::showRunways()) {
    return;
  }
  displayFontEnsureLoaded(gfx);

  // The label font must be active before the cache rebuild, which measures label text.
  initRunwayLabelStyle(gfx);
  applyRunwayLabelStyle(gfx);

  const radar_geo::ViewKey view = currentView();
  if (!s_cache_valid || !radar_geo::sameView(view, s_cached_view)) {
    rebuildCache(gfx, view);
  }

  for (size_t i = 0; i < s_cached_runway_count; ++i) {
    const CachedRunway& rw = s_cached_runways[i];
    gfx.drawWideLine(rw.x0, rw.y0, rw.x1, rw.y1, radar::kRunwayLineHalfWidth,
                     radar::kColorRunway);
  }

  if (s_cached_label_count == 0) {
    return;
  }

  for (size_t i = 0; i < s_cached_label_count; ++i) {
    drawBoldRunwayLabel(gfx, s_cached_labels[i].ident, s_cached_labels[i].icao,
                        s_cached_labels[i].x, s_cached_labels[i].y);
  }
}

bool labelsReady() {
  xSemaphoreTake(s_cache_lock, portMAX_DELAY);
  const bool ready = s_cache_valid;
  xSemaphoreGive(s_cache_lock);
  return ready;
}

size_t snapshotLabelIcaos(const char** out, size_t max) {
  xSemaphoreTake(s_cache_lock, portMAX_DELAY);
  const size_t count = s_cached_label_count < max ? s_cached_label_count : max;
  for (size_t i = 0; i < count; ++i) {
    out[i] = s_cached_labels[i].icao;
  }
  xSemaphoreGive(s_cache_lock);
  return count;
}

}  // namespace ui::runway
