#ifndef VIDEO_SDL_H
#define VIDEO_SDL_H

/*  Back‑end: SDL2 + OpenGL ------------------------------------------- */
#include <SDL.h>
#include <SDL_opengl.h>
#define glGetProcAddress(name) SDL_GL_GetProcAddress(name)

#include "video.h"
#include "../globals.h"

/* --------------------------------------------------------------------
 *  SDL implementation of abstract Video
 * ----------------------------------------------------------------- */
class VideoSdl : public Video
{
private:
    uintptr_t handle;                 /* native window handle if provided */

public:
    explicit VideoSdl(uintptr_t h) : Video(), handle(h) {}

    /* --------------------------------------------------------------
     *  Initialise SDL window and GL context
     * ----------------------------------------------------------- */
    void video_init(int width, int height, const Settings::Video& settings)
    {
        if(SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
            printf("Unable to init SDL Video:\n%s\n", SDL_GetError());
            exit(1);
        }

        /* OpenGL attributes */
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE,   8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,  8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

        if(settings.multisampling) {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 1 << settings.multisampling);
        }

        SDL_GL_SetSwapInterval(settings.vsync);   /* vsync on/off */

        if(!g_window) {
            g_window = SDL_CreateWindow(nullptr,
                                        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                        width, height,
                                        SDL_WINDOW_OPENGL);
            if(!g_window) {
                SDL_Log("Unable to create game window: %s", SDL_GetError());
                exit(1);
            }
        }

        SDL_SetWindowSize(g_window, width, height);
        glViewport(0, 0, width, height);
        SDL_RaiseWindow(g_window);

        if(glContext) SDL_GL_DeleteContext(glContext);
        glContext = SDL_GL_CreateContext(g_window);
        if(!glContext) {
            SDL_Log("OpenGL context could not be created: %s", SDL_GetError());
            SDL_DestroyWindow(g_window);
            exit(1);
        }

        SDL_GL_MakeCurrent(g_window, glContext);
        SDL_SetWindowInputFocus(g_window);
        SDL_ShowCursor(SDL_DISABLE);

        /* Delegate common initialisation to base class */
        Video::video_init(width, height, settings);
    }

    /* --------------------------------------------------------------
     *  Present frame and (optionally) capture it
     * ----------------------------------------------------------- */
    void swap_buffers()
    {
        /* If the game uploads textures or flushes GL, that code would be here */
        glFlush();                         /* minimal flush for safety */

        SDL_GL_SwapWindow(g_window);       /* present back‑buffer */

        if(capture_enabled)                /* grab final pixels */
            dumpPPM(frame_dir, frameCounter());
    }

    /* -------------------------------------------------------------- */
    void show_cursor(bool show) override
    {
        SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
    }
};

#endif /* VIDEO_SDL_H */
