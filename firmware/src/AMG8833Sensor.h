#pragma once

#if defined(SENSOR_MODE_AMG8833)

#include <Adafruit_AMG88xx.h>

#include "ThermalSensor.h"

class AMG8833Sensor : public ThermalSensor {
public:
  bool begin() override;
  bool readFrame(float* out, int rows, int cols) override;
  int rows() const override { return kRows; }
  int cols() const override { return kCols; }

private:
  static constexpr int kRows = 8;
  static constexpr int kCols = 8;
  Adafruit_AMG88xx amg_;
};

#endif  // SENSOR_MODE_AMG8833
