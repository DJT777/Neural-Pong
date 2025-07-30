#ifndef CIRCUIT_H
#define CIRCUIT_H

#include <stdint.h>
#include <vector>
#include <string>        // ← needed for std::string
#include <memory>        // ← needed for std::unique_ptr

#include "settings.h"
#include "game_config.h"

#include "chip.h"
#include "realtime.h"
#include "chips/video.h"
#include "chips/audio.h"
#include "chips/input.h"

#include "state_dump.h"  // ← new: state‑dump support

#define MAX_QUEUE_SIZE 4096

class CircuitDesc;

struct QueueEntry
{
    uint64_t time;
    Chip*    chip;

    QueueEntry(uint64_t t = 0, Chip* c = nullptr) : time(t), chip(c) {}
};

class Circuit
{
public:
    /* existing public members */
    std::vector<Chip*> chips;
    uint64_t           global_time;

    const Settings& settings;
    GameConfig      game_config;
    Input&          input;
    Video&          video;
    Audio           audio;
    RealTimeClock   rtc;

    int         queue_size;
    QueueEntry  queue[MAX_QUEUE_SIZE];

    /* new recorder members */
    std::unique_ptr<StateRecorder> recorder;   // owns the dump file
    uint32_t                        last_frame_count = 0;

    /* updated constructor */
    Circuit(const Settings&  s,
            Input&           i,
            Video&           v,
            const CircuitDesc* desc,
            const char*      name,
            const std::string& dump_path = "",
            SampleMode        smode      = SampleMode::Tick);

    ~Circuit();

    uint64_t queue_push(Chip* chip, uint64_t delay);
    void     queue_pop();
    void     run(int64_t time);

    static const double timescale;
};

#endif
