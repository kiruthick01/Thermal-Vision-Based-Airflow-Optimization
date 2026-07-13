#pragma once

#include "ThermalSensor.h"

// Generates synthetic hotspot blobs in-process (same idea as
// simulation/thermal_scene_simulator.py: an ambient baseline plus a few
// Gaussian warm blobs at randomized positions), entirely in C++, no network
// dependency. This is what runs until real hardware arrives.
class SimulatedSensor : public ThermalSensor {
public:
  SimulatedSensor(int rows, int cols, unsigned long seed = 42);

  bool begin() override;
  bool readFrame(float* out, int rows, int cols) override;
  int rows() const override { return rows_; }
  int cols() const override { return cols_; }

private:
  int rows_;
  int cols_;
  unsigned long rngState_;
  unsigned long frameCount_;

  float randomFloat(); // deterministic xorshift32, no <random>/heap
};
