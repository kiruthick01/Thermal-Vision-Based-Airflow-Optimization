#include "HeatmapDisplay.h"

bool HeatmapDisplay::begin() {
  spi_.begin(kTftSckPin, kTftMisoPin, kTftMosiPin, kTftCsPin);
  // Default SPI clock (~40MHz) is too fast for breadboard jumper wiring --
  // showed up as a corrupted/snowy band on every frame. 20MHz fixed that.
  // (A separate banding artifact across steep color gradients turned out
  // to be sensor noise amplified by upsampling, not SPI timing -- dropping
  // the clock further to 10MHz had zero effect on it; see smoothFrame().)
  tft_.begin(20000000);
  // landscape: 320x240. rotation(1) corrupts the bottom quarter of the
  // panel on this clone (confirmed via a solid fillScreen test -- not a
  // render-logic bug, that specific MADCTL setting just doesn't fully
  // address this board's GRAM). rotation(3) is clean at this SPI speed.
  tft_.setRotation(3);
  tft_.fillScreen(ILI9341_BLACK);
  tft_.setTextColor(ILI9341_WHITE);
  tft_.setTextSize(1);
  return true;
}

void HeatmapDisplay::render(const float* frame, int rows, int cols, const Hotspot* spots, int count) {
  // Display-only smoothed copy -- HotspotDetector already ran on the raw
  // `frame` upstream in main.cpp, so this can't affect detection accuracy.
  float smoothed[kMaxFramePixels];
  smoothFrame(frame, smoothed, rows, cols);

  // Auto-scale the color gradient to this frame's actual range -- same
  // behavior as a real cheap thermal camera (no fixed absolute scale).
  float tMin = smoothed[0];
  float tMax = smoothed[0];
  const int n = rows * cols;
  for (int i = 1; i < n; ++i) {
    if (smoothed[i] < tMin) tMin = smoothed[i];
    if (smoothed[i] > tMax) tMax = smoothed[i];
  }
  if (tMax - tMin < 0.5f) tMax = tMin + 0.5f;  // avoid a degenerate flat gradient

  const int yOffset = kStatusBarH;
  const int gridH = kScreenH - kStatusBarH;

  // Cell boundaries computed as (index * span) / count -- multiplying
  // before dividing means each row/col's edge lands exactly where it
  // should (rounding, not truncating, per cell), so the last row/col
  // always reaches the canvas edge instead of leaving an undrawn gap
  // that shows raw/undefined GRAM on the panel.
  for (int rr = 0; rr < kRenderRows; ++rr) {
    // Inverted (rows - 1 - ...): source row 0 is the top of the sensor's
    // field of view, but this panel's clean (non-corrupting) rotation
    // setting draws increasing rr top-to-bottom on screen in the opposite
    // sense -- flip here, in software, rather than touch tft_'s rotation
    // register (the other rotation option corrupts GRAM on this clone).
    const float fy = (rows <= 1) ? 0.0f : (rows - 1) - rr / static_cast<float>(kRenderRows - 1) * (rows - 1);
    const int y0 = yOffset + (rr * gridH) / kRenderRows;
    const int y1 = yOffset + ((rr + 1) * gridH) / kRenderRows;
    for (int cc = 0; cc < kRenderCols; ++cc) {
      const float fx = (cols <= 1) ? 0.0f : cc / static_cast<float>(kRenderCols - 1) * (cols - 1);
      const int x0 = (cc * kScreenW) / kRenderCols;
      const int x1 = ((cc + 1) * kScreenW) / kRenderCols;
      const float t = bilinearSample(smoothed, rows, cols, fx, fy);
      tft_.fillRect(x0, y0, x1 - x0, y1 - y0, thermalColor565(t, tMin, tMax));
    }
  }

  // Hotspot markers: a white/black ring at each detected blob's centroid,
  // directly overlaid on the raw heatmap -- this is HotspotDetector's
  // output rendered on top of the data it was computed from. Scaled by the
  // *source* grid's cell size on screen, independent of the render grid
  // above.
  const float srcCellW = kScreenW / static_cast<float>(cols);
  const float srcCellH = (kScreenH - kStatusBarH) / static_cast<float>(rows);
  for (int i = 0; i < count; ++i) {
    const int cx = static_cast<int>(spots[i].col * srcCellW + srcCellW / 2.0f);
    const int cy = yOffset + static_cast<int>((rows - 1 - spots[i].row) * srcCellH + srcCellH / 2.0f);
    const int radius = static_cast<int>((srcCellW < srcCellH ? srcCellW : srcCellH) / 2.0f) + 2;
    tft_.drawCircle(cx, cy, radius, ILI9341_WHITE);
    tft_.drawCircle(cx, cy, radius + 1, ILI9341_BLACK);
  }

  // Status bar.
  tft_.fillRect(0, 0, kScreenW, kStatusBarH, ILI9341_BLACK);
  tft_.setCursor(2, 4);
  tft_.setTextColor(ILI9341_WHITE);
  tft_.printf("Hotspots: %d   %.1f-%.1fC", count, tMin, tMax);
}

void HeatmapDisplay::smoothFrame(const float* src, float* dst, int rows, int cols) {
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      float sum = 0.0f;
      int count = 0;
      for (int dr = -1; dr <= 1; ++dr) {
        const int rr = r + dr;
        if (rr < 0 || rr >= rows) continue;
        for (int dc = -1; dc <= 1; ++dc) {
          const int cc = c + dc;
          if (cc < 0 || cc >= cols) continue;
          sum += src[rr * cols + cc];
          ++count;
        }
      }
      dst[r * cols + c] = sum / count;
    }
  }
}

float HeatmapDisplay::bilinearSample(const float* frame, int rows, int cols, float fx, float fy) {
  const int x0 = static_cast<int>(fx);
  const int y0 = static_cast<int>(fy);
  const int x1 = (x0 + 1 < cols) ? x0 + 1 : x0;
  const int y1 = (y0 + 1 < rows) ? y0 + 1 : y0;
  const float tx = fx - x0;
  const float ty = fy - y0;

  const float top = frame[y0 * cols + x0] * (1.0f - tx) + frame[y0 * cols + x1] * tx;
  const float bottom = frame[y1 * cols + x0] * (1.0f - tx) + frame[y1 * cols + x1] * tx;
  return top * (1.0f - ty) + bottom * ty;
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
