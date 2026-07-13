#include "AMG8833Sensor.h"

#if defined(SENSOR_MODE_AMG8833)

bool AMG8833Sensor::begin() { return amg_.begin(); }

bool AMG8833Sensor::readFrame(float* out, int rows, int cols) {
  if (rows != kRows || cols != kCols) return false;
  amg_.readPixels(out);
  return true;
}

#endif  // SENSOR_MODE_AMG8833
