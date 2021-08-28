//------------------------------------------------------------------------------
//  doomgeneric_sokol.c
//------------------------------------------------------------------------------
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_time.h"
#include "sokol_debugtext.h"
#include "sokol_fetch.h"
#include "sokol_glue.h"
#include "m_argv.h"
#include "doomgeneric.h"
#include <assert.h>
#include "shaders.glsl.h"

void D_DoomMain(void);
void D_DoomLoop(void);
void D_DoomFrame(void);
void M_FindResponseFile(void);
void dg_Create();

typedef enum {
    APP_STATE_LOADING,
    APP_STATE_INIT,
    APP_STATE_RUNNING,
    APP_STATE_LOADING_FAILED,
} app_state_t;

static struct {
    app_state_t state;
    uint64_t start_time;
    uint32_t ticks_ms;
    sg_buffer vbuf;
    sg_image img;
    sg_pipeline pip;
    sg_bindings bind;
} app;

#define MAX_WAD_SIZE (6 * 1024 * 1024)
static size_t wad_size;
static uint8_t wad_buffer[MAX_WAD_SIZE];

void fetch_callback(const sfetch_response_t* response) {
    if (response->fetched) {
        wad_size = response->fetched_size;
        app.state = APP_STATE_INIT;
    }
    else if (response->failed) {
        app.state = APP_STATE_LOADING_FAILED;
    }
}

void init(void) {
    app.state = APP_STATE_LOADING;
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
        .fonts[0] = sdtx_font_kc853(),
    });
    sfetch_setup(&(sfetch_desc_t){
        .max_requests = 1,
        .num_channels = 1,
        .num_lanes = 1,
    });
    sfetch_send(&(sfetch_request_t){
        .path = "DOOM1.WAD",
        .callback = fetch_callback,
        .buffer_ptr = wad_buffer,
        .buffer_size = sizeof(wad_buffer)
    });
}

void DG_Init(void) {
    // fullscreen triangle vertices
    const float verts[] = {
        0.0f, 0.0f,
        2.0f, 0.0f,
        0.0f, 2.0f
    };
    app.vbuf = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(verts),
    });

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
    app.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = sg_make_shader(display_shader_desc(sg_query_backend())),
        .layout = {
            .attrs[0].format = SG_VERTEXFORMAT_FLOAT2
        },
    });
    app.bind = (sg_bindings){
        .vertex_buffers[0] = app.vbuf,
        .fs_images[0] = app.img
    };
}

void DG_SetWindowTitle(const char* title) {
//    sapp_set_window_title(title);
}

int DG_GetKey(int* pressed, unsigned char* doomKey) {
    // FIXME
    return 0;
}

void DG_SleepMs(uint32_t ms) {
    assert(false && "DG_SleepMS called!\n");
}

uint32_t DG_GetTicksMs(void) {
    return (uint32_t) stm_ms(stm_since(app.start_time));
}

void DG_DrawFrame(void) {
    sg_update_image(app.img, &(sg_image_data){
        .subimage[0][0] = {
            .ptr = DG_ScreenBuffer,
            .size = DOOMGENERIC_RESX*DOOMGENERIC_RESY*sizeof(uint32_t)
        }
    });
    sg_begin_default_pass(&(sg_pass_action){0}, sapp_width(), sapp_height());
    sg_apply_pipeline(app.pip);
    sg_apply_bindings(&app.bind);
    sg_draw(0, 3, 1);
    sg_end_pass();
    sg_commit();
}

static void draw_loading_screen(void) {
    const float w = sapp_widthf();
    const float h = sapp_heightf();

    sdtx_canvas(w * 0.25f, h * 0.25f);
    sdtx_pos(1.0f, 1.0f);
    sdtx_puts("LOADING");
    for (int i = 0; i < ((sapp_frame_count() / 20) & 3); i++) {
        sdtx_putc('.');
    }

    const sg_pass_action pass_action = {
        .colors[0] = { .action = SG_ACTION_CLEAR, .value = { 0.0f, 0.0f, 0.0f, 1.0f } }
    };
    sg_begin_default_passf(&pass_action, w, h);
    sdtx_draw();
    sg_end_pass();
    sg_commit();
}

static void draw_loading_failed_screen(void) {
    const float w = sapp_widthf();
    const float h = sapp_heightf();

    sdtx_canvas(w * 0.25f, h * 0.25f);
    sdtx_pos(1.0f, 1.0f);
    if ((sapp_frame_count() / 20) & 1) {
        sdtx_puts("LOADING FAILED!");
    }

    const sg_pass_action pass_action = {
        .colors[0] = { .action = SG_ACTION_CLEAR, .value = { 1.0f, 0.0f, 0.0f, 1.0f } }
    };
    sg_begin_default_passf(&pass_action, w, h);
    sdtx_draw();
    sg_end_pass();
    sg_commit();
}

void frame(void) {
    sfetch_dowork();
    switch (app.state) {
        case APP_STATE_LOADING:
            draw_loading_screen();
            //app.state = APP_STATE_INIT;
            break;

        case APP_STATE_INIT:
            M_FindResponseFile();
            dg_Create();
            // D_DoomMain() without the trailing call to D_DoomLoop()
            D_DoomMain();
            app.start_time = stm_now();
            app.state = APP_STATE_RUNNING;
            // fallthough
        case APP_STATE_RUNNING:
            D_DoomFrame();
            break;

        case APP_STATE_LOADING_FAILED:
            draw_loading_failed_screen();
            break;
    }
}

void cleanup(void) {
    sdtx_shutdown();
    sg_shutdown();
}

void input(const sapp_event* ev) {
    (void)ev;
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
        .width = DOOMGENERIC_RESX,
        .height = DOOMGENERIC_RESY,
        .window_title = "Sokol Doom Shareware",
        .icon.sokol_default = true,
    };
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

    // Create a new stdc_wad_file_t to hold the file handle.
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
