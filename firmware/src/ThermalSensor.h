#pragma once

// Abstract thermal camera interface (PROJECT_PLAN.md section 3). main.cpp
// and everything downstream only ever talks to this interface -- swapping
// hardware later means swapping the implementation behind it, no logic
// changes anywhere else.
class ThermalSensor {
public:
  virtual ~ThermalSensor() = default;
  virtual bool begin() = 0;
  virtual bool readFrame(float* out, int rows, int cols) = 0; // row-major, deg C
  virtual int rows() const = 0;
  virtual int cols() const = 0;
};
