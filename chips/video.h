#ifndef VIDEO_H
#define VIDEO_H

class Video;

#include <SDL.h>
#include "../chip_desc.h"
#include "../video_desc.h"
#include "../settings.h"
#include "../globals.h"

class Video
{
protected:
    uint64_t scanline_time;
    uint64_t current_time;
    uint64_t initial_time;

    uint32_t v_size;
    uint32_t v_pos;

    std::vector<float> color;

    void adjust_screen_params();
    void draw(Chip* chip);
    void draw_overlays();
    void init_color_lut(const double (*r)[3]);

public:
    const VideoDesc* desc;
    uint32_t frame_count;
    enum VideoPins { HBLANK_PIN = 9, VBLANK_PIN = 10 };

    SDL_GLContext glContext = nullptr;
    
    Video();
    ~Video();
    virtual void video_init(int width, int height, const Settings::Video& settings);
    virtual void swap_buffers() = 0;
    virtual void show_cursor(bool show) = 0;
    static CUSTOM_LOGIC( video );

    static Video* createDefault(phoenix::VerticalLayout& layout, phoenix::Viewport*& viewport);
    uint32_t frameCounter() const noexcept { return frame_count; }
};

extern CHIP_DESC( VIDEO );

#endif
