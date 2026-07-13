#pragma once

// Covers MLX90640 (24x32=768) and AMG8833 (8x8=64) frame sizes with static
// (non-heap) working buffers, appropriate for an embedded target.
constexpr int kMaxFramePixels = 768;

struct Hotspot {
  float row;          // temperature-weighted centroid row
  float col;          // temperature-weighted centroid col
  float peak_temp_c;
  int pixel_count;
};

// C++ port of simulation/hotspot_detector.py (PROJECT_PLAN.md section 4):
//   1. ambient = median(frame)
//   2. hot_mask[i] = frame[i] > ambient + delta_c
//   3. 4-connected iterative flood fill (BFS, explicit queue) into blobs
//   4. discard blobs smaller than min_pixels
//   5. report each blob's temperature-weighted centroid + peak temp
class HotspotDetector {
public:
  HotspotDetector(float delta_c, int min_pixels)
      : delta_c_(delta_c), min_pixels_(min_pixels) {}

  // frame is row-major rows*cols temperatures (deg C), rows*cols <=
  // kMaxFramePixels. Writes up to max_hotspots blobs into out, returns the
  // number of blobs found.
  int detect(const float* frame, int rows, int cols, Hotspot* out, int max_hotspots) const;

private:
  float delta_c_;
  int min_pixels_;
};
