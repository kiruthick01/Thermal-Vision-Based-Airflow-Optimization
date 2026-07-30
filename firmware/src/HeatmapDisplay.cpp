#include "HeatmapDisplay.h"

bool HeatmapDisplay::begin() {
  spi_.begin(kTftSckPin, kTftMisoPin, kTftMosiPin, kTftCsPin);
  tft_.begin();
  tft_.setRotation(3);  // landscape: 320x240
  tft_.fillScreen(ILI9341_BLACK);
  tft_.setTextColor(ILI9341_WHITE);
  tft_.setTextSize(1);
  return true;
}

void HeatmapDisplay::render(const float* frame, int rows, int cols, const Hotspot* spots, int count) {
  // Auto-scale the color gradient to this frame's actual range -- same
  // behavior as a real cheap thermal camera (no fixed absolute scale).
  float tMin = frame[0];
  float tMax = frame[0];
  const int n = rows * cols;
  for (int i = 1; i < n; ++i) {
    if (frame[i] < tMin) tMin = frame[i];
    if (frame[i] > tMax) tMax = frame[i];
  }
  if (tMax - tMin < 0.5f) tMax = tMin + 0.5f;  // avoid a degenerate flat gradient

  const int cellW = kScreenW / cols;
  const int cellH = (kScreenH - kStatusBarH) / rows;
  const int yOffset = kStatusBarH;

  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      const float t = frame[r * cols + c];
      tft_.fillRect(c * cellW, yOffset + r * cellH, cellW, cellH, thermalColor565(t, tMin, tMax));
    }
  }

  // Hotspot markers: a white/black ring at each detected blob's centroid,
  // directly overlaid on the raw heatmap -- this is HotspotDetector's
  // output rendered on top of the data it was computed from.
  for (int i = 0; i < count; ++i) {
    const int cx = static_cast<int>(spots[i].col * cellW + cellW / 2.0f);
    const int cy = yOffset + static_cast<int>(spots[i].row * cellH + cellH / 2.0f);
    const int radius = (cellW < cellH ? cellW : cellH) / 2 + 2;
    tft_.drawCircle(cx, cy, radius, ILI9341_WHITE);
    tft_.drawCircle(cx, cy, radius + 1, ILI9341_BLACK);
  }

  // Status bar.
  tft_.fillRect(0, 0, kScreenW, kStatusBarH, ILI9341_BLACK);
  tft_.setCursor(2, 4);
  tft_.setTextColor(ILI9341_WHITE);
  tft_.printf("Hotspots: %d   %.1f-%.1fC", count, tMin, tMax);
}

uint16_t HeatmapDisplay::thermalColor565(float t, float tMin, float tMax) {
  float frac = (t - tMin) / (tMax - tMin);
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;

  // 5-stop "ironbow-ish" palette: blue (coldest) -> cyan -> green -> yellow
  // -> red (hottest). Matches the classic thermal-camera look.
  static const uint8_t kPalette[5][3] = {
      {0, 0, 255},
      {0, 255, 255},
      {0, 255, 0},
      {255, 255, 0},
      {255, 0, 0},
  };
  constexpr int kStops = 5;
  const float scaled = frac * (kStops - 1);
  const int idx = static_cast<int>(scaled);
  const int idx2 = (idx + 1 < kStops) ? idx + 1 : idx;
  const float localFrac = scaled - idx;

  const uint8_t r = static_cast<uint8_t>(kPalette[idx][0] + (kPalette[idx2][0] - kPalette[idx][0]) * localFrac);
  const uint8_t g = static_cast<uint8_t>(kPalette[idx][1] + (kPalette[idx2][1] - kPalette[idx][1]) * localFrac);
  const uint8_t b = static_cast<uint8_t>(kPalette[idx][2] + (kPalette[idx2][2] - kPalette[idx][2]) * localFrac);

  // RGB888 -> RGB565.
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
