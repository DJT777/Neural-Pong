// DICE State‑Dump Support
// © 2025  (licence header matches other DICE sources)
#pragma once
#include <fstream>
#include <vector>
#include <cstdint>

class Chip;                       // forward declaration (see chip.h)

/* When to sample */
enum class SampleMode { Tick, FrameEdge };

class StateRecorder {
public:
  StateRecorder(const std::string& file,
                unsigned           chip_count,
                SampleMode         mode = SampleMode::Tick);

  ~StateRecorder();

  void sample(uint64_t time_ps, const std::vector<Chip*>& chips);

  SampleMode mode() const noexcept { return mode_; }

private:
  std::ofstream        out_;
  std::vector<uint8_t> row_;
  SampleMode           mode_;
};
