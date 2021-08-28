//------------------------------------------------------------------------------
//  doomgeneric_sokol.c
//------------------------------------------------------------------------------
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_time.h"
#include "m_argv.h"
#include "doomgeneric.h"
#include <assert.h>
#include "shaders.glsl.h"

void D_DoomMain(void);
void D_DoomLoop(void);
void D_DoomFrame(void);
void M_FindResponseFile(void);
void dg_Create();

static struct {
    uint64_t start_time;
    uint32_t ticks_ms;
    sg_buffer vbuf;
    sg_image img;
    sg_pipeline pip;
    sg_pass_action pass_action;
    sg_bindings bind;
} state;

void DG_Init(void) {
    sg_setup(&(sg_desc){
        .buffer_pool_size = 8,
        .image_pool_size = 8,
        .shader_pool_size = 8,
        .pipeline_pool_size = 8,
        .context_pool_size = 1,
        .context = sapp_sgcontext()
    });

    // fullscreen triangle vertices
    const float verts[] = {
        0.0f, 0.0f,
        2.0f, 0.0f,
        0.0f, 2.0f
    };
    state.vbuf = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(verts),
    });

    state.img = sg_make_image(&(sg_image_desc){
        .width = DOOMGENERIC_RESX,
        .height = DOOMGENERIC_RESY,
        .pixel_format = SG_PIXELFORMAT_BGRA8,
        .usage = SG_USAGE_STREAM,
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });
    state.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = sg_make_shader(display_shader_desc(sg_query_backend())),
        .layout = {
            .attrs[0].format = SG_VERTEXFORMAT_FLOAT2
        },
    });
    state.pass_action = (sg_pass_action){ .colors[0].action = SG_ACTION_DONTCARE };
    state.bind = (sg_bindings){
        .vertex_buffers[0] = state.vbuf,
        .fs_images[0] = state.img
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
    return (uint32_t) stm_ms(stm_since(state.start_time));
}

void DG_DrawFrame(void) {
    sg_update_image(state.img, &(sg_image_data){
        .subimage[0][0] = {
            .ptr = DG_ScreenBuffer,
            .size = DOOMGENERIC_RESX*DOOMGENERIC_RESY*sizeof(uint32_t)
        }
    });
    sg_begin_default_pass(&(sg_pass_action){0}, sapp_width(), sapp_height());
    sg_apply_pipeline(state.pip);
    sg_apply_bindings(&state.bind);
    sg_draw(0, 3, 1);
    sg_end_pass();
    sg_commit();
}

void init(void) {
    stm_setup();

    M_FindResponseFile();
    dg_Create();

    // D_DoomMain() without the trailing call to D_DoomLoop()
    D_DoomMain();

    state.start_time = stm_now();
}

void frame(void) {
    D_DoomFrame();
}

void cleanup(void) {
    sg_shutdown();
}

void input(const sapp_event* ev) {
    (void)ev;
}

sapp_desc sokol_main(int argc, char* argv[]) {
    myargc = argc;
    myargv = argv;
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
