#pragma once

#include "common.h"
#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

PSXE_API void imgui_layer_init(SDL_Window* window, SDL_Renderer* renderer);
PSXE_API void imgui_layer_shutdown(void);
PSXE_API void imgui_layer_handle_event(const SDL_Event* event);
PSXE_API void imgui_layer_new_frame(void);
PSXE_API void imgui_layer_render_overlay(const char* os_name, float fps);
PSXE_API void imgui_layer_flush(void);

#if defined(IMGUI_FRONTEND) && !defined(__DLL_BUILD) && !defined(IOS_TARGET) && !defined(__ANDROID__)
typedef struct {
    int toggle_pause;
    int reset;
    int quit;
    int swap_cd;
    char new_cd_path[512];
} imgui_ingame_actions_t;

PSXE_API void imgui_ingame_menu_render(int paused, const char* current_cd, imgui_ingame_actions_t* actions);
PSXE_API int imgui_frontend_main(int argc, const char* argv[]);
#endif

#ifdef __cplusplus
}
#endif
