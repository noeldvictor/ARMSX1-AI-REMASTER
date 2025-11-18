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

#ifdef __cplusplus
}
#endif
