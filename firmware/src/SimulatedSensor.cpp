#include "SimulatedSensor.h"

#include <cmath>

SimulatedSensor::SimulatedSensor(int rows, int cols, unsigned long seed)
    : rows_(rows), cols_(cols), rngState_(seed ? seed : 1), frameCount_(0) {}

bool SimulatedSensor::begin() { return true; }

float SimulatedSensor::randomFloat() {
  // xorshift32
  rngState_ ^= rngState_ << 13;
  rngState_ ^= rngState_ >> 17;
  rngState_ ^= rngState_ << 5;
  return (rngState_ & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

bool SimulatedSensor::readFrame(float* out, int rows, int cols) {
  if (rows != rows_ || cols != cols_) return false;

  const float ambient = 24.0f + 0.5f * sinf(frameCount_ * 0.01f);
  for (int i = 0; i < rows * cols; ++i) out[i] = ambient;

  const int numBlobs = 1 + static_cast<int>(randomFloat() * 3.0f);
  for (int b = 0; b < numBlobs; ++b) {
    const float cr = randomFloat() * (rows - 1);
    const float cc = randomFloat() * (cols - 1);
    const float peakDelta = 6.0f + randomFloat() * 6.0f; // 6-12 C above ambient
    const float sigma = 1.0f + randomFloat() * 1.5f;

    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        const float dr = r - cr;
        const float dc = c - cc;
        const float dist2 = dr * dr + dc * dc;
        const float bump = peakDelta * expf(-dist2 / (2.0f * sigma * sigma));
        out[r * cols + c] += bump;
      }
    }
  }

  ++frameCount_;
  return true;
}
