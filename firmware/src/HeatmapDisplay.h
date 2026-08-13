#pragma once

#include <Adafruit_ILI9341.h>
#include <SPI.h>

#include "HotspotDetector.h"
#include "config.h"

// Renders the current thermal frame as a false-color heatmap on a 2.4"
// ILI9341 SPI TFT (320x240 landscape), with the detected hotspots
// overlaid as markers. Purely a visualization -- reads the same frame/
// hotspot data main.cpp already has, doesn't feed back into detection or
// control logic anywhere.
//
// Wiring: config.h kTftSckPin/kTftMisoPin/kTftMosiPin (ESP32 hardware
// VSPI bus) + kTftCsPin/kTftDcPin/kTftRstPin. Backlight (LED) wired
// straight to 3V3 on the physical board.
class HeatmapDisplay {
public:
  bool begin();

  // frame is row-major rows*cols temperatures (deg C). spots/count is the
  // same HotspotDetector output already computed this frame -- pass it
  // straight through, no recomputation.
  void render(const float* frame, int rows, int cols, const Hotspot* spots, int count);

private:
  static constexpr int kScreenW = 320;
  static constexpr int kScreenH = 240;
  static constexpr int kStatusBarH = 20;

  // Render grid is fixed regardless of sensor resolution (8x8 AMG8833 or
  // 24x32 MLX90640/SIM) -- bilinearSample() upsamples whatever the source
  // frame is onto this grid, so the on-screen block size (and how smooth
  // the gradient looks) stays constant across sensors instead of showing
  // raw 40x28px cells for the coarse AMG8833.
  static constexpr int kRenderCols = 64;
  static constexpr int kRenderRows = 48;

  // Largest source frame this project's sensors produce (MLX90640/SIM's
  // 24x32); AMG8833's 8x8 fits easily within it.
  static constexpr int kMaxFramePixels = 24 * 32;

  SPIClass spi_{VSPI};
  Adafruit_ILI9341 tft_{&spi_, kTftDcPin, kTftCsPin, kTftRstPin};

  static uint16_t thermalColor565(float t, float tMin, float tMax);
  // Interpolated temperature at continuous source-grid coordinate (fx, fy)
  // -- fx in [0, cols-1], fy in [0, rows-1]. Standard 4-neighbor bilinear.
  static float bilinearSample(const float* frame, int rows, int cols, float fx, float fy);
  // 3x3 box blur (edge-clamped) into dst, display-only -- softens the
  // sensor's normal per-pixel measurement noise before the 6x render-grid
  // upsampling amplifies it into visible banding across steep gradients.
  // Never touches the caller's original frame, which HotspotDetector
  // already ran on upstream -- detection accuracy is unaffected.
  static void smoothFrame(const float* src, float* dst, int rows, int cols);
};
