/*--------------------------------------------------------------------
    DICE 0.9a  –  main.cpp
    (full source with optional state‑dump recording)
--------------------------------------------------------------------*/

#include <phoenix.hpp>
#include <nall/platform.hpp>
#include <nall/directory.hpp>
#include "manymouse/manymouse.h"

using namespace nall;
using namespace phoenix;

#include <SDL.h>
#include <SDL_opengl.h>

#include "globals.h"
#include "circuit.h"
#include "circuit_desc.h"
#include "game_list.h"

#include "chips/input.h"
#include "ui/audio_window.h"
#include "ui/video_window.h"
#include "ui/input_window.h"
#include "ui/dipswitch_window.h"
#include "ui/game_window.h"
#include "ui/logo.h"

#include <string>        // already used elsewhere
#include "state_dump.h"  // SampleMode enum  ←–––– new include

#undef  DEBUG
//#define DEBUG           // uncomment to dump chip stats

static const char VERSION_STRING[] = "DICE 0.9a";

/*====================================================================
    MainWindow
====================================================================*/
struct MainWindow : Window
{
    /* ---------- core emulator state ---------- */
    Settings settings;
    Input*   input;
    Video*   video;
    Circuit* circuit;
    RealTimeClock real_time;

    /* ==== state‑dump begin ==================================== */
    std::string dump_path;             // empty  ⇒ recording off
    SampleMode  smode = SampleMode::Tick;
    /* ==== state‑dump end   ==================================== */

    /* ---------- UI objects ---------- */
    Menu game_menu;
    GameWindow game_window;
    Item new_game_item, end_game_item;
    Separator game_sep[2];
    CheckItem pause_item, throttle_item;
    Item exit_item;

    Menu settings_menu;
    Item audio_item;
    CheckItem mute_item;
    AudioWindow audio_window;
    Separator settings_sep[3];
    Item video_item;
    VideoWindow video_window;
    CheckItem fullscreen_item, status_visible_item;
    Item input_item;
    InputWindow input_window;
    Item dipswitch_item;
    DipswitchWindow dipswitch_window;

    VerticalLayout layout;
    Viewport* viewport;

    /* ---------- UI‑input polling ---------- */
    struct UserInterfaceState
    {
        bool pause, throttle, fullscreen, quit;
        static UserInterfaceState getCurrent(MainWindow& m)
        {
            return { m.input->getKeyPressed(m.settings.input.ui.pause),
                     m.input->getKeyPressed(m.settings.input.ui.throttle),
                     m.input->getKeyPressed(m.settings.input.ui.fullscreen),
                     m.input->getKeyPressed(m.settings.input.ui.quit) };
        }
    };
    UserInterfaceState prev_ui_state;

    /*----------------------------------------------------------------
        Constructor
    ----------------------------------------------------------------*/
    MainWindow()
    : input(nullptr)
    , video(nullptr)
    , circuit(nullptr)
    , prev_ui_state{false,false,false,false}
    , audio_window(settings, mute_item)
    , video_window(settings, *this)
    , input_window(settings, input, [&]{ run(); })
    {
        /* ---------- load config ---------- */
        nall::string cfg_dir = configpath();
        cfg_dir.append("dice/");
        directory::create(cfg_dir);
        settings.filename = {cfg_dir, "settings.cfg"};
        settings.load();

        onClose = &Application::quit;

        /* =====================  GAME menu  ===================== */
        game_menu.setText("Game");

        new_game_item.setText("New Game...");
        new_game_item.onActivate = [&]{
            game_window.create(geometry().position());
        };

        game_window.cancel_button.onActivate = [&]{
            game_window.setModal(false);
            game_window.setVisible(false);
        };

        game_window.start_button.onActivate = [&]
        {
            GameDesc& g = game_list[game_window.game_view.selection()];
            if(circuit) delete circuit;

            /* ==== state‑dump begin */
            circuit = new Circuit(
                settings, *input, *video,
                g.desc, g.command_line,
                dump_path, smode);
            /* ==== state‑dump end   */

            game_window.setModal(false);
            game_window.setVisible(false);
            onSize();
        };

        game_menu.append(new_game_item);

        end_game_item.setText("End Game");
        end_game_item.onActivate = [&]{
            if(circuit){ delete circuit; circuit = nullptr; }
            onSize();
        };
        game_menu.append(end_game_item);

        game_menu.append(game_sep[0]);
        pause_item.setText("Pause");
        pause_item.onToggle = [&]{ settings.pause = pause_item.checked(); };

        throttle_item.setText("Throttle");
        throttle_item.setChecked(true);
        throttle_item.onToggle = [&]{
            settings.throttle = throttle_item.checked();
            if(settings.throttle && circuit)
            {
                uint64_t emu = circuit->global_time * 1000000.0 * Circuit::timescale;
                circuit->rtc += int64_t(circuit->rtc.get_usecs()) - emu;
            }
        };
        game_menu.append(pause_item, throttle_item);

        game_menu.append(game_sep[1]);
        exit_item.setText("Exit");
        exit_item.onActivate = onClose;
        game_menu.append(exit_item);

        append(game_menu);

        /* ===================== SETTINGS menu ==================== */
        settings_menu.setText("Settings");

        /* ---- audio submenu ---- */
        audio_item.setText("Audio Settings...");
        audio_item.onActivate = [&]{ audio_window.create(geometry().position()); };
        settings_menu.append(audio_item);

        audio_window.onClose = audio_window.exit_button.onActivate = [&]{
            mute_item.setChecked(settings.audio.mute);
            audio_window.setModal(false);
            audio_window.setVisible(false);
            if(circuit) circuit->audio.toggle_mute();
        };

        mute_item.setText("Mute Audio");
        mute_item.setChecked(settings.audio.mute);
        mute_item.onToggle = [&]{
            settings.audio.mute = mute_item.checked();
            if(circuit) circuit->audio.toggle_mute();
        };
        settings_menu.append(mute_item);
        settings_menu.append(settings_sep[0]);

        /* ---- video submenu ---- */
        video_item.setText("Video Settings...");
        video_item.onActivate = [&]{ video_window.create(geometry().position()); };
        settings_menu.append(video_item);

        status_visible_item.setText("Status Bar Visible");
        status_visible_item.setChecked(settings.video.status_visible);
        status_visible_item.onToggle = [&]{
            settings.video.status_visible = status_visible_item.checked();
            setStatusVisible(settings.video.status_visible);
        };
        settings_menu.append(settings_sep[1]);

        /* ---- input submenu ---- */
        input_item.setText("Configure Inputs...");
        input_item.onActivate = [&]{ input_window.create(geometry().position()); };
        settings_menu.append(input_item);

        input_window.onClose = [&]{
            if(input_window.active_selector)
                input_window.active_selector->assign(KeyAssignment::None);
            input_window.setModal(false);
            input_window.setVisible(false);
        };

        input_window.exit_button.onActivate = [&]{
            if(input_window.active_selector)
                input_window.active_selector->assign(KeyAssignment::None);
            else
            {
                input_window.setModal(false);
                input_window.setVisible(false);
            }
        };

        /* ---- DIP switch submenu ---- */
        settings_menu.append(settings_sep[2]);

        dipswitch_item.setText("Configure DIP Switches...");
        dipswitch_item.onActivate = [&]{
            int sel = 0;
            if(circuit)
                for(int i=0;i<dipswitch_window.game_configs.size();++i)
                    if(circuit->game_config == dipswitch_window.game_configs[i])
                        { sel = i; break; }
            dipswitch_window.create(geometry().position(), sel);
        };
        settings_menu.append(dipswitch_item);

        dipswitch_window.onClose = dipswitch_window.exit_button.onActivate = [&]{
            dipswitch_window.game_configs[dipswitch_window.current_config].save();
            dipswitch_window.setModal(false);
            dipswitch_window.setVisible(false);
        };

        append(settings_menu);

        /* =================  window & viewport ================= */
        setStatusVisible(settings.video.status_visible);

        setBackgroundColor({0,0,0});
        layout.setMargin(0);
        viewport = new Viewport();
        layout.append(*viewport,{~0,~0});
        append(layout);

        /* ---------- SDL, input, video ---------- */
        settings.num_mice = ManyMouse_Init();

        if(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0)
        {
            printf("Unable to init SDL:\n%s\n", SDL_GetError());
            exit(1);
        }

        input = new Input();
        video = Video::createDefault(layout, viewport);

        onSize = [&]{
            if((signed)geometry().height < 0 || (signed)geometry().width < 0)
                return;

            video->video_init(geometry().width, geometry().height, settings.video);

            if(circuit == nullptr)
            {
                drawLogo();
                SDL_SetWindowTitle(g_window, VERSION_STRING);
            }
            else
            {
                SDL_SetWindowTitle(
                    g_window,
                    g_fullscreen ? VERSION_STRING
                                 : game_list[game_window.game_view.selection()].name);
            }
            viewport->setFocused();
        };

        setTitle("DICE");
        setFrameGeometry({
            (Desktop::workspace().width  - 640)/2,
            (Desktop::workspace().height - 480)/2,
            640,480});
        setMenuVisible();
        setVisible();
        onSize();
    }

    /*----------------------------------------------------------------
        Destructor
    ----------------------------------------------------------------*/
    ~MainWindow()
    {
        settings.save();
        if(circuit) delete circuit;
        delete video;
        delete viewport;
        delete input;
        SDL_Quit();
    }

    /*----------------------------------------------------------------
        Helper: toggleFullscreen
    ----------------------------------------------------------------*/
    void toggleFullscreen(bool fullscreen)
    {
        if(fullscreen)
            SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);

        setStatusVisible(!fullscreen);
        setMenuVisible(!fullscreen);
        setFullScreen(fullscreen);

        SDL_RaiseWindow(g_window);
        SDL_SetWindowInputFocus(g_window);
    }

    /*----------------------------------------------------------------
        Helper: drawLogo
    ----------------------------------------------------------------*/
    void drawLogo()
    {
        glViewport(0,0, geometry().width, geometry().height);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0,geometry().width, geometry().height,0,-1,1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glClearColor(0,0,0,0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        unsigned logo_w = logo_data[0x12];
        unsigned logo_h = logo_data[0x16];
        int logo_x = (geometry().width  - logo_w*4)/2;
        int logo_y = (geometry().height - logo_h*4)/2;

        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB, logo_w,logo_h,0,
                     GL_RGB,GL_UNSIGNED_BYTE, &logo_data[0x36]);

        glBegin(GL_QUADS);
            glColor3f(1,1,1);
            glTexCoord2f(0,1); glVertex3f(logo_x,               logo_y,               0);
            glTexCoord2f(1,1); glVertex3f(logo_x+logo_w*4,      logo_y,               0);
            glTexCoord2f(1,0); glVertex3f(logo_x+logo_w*4,      logo_y+logo_h*4,      0);
            glTexCoord2f(0,0); glVertex3f(logo_x,               logo_y+logo_h*4,      0);
        glEnd();

        video->swap_buffers();
    }

    /*----------------------------------------------------------------
        Main loop
    ----------------------------------------------------------------*/
    void run()
    {
        input->poll_input();

        if(circuit && !settings.pause)
        {
            circuit->run(2.5e-3 / Circuit::timescale);

            uint64_t emu = circuit->global_time * 1000000.0 * Circuit::timescale;

            if(settings.throttle)
                while(circuit->rtc.get_usecs() + 50000 < emu);

            if(circuit->rtc.get_usecs() > emu + 100000)
                circuit->rtc += (circuit->rtc.get_usecs() - emu - 100000);

            if(real_time.get_usecs() > 1000000)
            {
                setStatusText({"FPS: ", circuit->video.frame_count});
                circuit->video.frame_count = 0;
                real_time += 1000000;
            }
        }
        else
        {
            SDL_Delay(10);
            if(settings.pause && statusText() != "Paused")
                setStatusText("Paused");
            else if(!settings.pause && statusText() != VERSION_STRING)
                setStatusText(VERSION_STRING);
            if(circuit == nullptr && (focused() || video_window.focused()))
                drawLogo();
        }

        /* ---------- full input‑mapper & UI hotkey block
           (identical to original; no changes needed) ---------- */
        /* …  ~200 lines omitted for brevity … */
    }
};

/*====================================================================
    Global helpers
====================================================================*/
static nall::string app_path;
static MainWindow*  window_ptr;

const nall::string& application_path()  { return app_path; }
Window&             application_window(){ return *window_ptr; }

/*====================================================================
    main()
====================================================================*/
#undef main
int main(int argc, char** argv)
{
    std::sort(game_list, game_list + game_list_size);

    MainWindow main_window;
    window_ptr = &main_window;

    Application::setName("DICE");
    Application::Cocoa::onQuit = &Application::quit;
    Application::main = [&]{ main_window.run(); };

    app_path = dir(realpath(argv[0]));
    srand(time(nullptr));

    /* ---------- parse CLI flags ---------- */
    bool start_fullscreen = true;
    if(argc > 1)
    {
        for(int i=2;i<argc;++i)
        {
            if(strcmp(argv[i], "-window") == 0)
                start_fullscreen = false;
            else if(strcmp(argv[i], "--dump-state") == 0 && i+1<argc)
            {
                main_window.dump_path = argv[++i];
                main_window.smode     = SampleMode::Tick;
            }
            else if(strcmp(argv[i], "--dump-state-frame") == 0 && i+1<argc)
            {
                main_window.dump_path = argv[++i];
                main_window.smode     = SampleMode::FrameEdge;
            }
        }
    }

    /* ---------- quick‑launch if argv[1] matches game tag ---------- */
    if(argc > 1)
    {
        for(const GameDesc& g : game_list)
        {
            if(strcmp(argv[1], g.command_line) == 0)
            {
                if(start_fullscreen)
                    main_window.toggleFullscreen(true);

                main_window.circuit = new Circuit(
                    main_window.settings,
                    *main_window.input,
                    *main_window.video,
                    g.desc,
                    g.command_line,
                    main_window.dump_path,
                    main_window.smode);

                main_window.onSize();
                break;
            }
        }
    }

    Application::run();

#ifdef DEBUG
    printf("chip size:%d\n", int(sizeof(Chip)));
    if(main_window.circuit)
        printf("chips: %d\n", int(main_window.circuit->chips.size()));
#endif
    return 0;
}
