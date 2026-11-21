#include "hardware/display_font.h"

#include "hardware/display.h"

extern "C" {
extern const uint8_t _binary_data_ui_font_vlw_start[] asm(
    "_binary_data_ui_font_vlw_start");
extern const uint8_t _binary_data_ui_font_vlw_end[] asm("_binary_data_ui_font_vlw_end");
}

namespace {

bool s_vlw_loaded = false;

const uint8_t* vlwData() { return _binary_data_ui_font_vlw_start; }

size_t vlwDataLen() {
  return static_cast<size_t>(_binary_data_ui_font_vlw_end -
                               _binary_data_ui_font_vlw_start);
}

bool vlwActiveOn(const lgfx::LGFXBase& gfx) {
  const lgfx::IFont* font = gfx.getFont();
  return font != nullptr && font->getType() == lgfx::IFont::font_type_t::ft_vlw;
}

}  // namespace

bool displayFontInit() {
  s_vlw_loaded = vlwDataLen() > 0 &&
                 tft.loadFont(vlwData(), lgfx::IFont::font_type_t::ft_vlw);
  if (!s_vlw_loaded) {
    Serial.println("Smooth font load failed — using bitmap fallback");
  }
  return s_vlw_loaded;
}

bool displayFontIsSmooth() { return s_vlw_loaded; }

bool displayFontEnsureLoaded(lgfx::LGFXBase& gfx) {
  if (!s_vlw_loaded) {
    return false;
  }
  if (vlwActiveOn(gfx)) {
    return true;
  }
  return gfx.loadFont(vlwData(), lgfx::IFont::font_type_t::ft_vlw);
}

void displayFontSetSmoothSize(lgfx::LGFXBase& gfx, float size) {
  gfx.setTextSize(size);
}

float displayFontSmoothSizeForHeight(lgfx::LGFXBase& gfx, int target_px) {
  float lo = 0.2f;
  float hi = 1.2f;
  for (int i = 0; i < 16; ++i) {
    const float mid = (lo + hi) * 0.5f;
    displayFontSetSmoothSize(gfx, mid);
    if (gfx.fontHeight() < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void displayFontSetBitmap(lgfx::LGFXBase& gfx, const lgfx::GFXfont* font) {
  gfx.setFont(font);
  gfx.setTextSize(1);
}
