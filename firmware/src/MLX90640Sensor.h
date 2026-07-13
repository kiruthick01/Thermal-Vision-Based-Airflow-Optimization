#pragma once

#if defined(SENSOR_MODE_MLX90640)

#include <Adafruit_MLX90640.h>

#include "ThermalSensor.h"

class MLX90640Sensor : public ThermalSensor {
public:
  bool begin() override;
  bool readFrame(float* out, int rows, int cols) override;
  int rows() const override { return kRows; }
  int cols() const override { return kCols; }

private:
  static constexpr int kRows = 24;
  static constexpr int kCols = 32;
  Adafruit_MLX90640 mlx_;
};

#endif  // SENSOR_MODE_MLX90640
