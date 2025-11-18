#include "imgui_layer.h"

#include <string>

#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/backends/imgui_impl_sdl2.h"
#include "../third_party/imgui/backends/imgui_impl_sdlrenderer2.h"

static bool g_imgui_initialized = false;
static SDL_Window* g_window = nullptr;
static SDL_Renderer* g_renderer = nullptr;

extern "C" PSXE_API void imgui_layer_init(SDL_Window* window, SDL_Renderer* renderer) {
    if (!window || !renderer)
        return;

    if (g_imgui_initialized) {
        if (window == g_window && renderer == g_renderer)
            return;

        imgui_layer_shutdown();
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // avoid disk writes

    ImGui::StyleColorsDark();

    g_window = window;
    g_renderer = renderer;

    ImGui_ImplSDL2_InitForSDLRenderer(g_window, g_renderer);
    ImGui_ImplSDLRenderer2_Init(g_renderer);

    g_imgui_initialized = true;
}

extern "C" PSXE_API void imgui_layer_shutdown(void) {
    if (!g_imgui_initialized)
        return;

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    g_window = nullptr;
    g_renderer = nullptr;
    g_imgui_initialized = false;
}

extern "C" PSXE_API void imgui_layer_handle_event(const SDL_Event* event) {
    if (!g_imgui_initialized || !event)
        return;

    ImGui_ImplSDL2_ProcessEvent(event);
}

extern "C" PSXE_API void imgui_layer_new_frame(void) {
    if (!g_imgui_initialized)
        return;

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

extern "C" PSXE_API void imgui_layer_render_overlay(const char* os_name, float fps) {
    if (!g_imgui_initialized)
        return;

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(240.0f, 60.0f), ImGuiCond_Always);

    ImGui::Begin("Overlay", nullptr, flags);
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("OS : %s", os_name ? os_name : "unknown");
    ImGui::End();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_renderer);
}
