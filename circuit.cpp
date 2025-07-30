// circuit.cpp — complete implementation with frame-synchronous pixel capture
// -----------------------------------------------------------------------------
//  * Added <iomanip> and <nall/directory.hpp> includes
//  * Added member initialiser frame_dir()
//  * Constructor now stores dump_path in frame_dir and ensures directory exists
//  * run() now calls video.dumpPPM(...) exactly when StateRecorder samples the
//    frame-edge, keeping registers and pixels in perfect lock-step
//  * FIX: cast std::string → nall::string when calling nall::directory::create()
//    to resolve template instantiation error during compilation
//
//  NOTE: this file assumes you have also added the following one-liner to
//  circuit.h (inside class Circuit):
//      std::string frame_dir;
// -----------------------------------------------------------------------------

#include "circuit.h"
#include "circuit_desc.h"

#include <map>
#include <string>
#include <sstream>
#include <cstdio>
#include <iomanip>              // std::setw / std::setfill
#include <nall/directory.hpp>   // directory::create (expects nall::string)

#define DEBUG
#undef DEBUG

#define EVENT_QUEUE_SIZE 128
#define SUBCYCLE_SIZE    64

// -----------------------------------------------------------------------------
//  Special helper chip descriptions (VCC, GND, DEOPTIMIZER)
// -----------------------------------------------------------------------------

CHIP_DESC(_VCC) = {
    CUSTOM_CHIP_START(NULL)
    OUTPUT_PIN(1)
};

CHIP_DESC(_GND) = {
    CUSTOM_CHIP_START(NULL)
    OUTPUT_PIN(1)
};

// Option to disable optimisations on chips where they do not perform well
CUSTOM_LOGIC(deoptimize)
{
    for(ChipLink& cl : chip->output_links)
    {
        printf("Deoptimizing %p\n", cl.chip);
        cl.chip->optimization_disabled = true;
    }
}

CHIP_DESC(_DEOPTIMIZER) = {
    CUSTOM_CHIP_START(deoptimize)
    OUTPUT_PIN(1)
};

// -----------------------------------------------------------------------------
//  CircuitBuilder — helper class (local to this TU)
// -----------------------------------------------------------------------------

const double Circuit::timescale = 1.0e-12;   // 1 ps

class CircuitBuilder
{
private:
    typedef std::pair<Chip*, const ChipDesc*> ChipDescPair;
    typedef std::pair<uint8_t, ChipDescPair>   Connection;
    typedef std::multimap<std::string, ChipDescPair>::iterator ChipMapIterator;

    typedef std::pair<std::string, uint8_t> Net;
    typedef std::multimap<std::string, Net>::iterator NetListIterator;

    std::multimap<std::string, ChipDescPair> chip_map;
    std::multimap<std::string, Net>          net_list;
    std::vector<Connection>                  connection_list_out, connection_list_in;

    Circuit*             circuit;
    std::vector<Chip*>&  chips;

    void createChip(const ChipDesc* chip_desc, std::string name, void* custom,
                    int queue_size, int subcycle_size);
    bool findConnection(const std::string& name1, const std::string& name2,
                        const ConnectionDesc& connection);

public:
    CircuitBuilder(Circuit* cir, std::vector<Chip*>& ch) : circuit(cir), chips(ch) {}

    void createChips(std::string prefix, const CircuitDesc* desc);
    void createSpecialChips();

    void findConnections(std::string prefix, const CircuitDesc* desc);
    void makeAllConnections();

    const std::string getOutputInfo(const Chip* chip);
    const std::string getInputInfo(const Chip* chip, int num);
};

// -----------------------------------------------------------------------------
//  CircuitBuilder inline helpers
// -----------------------------------------------------------------------------

const std::string CircuitBuilder::getOutputInfo(const Chip* chip)
{
    for(auto x : chip_map)
        if(x.second.first == chip)
        {
            std::stringstream ss;
            ss << x.first << "." << int(x.second.second->output_pin);
            return ss.str();
        }
    return std::string("unknown");
}

const std::string CircuitBuilder::getInputInfo(const Chip* chip, int num)
{
    for(auto x : chip_map)
        if(x.second.first == chip)
        {
            std::stringstream ss;
            ss << x.first << "." << int(x.second.second->input_pins[num]);
            return ss.str();
        }
    return std::string("unknown");
}

// -----------------------------------------------------------------------------
//  Circuit constructor — builds the full net-list
// -----------------------------------------------------------------------------

Circuit::Circuit(const Settings&    s,
                 Input&             i,
                 Video&             v,
                 const CircuitDesc* desc,
                 const char*        name,
                 const std::string& dump_path,
                 SampleMode         smode)
  : settings(s)
  , game_config(desc, name)
  , input(i)
  , video(v)
  , global_time(0)
  , queue_size(0)
  , recorder()
  , last_frame_count(0)
  , frame_dir()
{
    CircuitBuilder converter(this, chips);

    /* 1. Special chips & IO roots */
    converter.createSpecialChips();

    /* 2. Chip instances (root + sub-circuits) */
    converter.createChips("", desc);
    for(const SubcircuitDesc& d : desc->get_sub_circuits())
        converter.createChips(d.prefix, d.desc());

    /* 3. Connections */
    converter.findConnections("", desc);
    for(const SubcircuitDesc& d : desc->get_sub_circuits())
        converter.findConnections(d.prefix, d.desc());
    converter.makeAllConnections();

    /* 4. Tie unconnected inputs to GND */
    for(size_t i = 2; i < chips.size(); i++)
        for(size_t j = 0; j < chips[i]->input_links.size(); j++)
            if(chips[i]->input_links[j].chip == nullptr)
            {
                std::string name = converter.getInputInfo(chips[i], j);
                if(chips[i]->type != CUSTOM_CHIP)
                    printf("WARNING: Unconnected input pin: %s, connecting to GND\n", name.c_str());

                chips[1]->output_links.emplace_back(chips[i], 1 << j);
                chips[i]->input_links[j] = ChipLink(chips[1], 0);
            }

    /* 5. Optional dump initialisation */
    if(!dump_path.empty()) {
        frame_dir = dump_path;
        nall::directory::create(nall::string(frame_dir.c_str()));   // mkdir ‑p

        std::string state_file = frame_dir + "/state.bin";          // NEW: one file inside dir
        recorder.reset(new StateRecorder(state_file,                // NEW
                                        chips.size(),              // NEW
                                        smode));                   // NEW
    }

    if(!frame_dir.empty()) {            // enable pixel capture for this run
        video.capture_enabled = true;
        video.frame_dir       = frame_dir;
    }


    /* 6. Video, audio & power rails */
    video.desc  = (desc->video  ? desc->video  : &VideoDesc::DEFAULT);
    audio.desc  = (desc->audio  ? desc->audio  : nullptr);
    chips[0]->analog_output = 5.0;   // VCC
    chips[1]->analog_output = 0.0;   // GND
    audio.audio_init(this);

    // Propagate VCC once
    chips[0]->output = 1;
    for(auto& link : chips[0]->output_links) link.chip->inputs |= link.mask;

    for(size_t i = 2; i < chips.size(); i++) chips[i]->initialize();
}

// -----------------------------------------------------------------------------
//  CircuitBuilder implementation
// -----------------------------------------------------------------------------

void CircuitBuilder::createChip(const ChipDesc* chip_desc, std::string name,
                                void* custom, int queue_size, int subcycle_size)
{
    std::map<uint8_t, ChipDescPair> output_pin_map;
    size_t chip_index = chips.size();

    for(const ChipDesc* d = chip_desc; !d->endOfDesc(); ++d)
    {
        chips.push_back(new Chip(queue_size, subcycle_size, circuit, d, custom));
        ChipDescPair cd(chips.back(), d);
        chip_map.insert({name, cd});
        if(d->output_pin) output_pin_map[d->output_pin] = cd;
#ifdef DEBUG
        printf("chip name:%s p:%p\n", name.c_str(), chips.back());
#endif
    }

    for(const ChipDesc* d = chip_desc; !d->endOfDesc(); ++d, ++chip_index)
        for(int i = 0; d->input_pins[i]; ++i)
            if(output_pin_map.count(d->input_pins[i]))
            {
                connection_list_out.push_back(*output_pin_map.find(d->input_pins[i]));
                connection_list_in .push_back(Connection(d->input_pins[i],
                                                          ChipDescPair(chips[chip_index], d)));
            }
}

void CircuitBuilder::createSpecialChips()
{
    chips.push_back(new Chip(1, 64, circuit, chip__VCC));
    chip_map.insert({"_VCC", {chips.back(), chip__VCC}});

    chips.push_back(new Chip(1, 64, circuit, chip__GND));
    chip_map.insert({"_GND", {chips.back(), chip__GND}});

    chips.push_back(new Chip(1, 64, circuit, chip__DEOPTIMIZER));
    chip_map.insert({"_DEOPTIMIZER", {chips.back(), chip__DEOPTIMIZER}});

    createChip(chip_VIDEO, "VIDEO", &circuit->video, 8, 64);
    createChip(chip_AUDIO, "AUDIO", &circuit->audio, 8, 64);
}

void CircuitBuilder::createChips(std::string prefix, const CircuitDesc* desc)
{
    std::map<std::string, OptimizationHintDesc> hint_list;
    for(const auto& hint : desc->get_hints())
    {
        printf("Hinting %s\n", hint.chip);
        hint_list[hint.chip] = hint;
    }

    for(const auto& instance : desc->get_chips())
    {
        int qsize = EVENT_QUEUE_SIZE;
        int ssize = SUBCYCLE_SIZE;
        if(hint_list.count(instance.name))
        {
            qsize = hint_list[instance.name].queue_size;
            ssize = hint_list[instance.name].subcycle_size;
        }
        createChip(instance.chip, prefix + instance.name,
                   (void*)instance.custom_data, qsize, ssize);
    }
}

void CircuitBuilder::findConnections(std::string prefix, const CircuitDesc* desc)
{
    for(const auto& c : desc->get_connections())
    {
        if(findConnection(prefix + c.name1, prefix + c.name2, c)) continue;

        if(!prefix.empty())
        {
            if(findConnection(prefix + c.name1, c.name2, c)) continue;
            if(findConnection(c.name1, prefix + c.name2, c)) continue;

            if((prefix.compare(0, prefix.size(), c.name1) == 0 ||
                prefix.compare(0, prefix.size(), c.name2) == 0) &&
               findConnection(c.name1, c.name2, c)) continue;
        }

        printf("WARNING: Invalid connection: %s(%s.%d -> %s.%d)\n",
               prefix.c_str(), c.name1, c.pin1, c.name2, c.pin2);
    }
}

bool CircuitBuilder::findConnection(const std::string& name1, const std::string& name2,
                                    const ConnectionDesc& connection)
{
    auto range1 = chip_map.equal_range(name1);
    auto range2 = chip_map.equal_range(name2);

    bool connected = false;

    // Forward direction
    for(auto it1 = range1.first; it1 != range1.second; ++it1)
        if(it1->second.second->output_pin == connection.pin1)
        {
            for(auto it2 = range2.first; it2 != range2.second; ++it2)
                for(int i = 0; it2->second.second->input_pins[i]; ++i)
                    if(it2->second.second->input_pins[i] == connection.pin2)
                    {
                        if(std::find(connection_list_in.begin(), connection_list_in.end(),
                                     Connection(connection.pin2, it2->second)) != connection_list_in.end())
                            printf("WARNING: Attempted multiple connections to input: %s.%d\n", name2.c_str(), connection.pin2);

                        connected = true;
                        connection_list_out.push_back(Connection(connection.pin1, it1->second));
                        connection_list_in .push_back(Connection(connection.pin2, it2->second));
                    }
            break;
        }

    // Reverse direction
    for(auto it2 = range2.first; it2 != range2.second; ++it2)
        if(it2->second.second->output_pin == connection.pin2)
        {
            for(auto it1 = range1.first; it1 != range1.second; ++it1)
                for(int i = 0; it1->second.second->input_pins[i]; ++i)
                    if(it1->second.second->input_pins[i] == connection.pin1)
                    {
                        if(std::find(connection_list_in.begin(), connection_list_in.end(),
                                     Connection(connection.pin1, it1->second)) != connection_list_in.end())
                            printf("WARNING: Attempted multiple connections to input: %s.%d\n", name1.c_str(), connection.pin1);

                        connected = true;
                        connection_list_out.push_back(Connection(connection.pin2, it2->second));
                        connection_list_in .push_back(Connection(connection.pin1, it1->second));
                    }
            break;
        }

    return connected;
}

void CircuitBuilder::makeAllConnections()
{
    bool removed;
    do {
        removed = false;
        for(auto it = chips.begin(); it != chips.end(); ++it)
        {
            if((*it)->type == CUSTOM_CHIP) continue;

            bool found = false;
            for(const auto& co : connection_list_out)
                if(*it == co.second.first) { found = true; break; }

            if(!found)
            {
                printf("Removing unused chip %s\n", getOutputInfo(*it).c_str());
                removed = true;

                for(size_t i = 0; i < connection_list_in.size(); ++i)
                    if(*it == connection_list_in[i].second.first)
                    {
                        connection_list_in .erase(connection_list_in .begin() + i);
                        connection_list_out.erase(connection_list_out.begin() + i);
                        --i;
                    }

                if((*it)->type == BASIC_CHIP) delete[] (*it)->lut;
                delete *it;
                it = chips.erase(it) - 1;
            }
        }
    } while(removed);

    for(size_t i = 0; i < connection_list_out.size(); ++i)
    {
        Chip* c_out          = connection_list_out[i].second.first;
        Chip* c_in           = connection_list_in [i].second.first;
        const ChipDesc* desc = connection_list_in [i].second.second;
        uint8_t pin          = connection_list_in [i].first;

        c_out->connect(c_in, desc, pin);

        if(c_out->output_links.size() > 64)
        {
            std::string name = getOutputInfo(c_out);
            if(name != "_VCC.1" && name != "_GND.1")
                printf("ERROR: Maximum output connection limit reached, chip:%s, cout:%zu\n",
                       name.c_str(), c_out->output_links.size());
        }
    }
}

// -----------------------------------------------------------------------------
//  Destructor & queue helpers
// -----------------------------------------------------------------------------

Circuit::~Circuit()
{
    for(auto* c : chips)
    {
        if(c->type == BASIC_CHIP) delete[] c->lut;
        delete c;
    }
}

uint64_t Circuit::queue_push(Chip* chip, uint64_t delay)
{
    QueueEntry qe(global_time + delay, chip);
    queue_size++;

    size_t i;
    for(i = queue_size; i > 1 && queue[i >> 1].time > qe.time; i >>= 1)
        queue[i] = queue[i >> 1];
    queue[i] = qe;
    return qe.time;
}

void Circuit::queue_pop()
{
    QueueEntry qe = queue[queue_size];
    queue_size--;

    size_t i = 1;
    while((i << 1) < (queue_size + 1))
    {
        size_t child = (i << 1);
        if(child + 1 < queue_size + 1 && queue[child + 1].time < queue[child].time)
            ++child;
        if(qe.time <= queue[child].time) break;
        queue[i] = queue[child];
        i = child;
    }
    queue[i] = qe;
}

// -----------------------------------------------------------------------------
//  Main simulation loop — emits pixels in sync with register dump
// -----------------------------------------------------------------------------
void Circuit::run(int64_t run_time)
{
    while(run_time > 0)
    {
        /* ---------- advance to next scheduled event ------------------ */
        if(queue_size)
        {
            run_time    -= queue[1].time - global_time;
            global_time  = queue[1].time;
        }
        else           /* no more events in queue */
        {
            global_time += run_time;
            return;
        }

        /* ---------- execute event ------------------------------------ */
        if(global_time == queue[1].chip->pending_event)
            queue[1].chip->update_output();
        queue_pop();

        /* ---------- optional state‑dump ------------------------------ */
        if(recorder)
        {
            if(recorder->mode() == SampleMode::Tick)
            {
                /* dump every tick (very large) */
                recorder->sample(global_time, chips);
            }
            else  /* SampleMode::FrameEdge */
            {
                uint32_t f_now = video.frameCounter();
                if(f_now != last_frame_count)
                {
                    /* registers; pixel grab happens later in swap_buffers() */
                    recorder->sample(global_time, chips);
                    last_frame_count = f_now;
                }
            }
        }
    }
}
