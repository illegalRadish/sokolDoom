//------------------------------------------------------------------------------
//  doomgeneric_sokol.c
//
//  This is all the sokol-backend-specific code, including the entry
//  point sokol_main().
//------------------------------------------------------------------------------
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_time.h"
#include "sokol_debugtext.h"
#include "sokol_fetch.h"
#include "sokol_glue.h"
#include "m_argv.h"
#include "d_event.h"
#include "i_video.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include <assert.h>
#include "shaders.glsl.h"

// in m_menu.c
extern boolean menuactive;

void D_DoomMain(void);
void D_DoomLoop(void);
void D_DoomFrame(void);
void dg_Create();

#define KEY_QUEUE_SIZE (32)

typedef enum {
    APP_STATE_LOADING,
    APP_STATE_WAITING,
    APP_STATE_INIT,
    APP_STATE_RUNNING,
    APP_STATE_LOADING_FAILED,
} app_state_t;

typedef struct {
    uint8_t key_code;
    bool pressed;
} key_state_t;

static struct {
    app_state_t state;
    sg_buffer vbuf;
    sg_image img;
    sg_pipeline pip;
    key_state_t key_queue[KEY_QUEUE_SIZE];
    uint32_t key_write_index;
    uint32_t key_read_index;
    uint32_t mouse_button_state;
    uint32_t delayed_mouse_button_up;
} app;

#define MAX_WAD_SIZE (6 * 1024 * 1024)
static size_t wad_size;
static uint8_t wad_buffer[MAX_WAD_SIZE];

void fetch_callback(const sfetch_response_t* response) {
    if (response->fetched) {
        wad_size = response->fetched_size;
        app.state = APP_STATE_WAITING;
    }
    else if (response->failed) {
        app.state = APP_STATE_LOADING_FAILED;
    }
}

void init(void) {
    // initialize sokol-time, -gfx, -debugtext and -fetch
    stm_setup();
    sg_setup(&(sg_desc){
        .buffer_pool_size = 8,
        .image_pool_size = 8,
        .shader_pool_size = 8,
        .pipeline_pool_size = 8,
        .context_pool_size = 1,
        .context = sapp_sgcontext()
    });
    sdtx_setup(&(sdtx_desc_t){
        .context_pool_size = 1,
        .fonts[0] = sdtx_font_kc854(),
    });
    sfetch_setup(&(sfetch_desc_t){
        .max_requests = 1,
        .num_channels = 1,
        .num_lanes = 1,
    });

    // create sokol-gfx resources to render a fullscreen texture

    // a vertex buffer to render a fullscreen triangle
    const float verts[] = {
        0.0f, 0.0f,
        2.0f, 0.0f,
        0.0f, 2.0f
    };
    app.vbuf = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(verts),
    });

    // a dynamic texture for Doom's framebuffer
    app.img = sg_make_image(&(sg_image_desc){
        .width = DOOMGENERIC_RESX,
        .height = DOOMGENERIC_RESY,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .usage = SG_USAGE_STREAM,
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });

    // a pipeline object to render a textured fullscreen triangle
    app.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = sg_make_shader(display_shader_desc(sg_query_backend())),
        .layout = {
            .attrs[0].format = SG_VERTEXFORMAT_FLOAT2
        },
        .cull_mode = SG_CULLMODE_NONE,
        .depth = {
            .write_enabled = false,
            .compare = SG_COMPAREFUNC_ALWAYS
        }
    });

    // start loading the DOOM1.WAD file, the game start will be delayed
    // until this has finished (see the frame() callback below)
    sfetch_send(&(sfetch_request_t){
        .path = "DOOM1.WAD",
        .callback = fetch_callback,
        .buffer_ptr = wad_buffer,
        .buffer_size = sizeof(wad_buffer)
    });
    app.state = APP_STATE_LOADING;
}

static void begin_init_screen(void) {
    sdtx_canvas(sapp_widthf(), sapp_height() * 0.5f);
    sdtx_origin(2.0f, 1.0f);
}

static void end_init_screen(void) {
    sg_pass_action pass_action = {
        .colors[0] = { .action = SG_ACTION_CLEAR, .value = { 0.0f, 0.0f, 0.0f, 1.0f } }
    };
    if (app.state == APP_STATE_LOADING_FAILED) {
        // red background color if loading has failed
        pass_action.colors[0].value = (sg_color) { 1.0f, 0.0f, 0.0f, 1.0f };
    }
    sg_begin_default_pass(&pass_action, sapp_width(), sapp_height());
    sdtx_draw();
    sg_end_pass();
    sg_commit();
}

static void draw_greeting_message(void) {
    sdtx_color3f(0.75f, 0.75f, 0.75f);
    sdtx_puts("*** DOOM (shareware, no sound) ***\n\n");
    sdtx_puts("Ported to the sokol headers.\n\n");
    sdtx_puts("Controls:\n");
    sdtx_puts("=========\n\n");
    sdtx_puts("Arrow keys:     move and turn\n\n");
    sdtx_puts("Alt+arrow keys: move and strafe\n\n");
    sdtx_puts("AWSD:           move and strafe\n\n");
    sdtx_puts("Shift:          run\n\n");
    sdtx_puts("1 - 7:          select weapon\n\n");
    sdtx_puts("Ctrl:           fire weapon\n\n");
    sdtx_puts("Space:          use door\n\n");
    sdtx_puts("\n");
}

// draw a simple loading message during async WAD file loading
static void draw_loading_screen(void) {
    begin_init_screen();
    draw_greeting_message();
    sdtx_puts("Loading DOOM1.WAD");
    for (int i = 0; i < ((sapp_frame_count() / 20) & 3); i++) {
        sdtx_putc('.');
    }
    end_init_screen();
}

// draw the greeting screen with a 'press key to start' message
static void draw_waiting_screen(void) {
    begin_init_screen();
    draw_greeting_message();
    if ((sapp_frame_count() / 20) & 1) {
        sdtx_puts("Press any key to start game!");
    }
    end_init_screen();
}

// draw an error screen if WAD file loading failed
static void draw_loading_failed_screen(void) {
    begin_init_screen();
    if ((sapp_frame_count() / 20) & 1) {
        sdtx_puts("LOADING FAILED!");
    }
    end_init_screen();
}

// helper function to adjust aspect ratio
static void apply_viewport(float canvas_width, float canvas_height) {
    const float canvas_aspect = canvas_width / canvas_height;
    const float doom_width = (float)DOOMGENERIC_RESX;
    const float doom_height = (float)DOOMGENERIC_RESY;
    const float doom_aspect = doom_width / doom_height;
    float vp_x, vp_y, vp_w, vp_h;
    if (doom_aspect < canvas_aspect) {
        vp_y = 0.0f;
        vp_h = canvas_height;
        vp_w = canvas_height * doom_aspect;
        vp_x = (canvas_width - vp_w) / 2;
    }
    else {
        vp_x = 0.0f;
        vp_w = canvas_width;
        vp_h = canvas_width / doom_aspect;
        vp_y = (canvas_height - vp_h) / 2;
    }
    sg_apply_viewport(vp_x, vp_y, vp_w, vp_h, true);
}

// copy the Doom framebuffer into sokol-gfx texture and render to display
static void draw_game_frame(void) {
    sg_update_image(app.img, &(sg_image_data){
        .subimage[0][0] = {
            .ptr = DG_ScreenBuffer,
            .size = DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(uint32_t)
        }
    });
    const sg_pass_action pass_action = { .colors[0] = { .action = SG_ACTION_DONTCARE } };
    sg_begin_default_pass(&pass_action, sapp_width(), sapp_height());
    sg_apply_pipeline(app.pip);
    sg_apply_bindings(&(sg_bindings){
        .vertex_buffers[0] = app.vbuf,
        .fs_images[0] = app.img,
    });
    apply_viewport(sapp_widthf(), sapp_heightf());
    sg_draw(0, 3, 1);
    sg_end_pass();
    sg_commit();
}

void frame(void) {
    sfetch_dowork();
    switch (app.state) {
        case APP_STATE_LOADING:
            draw_loading_screen();
            break;
        case APP_STATE_WAITING:
            draw_waiting_screen();
            break;
        case APP_STATE_INIT:
            dg_Create();
            // D_DoomMain() without the trailing call to D_DoomLoop()
            D_DoomMain();
            app.state = APP_STATE_RUNNING;
            // fallthough!
        case APP_STATE_RUNNING:
            // handle delayed mouse-button-up (see SAPP_EVENTTYPE_MOUSE_UP handling)
            // FIXME: run D_DoomFrame at actual 30 fps, regardless of display refresh rate
            if (sapp_frame_count() & 1) {
                D_DoomFrame();
                // this prevents that very short mouse button taps on touchpads
                // are not deteced
                if (app.delayed_mouse_button_up != 0) {
                    app.mouse_button_state &= ~app.delayed_mouse_button_up;
                    app.delayed_mouse_button_up = 0;
                    D_PostEvent(&(event_t){
                        .type = ev_mouse,
                        .data1 = app.mouse_button_state
                    });
                }
            }
            draw_game_frame();
            break;

        case APP_STATE_LOADING_FAILED:
            draw_loading_failed_screen();
            break;
    }
}

void cleanup(void) {
    sfetch_shutdown();
    sdtx_shutdown();
    sg_shutdown();
}

static void push_key(uint8_t key_code, bool pressed) {
    if (key_code != 0) {
        assert(app.key_write_index < KEY_QUEUE_SIZE);
        app.key_queue[app.key_write_index] = (key_state_t) {
            .key_code = key_code,
            .pressed = pressed
        };
        app.key_write_index = (app.key_write_index + 1) % KEY_QUEUE_SIZE;
    }
}

static key_state_t pull_key(void) {
    if (app.key_read_index == app.key_write_index) {
        return (key_state_t){0};
    }
    else {
        assert(app.key_read_index < KEY_QUEUE_SIZE);
        key_state_t res = app.key_queue[app.key_read_index];
        app.key_read_index = (app.key_read_index + 1) % KEY_QUEUE_SIZE;
        return res;
    }
}

// originally in i_video.c
static int AccelerateMouse(int val) {
    if (val < 0) {
        return -AccelerateMouse(-val);
    }
    if (val > mouse_threshold) {
        return (int)((val - mouse_threshold) * mouse_acceleration + mouse_threshold);
    }
    else
    {
        return val;
    }
}

void input(const sapp_event* ev) {
    if (app.state == APP_STATE_WAITING) {
        if ((ev->type == SAPP_EVENTTYPE_KEY_DOWN) ||
            (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN) ||
            (ev->type == SAPP_EVENTTYPE_TOUCHES_BEGAN))
        {
            app.state = APP_STATE_INIT;
        }
    }
    else if (app.state == APP_STATE_RUNNING) {
        if (menuactive && sapp_mouse_locked()) {
            sapp_lock_mouse(false);
        }
        if (!menuactive && !sapp_mouse_locked()) {
            sapp_lock_mouse(true);
        }
        if (ev->type == SAPP_EVENTTYPE_UNFOCUSED) {
            // clear all input when window loses focus
            push_key(KEY_UPARROW, false);
            push_key(KEY_DOWNARROW, false);
            push_key(KEY_LEFTARROW, false);
            push_key(KEY_RIGHTARROW, false);
            push_key(KEY_STRAFE_L, false);
            push_key(KEY_STRAFE_R, false);
            push_key(KEY_FIRE, false);
            push_key(KEY_USE, false);
            push_key(KEY_TAB, false);
            push_key(KEY_RSHIFT, false);
            push_key(KEY_ESCAPE, false);
            push_key(KEY_ENTER, false);
            push_key('1', false);
            push_key('2', false);
            push_key('3', false);
            push_key('4', false);
            push_key('5', false);
            push_key('6', false);
            push_key('7', false);
        }
        else if ((ev->type == SAPP_EVENTTYPE_KEY_DOWN) || (ev->type == SAPP_EVENTTYPE_KEY_UP)) {
            bool pressed = (ev->type == SAPP_EVENTTYPE_KEY_DOWN);
            bool consume_event = true;
            switch (ev->key_code) {
                case SAPP_KEYCODE_W:
                case SAPP_KEYCODE_UP:
                    push_key(KEY_UPARROW, pressed);
                    break;
                case SAPP_KEYCODE_S:
                case SAPP_KEYCODE_DOWN:
                    push_key(KEY_DOWNARROW, pressed);
                    break;
                case SAPP_KEYCODE_LEFT:
                    if (pressed) {
                        if (ev->modifiers & SAPP_MODIFIER_ALT) {
                            push_key(KEY_STRAFE_L, true);
                        }
                        else {
                            push_key(KEY_LEFTARROW, true);
                        }
                    }
                    else {
                        push_key(KEY_STRAFE_L, false);
                        push_key(KEY_LEFTARROW, false);
                    }
                    break;
                case SAPP_KEYCODE_RIGHT:
                    if (pressed) {
                        if (ev->modifiers & SAPP_MODIFIER_ALT) {
                            push_key(KEY_STRAFE_R, true);
                        }
                        else {
                            push_key(KEY_RIGHTARROW, true);
                        }
                    }
                    else {
                        push_key(KEY_STRAFE_R, false);
                        push_key(KEY_RIGHTARROW, false);
                    }
                    break;
                case SAPP_KEYCODE_A:
                    push_key(KEY_STRAFE_L, pressed);
                    break;
                case SAPP_KEYCODE_D:
                    push_key(KEY_STRAFE_R, pressed);
                    break;
                case SAPP_KEYCODE_SPACE:
                    push_key(KEY_USE, pressed);
                    break;
                case SAPP_KEYCODE_LEFT_CONTROL:
                    push_key(KEY_FIRE, pressed);
                    break;
                case SAPP_KEYCODE_ESCAPE:
                    push_key(KEY_ESCAPE, pressed);
                    break;
                case SAPP_KEYCODE_ENTER:
                    push_key(KEY_ENTER, pressed);
                    break;
                case SAPP_KEYCODE_TAB:
                    push_key(KEY_TAB, pressed);
                    break;
                case SAPP_KEYCODE_LEFT_SHIFT:
                case SAPP_KEYCODE_RIGHT_SHIFT:
                    push_key(KEY_RSHIFT, pressed);
                    break;
                case SAPP_KEYCODE_1:
                    push_key('1', pressed);
                    break;
                case SAPP_KEYCODE_2:
                    push_key('2', pressed);
                    break;
                case SAPP_KEYCODE_3:
                    push_key('3', pressed);
                    break;
                case SAPP_KEYCODE_4:
                    push_key('4', pressed);
                    break;
                case SAPP_KEYCODE_5:
                    push_key('5', pressed);
                    break;
                case SAPP_KEYCODE_6:
                    push_key('6', pressed);
                    break;
                case SAPP_KEYCODE_7:
                    push_key('7', pressed);
                    break;
                default:
                    consume_event = false;
                    break;
            }
            if (consume_event) {
                sapp_consume_event();
            }
        }
        else if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN) {
            app.mouse_button_state |= (1<<ev->mouse_button);
            D_PostEvent(&(event_t){
                .type = ev_mouse,
                .data1 = app.mouse_button_state,
            });
        }
        else if (ev->type == SAPP_EVENTTYPE_MOUSE_UP) {
            // delay mouse up to the next frame so that short
            // taps on touch pads are registered
            app.delayed_mouse_button_up |= (1<<ev->mouse_button);
        }
        else if (ev->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
            D_PostEvent(&(event_t){
                .type = ev_mouse,
                .data1 = app.mouse_button_state,
                .data2 = AccelerateMouse(ev->mouse_dx),
            });
        }
    }
}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    static char* args[] = { "doom", "-iwad", "DOOM1.WAD" };
    myargc = 3;
    myargv = args;
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .event_cb = input,
        .width = DOOMGENERIC_RESX * 2,
        .height = DOOMGENERIC_RESY * 2,
        .window_title = "Sokol Doom Shareware",
        .icon.sokol_default = true,
    };
}

//== DoomGeneric backend callbacks =============================================

// Note that some of those are empty, because they only make sense
// in an "own the game loop" scenario, not in a frame-callback scenario.

void DG_Init(void) {
    // empty, see sokol-app init() callback instead
}

void DG_DrawFrame(void) {
    // empty, see sokol-app frame() callback instead
}

void DG_SetWindowTitle(const char* title) {
    // window title changes ignored
    (void)title;
}

int DG_GetKey(int* pressed, unsigned char* doomKey) {
    key_state_t key_state = pull_key();
    if (key_state.key_code != 0) {
        *doomKey = key_state.key_code;
        *pressed = key_state.pressed ? 1 : 0;
        return 1;
    }
    else {
        // no key available
        return 0;
    }
}

// the sleep function is used in blocking wait loops, those don't
// work in a browser environment anyway, inject an assert instead
// so we easily find all those wait loops
void DG_SleepMs(uint32_t ms) {
    assert(false && "DG_SleepMS called!\n");
}

// NO IDEA why tf this works, but it's a non-intrusive way to fix the timing
// (I guess Doom advances at least one game tick per frame)
uint32_t DG_GetTicksMs(void) {
    return 0;
}

//== FILE SYSTEM OVERRIDE ======================================================
#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"
#include "memio.h"

typedef struct
{
    wad_file_t wad;
    MEMFILE *fstream;
} memio_wad_file_t;

// at end of file!
extern wad_file_class_t memio_wad_file;

static wad_file_t* W_MemIO_OpenFile(char* path) {
    if (0 != strcmp(path, "DOOM1.WAD")) {
        return 0;
    }
    MEMFILE* fstream = mem_fopen_read(wad_buffer, wad_size);
    if (fstream == 0) {
        return 0;
    }

    memio_wad_file_t* result = Z_Malloc(sizeof(memio_wad_file_t), PU_STATIC, 0);
    result->wad.file_class = &memio_wad_file;
    result->wad.mapped = NULL;
    result->wad.length = wad_size;
    result->fstream = fstream;

    return &result->wad;
}

static void W_MemIO_CloseFile(wad_file_t* wad) {
    memio_wad_file_t* memio_wad = (memio_wad_file_t*) wad;
    mem_fclose(memio_wad->fstream);
    Z_Free(memio_wad);
}

static size_t W_MemIO_Read(wad_file_t* wad, uint32_t offset, void* buffer, size_t buffer_len) {
    memio_wad_file_t* memio_wad = (memio_wad_file_t*) wad;
    mem_fseek(memio_wad->fstream, offset, MEM_SEEK_SET);
    return mem_fread(buffer, 1, buffer_len, memio_wad->fstream);
}

wad_file_class_t memio_wad_file = {
    .OpenFile = W_MemIO_OpenFile,
    .CloseFile = W_MemIO_CloseFile,
    .Read = W_MemIO_Read,
};
