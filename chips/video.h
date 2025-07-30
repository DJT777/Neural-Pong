#ifndef VIDEO_H
#define VIDEO_H

/*  Public “front door” for every Video back‑end (SDL, Qt, …)            */
#include <SDL.h>

#include "../chip_desc.h"
#include "../video_desc.h"
#include "../settings.h"
#include "../globals.h"
#include <string>

class Video
{
protected:
    /* ------------ timing state used by custom chip ------------------- */
    uint64_t scanline_time;
    uint64_t current_time;
    uint64_t initial_time;

    uint32_t v_size;
    uint32_t v_pos;

    std::vector<float> color;                 /* 24‑bit RGB LUT */

    /* helpers used internally by custom logic ------------------------- */
    void adjust_screen_params();
    void draw(class Chip* chip);
    void draw_overlays();
    void init_color_lut(const double (*r)[3]);

public:
    const VideoDesc* desc;                    /* set by Circuit */
    uint32_t frame_count;                     /* ++ every VSYNC */

    /* ---------- capture helpers (added once, no duplicates) ---------- */
    bool        capture_enabled = false;      /* Circuit sets true when --dump‑state‑frame DIR used */
    std::string frame_dir;                    /* Directory for frame_########.ppm files            */

    enum VideoPins { HBLANK_PIN = 9, VBLANK_PIN = 10 };

    /* Back‑end may store GL context pointer here. */
    SDL_GLContext glContext = nullptr;

    Video();
    virtual ~Video();

    /* Initialisation, swap, cursor control implemented per back‑end --- */
    virtual void video_init(int width, int height,
                            const Settings::Video& settings);
    virtual void swap_buffers()   = 0;
    virtual void show_cursor(bool show) = 0;

    /* Entry point for VIDEO custom chip -------------------------------- */
    static CUSTOM_LOGIC( video );

    /* Factory: returns default back‑end instance ----------------------- */
    static Video* createDefault(phoenix::VerticalLayout& layout,
                                phoenix::Viewport*& viewport);

    /* Small helpers ---------------------------------------------------- */
    uint32_t frameCounter() const noexcept { return frame_count; }

    /* Capture finished frame to PPM ----------------------------------- */
    void dumpPPM(const std::string& dir, uint32_t idx) const;
};

extern CHIP_DESC( VIDEO );

#endif /* VIDEO_H */
