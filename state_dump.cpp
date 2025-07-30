// DICE State‑Dump Support  © 2025
#include "state_dump.h"
#include "chip.h"
#include <algorithm>

StateRecorder::StateRecorder(const std::string& file,
                             unsigned chip_count,
                             SampleMode mode)
: out_(file, std::ios::binary)
, row_((chip_count + 7) >> 3, 0)
, mode_(mode)
{
  if(!out_) throw std::runtime_error("StateRecorder: cannot open " + file);
}

StateRecorder::~StateRecorder() { out_.close(); }

void StateRecorder::sample(uint64_t t, const std::vector<Chip*>& chips)
{
  std::fill(row_.begin(), row_.end(), 0);

  for(std::size_t i = 0; i < chips.size(); ++i)
    if(chips[i]->output == 1)                 // 1 = logic‑high
      row_[i >> 3] |= 1u << (i & 7);

  out_.write(reinterpret_cast<char*>(&t), sizeof t);
  out_.write(reinterpret_cast<char*>(row_.data()), row_.size());
}
