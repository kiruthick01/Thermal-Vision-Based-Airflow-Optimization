#include "MLX90640Sensor.h"

#if defined(SENSOR_MODE_MLX90640)

#include <Wire.h>

bool MLX90640Sensor::begin() {
  if (!mlx_.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) return false;
  mlx_.setMode(MLX90640_CHESS);
  mlx_.setResolution(MLX90640_ADC_18BIT);
  mlx_.setRefreshRate(MLX90640_4_HZ);
  return true;
}

bool MLX90640Sensor::readFrame(float* out, int rows, int cols) {
  if (rows != kRows || cols != kCols) return false;
  return mlx_.getFrame(out) == 0;
}

#endif  // SENSOR_MODE_MLX90640
