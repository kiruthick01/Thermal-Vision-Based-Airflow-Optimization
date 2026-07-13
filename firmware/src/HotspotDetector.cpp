#include "HotspotDetector.h"

#include <algorithm>
#include <cstring>

namespace {

float computeMedian(const float* frame, int n) {
  static float sorted[kMaxFramePixels];
  std::memcpy(sorted, frame, sizeof(float) * n);
  std::sort(sorted, sorted + n);
  return sorted[n / 2];
}

}  // namespace

int HotspotDetector::detect(const float* frame, int rows, int cols, Hotspot* out,
                             int max_hotspots) const {
  const int n = rows * cols;
  const float ambient = computeMedian(frame, n);
  const float threshold = ambient + delta_c_;

  static bool hot_mask[kMaxFramePixels];
  static bool visited[kMaxFramePixels];
  for (int i = 0; i < n; ++i) {
    hot_mask[i] = frame[i] > threshold;
    visited[i] = false;
  }

  static int queue[kMaxFramePixels];
  int found = 0;

  for (int start = 0; start < n; ++start) {
    if (!hot_mask[start] || visited[start]) continue;

    // 4-connected iterative flood fill via explicit BFS queue (no recursion).
    int head = 0, tail = 0;
    queue[tail++] = start;
    visited[start] = true;

    int pixel_count = 0;
    double weighted_row_sum = 0.0;
    double weighted_col_sum = 0.0;
    double weight_sum = 0.0;
    float peak_temp = frame[start];

    while (head < tail) {
      const int idx = queue[head++];
      const int r = idx / cols;
      const int c = idx % cols;
      const float temp = frame[idx];

      ++pixel_count;
      weighted_row_sum += static_cast<double>(r) * temp;
      weighted_col_sum += static_cast<double>(c) * temp;
      weight_sum += temp;
      if (temp > peak_temp) peak_temp = temp;

      static const int dr[4] = {-1, 1, 0, 0};
      static const int dc[4] = {0, 0, -1, 1};
      for (int d = 0; d < 4; ++d) {
        const int nr = r + dr[d];
        const int nc = c + dc[d];
        if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
        const int nidx = nr * cols + nc;
        if (hot_mask[nidx] && !visited[nidx]) {
          visited[nidx] = true;
          queue[tail++] = nidx;
        }
      }
    }

    if (pixel_count >= min_pixels_ && found < max_hotspots) {
      out[found].row = static_cast<float>(weighted_row_sum / weight_sum);
      out[found].col = static_cast<float>(weighted_col_sum / weight_sum);
      out[found].peak_temp_c = peak_temp;
      out[found].pixel_count = pixel_count;
      ++found;
    }
  }

  return found;
}
