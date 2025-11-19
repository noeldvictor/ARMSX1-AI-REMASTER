#include "imgui_layer.h"

#include <string>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/backends/imgui_impl_sdl2.h"
#include "../third_party/imgui/backends/imgui_impl_sdlrenderer2.h"
#include "../psx/log.h"

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

#if defined(IMGUI_FRONTEND) && !defined(__DLL_BUILD) && !defined(IOS_TARGET) && !defined(__ANDROID__)
extern "C" {
#include "config.h"
PSXE_API int psxe_run_configured(psxe_config_t* cfg, void* external_window, void* external_renderer);
}

#include <array>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>
#include <climits>

static void sync_buffer(std::array<char, 512>& buf, const std::string& value) {
    buf.fill(0);
    size_t copy_len = std::min(value.size(), buf.size() - 1);
    memcpy(buf.data(), value.c_str(), copy_len);
    buf[copy_len] = '\0';
}

static void center_next_window(const ImVec2& size) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
}

static std::string read_readme() {
    std::ifstream file("README.md");
    if (!file.is_open())
        return std::string();

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static int map_upscale_index(int height) {
    switch (height) {
        case 720: return 1;
        case 1080: return 2;
        case 1440: return 3;
        case 2160: return 4;
        default: return 0;
    }
}

static int upscale_value_from_index(int idx) {
    switch (idx) {
        case 1: return 720;
        case 2: return 1080;
        case 3: return 1440;
        case 4: return 2160;
        default: return 480;
    }
}

static std::string display_aspect_to_string(int aspect) {
    switch (aspect) {
        case 1: return "square";
        case 2: return "wide16x9";
        default: return "classic";
    }
}

static int display_aspect_from_string(const std::string& value) {
    if (value == "square") return 1;
    if (value == "wide16x9") return 2;
    return 0;
}

static std::string upscale_label_from_height(int height) {
    switch (height) {
        case 720: return "720p";
        case 1080: return "1080p";
        case 1440: return "1440p";
        case 2160: return "2160p";
        default: return "480p";
    }
}

static bool write_settings_file(
    const std::string& path,
    const std::string& bios_search,
    const std::string& model,
    const std::string& bios_override,
    const std::string& region,
    bool texture_scale_mode,
    bool debug_panel,
    bool stretch_mode,
    int display_aspect,
    int upscale_height
) {
    std::ofstream out(path, std::ios::trunc);

    if (!out.is_open()) {
        log_error("Failed to write settings file at %s", path.c_str());
        return false;
    }

    out
        << "# Settings file generated by ARMSX frontend\n\n"
        << "psxe_version = \"" << STR(REP_VERSION) << "\"\n\n"
        << "[bios]\n"
        << "    search_path     = \"" << bios_search << "\"\n"
        << "    preferred_model = \"" << model << "\"\n"
        << "    override_file   = \"" << bios_override << "\"\n\n"
        << "[console]\n"
        << "    region          = \"" << region << "\"\n\n"
        << "[video]\n"
        << "    texture_scale_mode = " << (texture_scale_mode ? "true" : "false") << "\n"
        << "    debug_panel = " << (debug_panel ? "true" : "false") << "\n"
        << "    stretch_mode = " << (stretch_mode ? "true" : "false") << "\n"
        << "    display_aspect = \"" << display_aspect_to_string(display_aspect) << "\"\n"
        << "    wide_upscale = \"" << upscale_label_from_height(upscale_height) << "\"\n";

    return true;
}

struct FilePickerState {
    bool open = false;
    std::string cwd;
    std::string selection;
};

static std::string get_current_directory() {
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)))
        return std::string(buf);
    return std::string(".");
}

static bool is_directory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0)
        return S_ISDIR(st.st_mode);
    return false;
}

static bool list_directory(const std::string& cwd, std::vector<std::pair<std::string, bool>>& entries) {
    DIR* dir = opendir(cwd.c_str());
    if (!dir)
        return false;

    struct dirent* dent;
    while ((dent = readdir(dir)) != NULL) {
        std::string name = dent->d_name;
        if (name == ".")
            continue;
        // include .. for navigation
        std::string full = cwd + "/" + name;
        bool dir_flag = (dent->d_type == DT_DIR) || ((dent->d_type == DT_UNKNOWN) && is_directory(full));
        entries.emplace_back(full, dir_flag);
    }
    closedir(dir);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second)
            return a.second && !b.second;
        return a.first < b.first;
    });
    return true;
}

static bool file_picker_popup(const char* popup_id, FilePickerState& state, std::string& target_path) {
    if (state.open)
        ImGui::OpenPopup(popup_id);

    bool changed = false;

    if (ImGui::BeginPopupModal(popup_id, nullptr, ImGuiWindowFlags_NoResize)) {
        if (state.cwd.empty())
            state.cwd = get_current_directory();

        ImGui::TextWrapped("Current folder:\n%s", state.cwd.c_str());

        if (ImGui::Button("Up")) {
            size_t pos = state.cwd.find_last_of("/\\");
            if (pos != std::string::npos) {
                state.cwd = state.cwd.substr(0, pos ? pos : 1);
                state.selection.clear();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            // simple redraw
        }

        ImGui::BeginChild("browser", ImVec2(540, 360), true);

        std::vector<std::pair<std::string, bool>> entries;
        if (!list_directory(state.cwd, entries)) {
            log_error("File picker failed to list dir %s", state.cwd.c_str());
        } else {
            for (const auto& entry : entries) {
                const std::string& full = entry.first;
                std::string label = full.substr(state.cwd.size());
                if (!label.empty() && (label[0] == '/' || label[0] == '\\'))
                    label = label.substr(1);
                if (label.empty())
                    label = full;
                bool is_dir = entry.second;
                std::string display = is_dir ? "[DIR] " + label : label;

                if (ImGui::Selectable(display.c_str(), state.selection == full, ImGuiSelectableFlags_AllowDoubleClick)) {
                    state.selection = full;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        if (is_dir) {
                            state.cwd = full;
                            state.selection.clear();
                        } else {
                            target_path = state.selection;
                            changed = true;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            }
        }

        ImGui::EndChild();

        if (ImGui::Button("Select")) {
            if (!state.selection.empty()) {
                target_path = state.selection;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    state.open = false;
    return changed;
}

extern "C" PSXE_API int imgui_frontend_main(int argc, const char* argv[]) {
    psxe_config_t* cfg = psxe_cfg_create();

    if (!cfg) {
        log_fatal("Failed to allocate frontend config");
        return 1;
    }

    psxe_cfg_init(cfg);
    psxe_cfg_load_defaults(cfg);
    psxe_cfg_load(cfg, argc, argv);

    std::string bios_path = cfg->bios ? cfg->bios : "";
    std::string cdrom_path = cfg->cd_path ? cfg->cd_path : "";
    std::string exp_path = cfg->exp_path ? cfg->exp_path : "";
    std::string exe_path = cfg->exe ? cfg->exe : "";
    std::string region = cfg->region ? cfg->region : "ntsc";
    std::string model = cfg->model ? cfg->model : "scph1001";
    std::string settings_path = cfg->settings_path ? cfg->settings_path : "settings.toml";
    std::string bios_search = cfg->bios_search ? cfg->bios_search : "bios";
    bool quiet = cfg->quiet != 0;
    bool use_args = cfg->use_args != 0;
    bool texture_scale_mode = cfg->texture_scale_mode != 0;
    bool debug_panel = cfg->debug_panel != 0;
    bool stretch_mode = cfg->stretch_mode != 0;
    int display_aspect = cfg->display_aspect;
    int upscale_height = cfg->upscale_height ? cfg->upscale_height : 480;
    int log_level = std::clamp(cfg->log_level, 0, (int)LOG_FATAL);
    int scale = cfg->scale ? cfg->scale : 1;

    std::array<char, 512> bios_buf{};
    std::array<char, 512> cdrom_buf{};
    std::array<char, 512> exp_buf{};
    std::array<char, 512> exe_buf{};
    std::array<char, 512> region_buf{};
    std::array<char, 512> model_buf{};
    std::array<char, 512> settings_buf{};
    std::array<char, 512> bios_search_buf{};

    sync_buffer(bios_buf, bios_path);
    sync_buffer(cdrom_buf, cdrom_path);
    sync_buffer(exp_buf, exp_path);
    sync_buffer(exe_buf, exe_path);
    sync_buffer(region_buf, region);
    sync_buffer(model_buf, model);
    sync_buffer(settings_buf, settings_path);
    sync_buffer(bios_search_buf, bios_search);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        log_error("SDL_Init failed for frontend: %s", SDL_GetError());
        psxe_cfg_destroy(cfg);
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "ARMSX Frontend",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 640,
        SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        log_error("Failed to create SDL window for frontend: %s", SDL_GetError());
        psxe_cfg_destroy(cfg);
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!renderer) {
        log_error("Failed to create SDL renderer for frontend: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        psxe_cfg_destroy(cfg);
        SDL_Quit();
        return 1;
    }

    imgui_layer_init(window, renderer);

    bool running = true;
    bool settings_dirty = false;
    bool start_requested = false;
    bool show_settings = false;
    bool show_about = false;
    bool show_bios_popup = false;
    bool show_cd_popup = false;
    FilePickerState bios_picker{};
    FilePickerState cd_picker{};

    const std::string readme_text = read_readme();

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            imgui_layer_handle_event(&event);

            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        imgui_layer_new_frame();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);

        ImGuiWindowFlags root_flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_MenuBar;

        ImGui::Begin("FrontendRoot", nullptr, root_flags);
        if (ImGui::BeginMenuBar()) {
            if (ImGui::MenuItem("Start")) {
                if (!cdrom_path.empty() && bios_path.empty()) {
                    SDL_ShowSimpleMessageBox(
                        SDL_MESSAGEBOX_ERROR,
                        "Missing BIOS",
                        "A BIOS is required before booting a CDROM image.",
                        window
                    );
                } else {
                    start_requested = true;
                    running = false;
                }
            }

            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Load BIOS")) {
                    bios_picker.open = true;
                    bios_picker.selection.clear();
                    bios_picker.cwd.clear();
                    show_bios_popup = true;
                    show_cd_popup = false;
                    show_settings = false;
                    show_about = false;
                }
                if (ImGui::MenuItem("Load CDROM")) {
                    cd_picker.open = true;
                    cd_picker.selection.clear();
                    cd_picker.cwd.clear();
                    show_cd_popup = true;
                    show_bios_popup = false;
                    show_settings = false;
                    show_about = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) {
                    running = false;
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Settings")) {
                show_settings = true;
                show_about = false;
                show_bios_popup = false;
                show_cd_popup = false;
            }

            if (ImGui::MenuItem("About")) {
                show_about = true;
                show_settings = false;
                show_bios_popup = false;
                show_cd_popup = false;
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();

        if (show_bios_popup || bios_picker.open) {
            if (show_bios_popup)
                ImGui::OpenPopup("Load BIOS");

            if (ImGui::BeginPopupModal("Load BIOS", &show_bios_popup, ImGuiWindowFlags_NoResize)) {
                if (ImGui::InputText("Path", bios_buf.data(), bios_buf.size())) {
                    bios_path = bios_buf.data();
                }
                if (ImGui::Button("Browse...")) {
                    bios_picker.open = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Use BIOS")) {
                    bios_path = bios_buf.data();
                    settings_dirty = true;
                    show_bios_popup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Close")) {
                    show_bios_popup = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::Separator();
                ImGui::TextWrapped("Select a BIOS file to override the default search/model.");
                ImGui::EndPopup();
            }

            if (file_picker_popup("Browse BIOS File", bios_picker, bios_path)) {
                sync_buffer(bios_buf, bios_path);
                show_bios_popup = false;
                settings_dirty = true;
            }
        }

        if (show_cd_popup || cd_picker.open) {
            if (show_cd_popup)
                ImGui::OpenPopup("Load CDROM");

            if (ImGui::BeginPopupModal("Load CDROM", &show_cd_popup, ImGuiWindowFlags_NoResize)) {
                if (ImGui::InputText("Path", cdrom_buf.data(), cdrom_buf.size())) {
                    cdrom_path = cdrom_buf.data();
                }
                if (ImGui::Button("Browse...")) {
                    cd_picker.open = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Use CDROM")) {
                    cdrom_path = cdrom_buf.data();
                    show_cd_popup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Close")) {
                    show_cd_popup = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::Separator();
                ImGui::TextWrapped("Choose a CDROM image to boot.");
                ImGui::EndPopup();
            }

            if (file_picker_popup("Browse CDROM File", cd_picker, cdrom_path)) {
                sync_buffer(cdrom_buf, cdrom_path);
                show_cd_popup = false;
            }
        }

        if (show_settings) {
            center_next_window(ImVec2(392.0f, 448.0f));

            if (ImGui::Begin("Settings", &show_settings, ImGuiWindowFlags_NoCollapse)) {
                ImGui::TextUnformatted("Boot options");
                if (ImGui::InputText("BIOS", bios_buf.data(), bios_buf.size())) {
                    bios_path = bios_buf.data();
                    settings_dirty = true;
                }
                if (ImGui::InputText("CDROM", cdrom_buf.data(), cdrom_buf.size()))
                    cdrom_path = cdrom_buf.data();
                if (ImGui::InputText("Expansion ROM", exp_buf.data(), exp_buf.size()))
                    exp_path = exp_buf.data();
                if (ImGui::InputText("PS-X EXE", exe_buf.data(), exe_buf.size()))
                    exe_path = exe_buf.data();

                ImGui::Separator();
                ImGui::TextUnformatted("Console");
                const char* regions[] = { "auto", "ntsc", "pal" };
                int region_idx = 1;
                for (int i = 0; i < 3; i++) {
                    if (region == regions[i]) region_idx = i;
                }
                if (ImGui::Combo("Region", &region_idx, regions, IM_ARRAYSIZE(regions))) {
                    region = regions[region_idx];
                    settings_dirty = true;
                }

                static const char* models[] = {
                    "scph1000","scph1001","scph1002","scph3000","scph3500","scph5000",
                    "scph5500","scph5501","scph5502","scph5552",
                    "scph7000","scph7001","scph7002","scph7003",
                    "scph7501","scph7502","scph9002","scph100","scph101","scph102a","scph102b","scph102c"
                };
                int model_idx = 1;
                for (int i = 0; i < (int)(sizeof(models)/sizeof(models[0])); i++) {
                    if (model == models[i]) { model_idx = i; break; }
                }
                if (ImGui::Combo("Model", &model_idx, models, IM_ARRAYSIZE(models))) {
                    model = models[model_idx];
                    settings_dirty = true;
                }

                ImGui::Checkbox("Use CLI args only (--use-args)", &use_args);
                if (ImGui::InputText("Settings file", settings_buf.data(), settings_buf.size())) {
                    settings_path = settings_buf.data();
                    settings_dirty = true;
                }
                if (ImGui::InputText("BIOS folder", bios_search_buf.data(), bios_search_buf.size())) {
                    bios_search = bios_search_buf.data();
                    settings_dirty = true;
                }

                ImGui::Separator();
                ImGui::TextUnformatted("Video");
                if (ImGui::SliderInt("Scale", &scale, 1, 6))
                    settings_dirty = true;
                if (ImGui::Checkbox("Texture scale mode", &texture_scale_mode))
                    settings_dirty = true;
                if (ImGui::Checkbox("Debug panel", &debug_panel))
                    settings_dirty = true;
                if (ImGui::Checkbox("Stretch to window", &stretch_mode))
                    settings_dirty = true;

                const char* aspect_labels[] = { "classic", "square", "wide16x9" };
                if (display_aspect < 0 || display_aspect > 2)
                    display_aspect = 0;
                if (ImGui::Combo("Display aspect", &display_aspect, aspect_labels, IM_ARRAYSIZE(aspect_labels)))
                    settings_dirty = true;

                const char* upscales[] = { "480p", "720p", "1080p", "1440p", "2160p" };
                int upscale_idx = map_upscale_index(upscale_height);
                if (ImGui::Combo("Wide upscale", &upscale_idx, upscales, IM_ARRAYSIZE(upscales))) {
                    upscale_height = upscale_value_from_index(upscale_idx);
                    settings_dirty = true;
                }

                ImGui::Separator();
                ImGui::TextUnformatted("Logging");
                const char* log_levels[] = { "trace", "debug", "info", "warn", "error", "fatal" };
                log_level = std::clamp(log_level, 0, (int)LOG_FATAL);
                if (ImGui::Combo("Log level", &log_level, log_levels, IM_ARRAYSIZE(log_levels)))
                    settings_dirty = true;
                if (ImGui::Checkbox("Quiet logs", &quiet))
                    settings_dirty = true;

                if (!readme_text.empty()) {
                    ImGui::Separator();
                    ImGui::TextUnformatted("CLI reference (README.md)");
                    ImGui::BeginChild("readme_help", ImVec2(0, 160), true);
                    ImGui::TextUnformatted(readme_text.c_str());
                    ImGui::EndChild();
                }
            }

            ImGui::End();
        }

        if (show_about) {
            center_next_window(ImVec2(540.0f, 260.0f));
            ImGui::OpenPopup("About ARMSX");
            show_about = false;
        }

        ImGui::SetNextWindowSize(ImVec2(540.0f, 260.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("About ARMSX", nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("ARMSX - the PS1 emulator for everything.");
            ImGui::Spacing();
            ImGui::TextWrapped("Use the menu bar to load a BIOS, choose a CDROM, adjust settings, and press Start to boot.");
            ImGui::Spacing();
            if (ImGui::Button("Close")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Render();

        if (settings_dirty && !settings_path.empty()) {
            write_settings_file(
                settings_path,
                bios_search,
                model,
                bios_path,
                region,
                texture_scale_mode,
                debug_panel,
                stretch_mode,
                display_aspect,
                upscale_height
            );
            settings_dirty = false;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    imgui_layer_shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (!start_requested) {
        psxe_cfg_destroy(cfg);
        return 0;
    }

    cfg->bios = bios_path.empty() ? NULL : bios_path.c_str();
    cfg->cd_path = cdrom_path.empty() ? NULL : cdrom_path.c_str();
    cfg->exp_path = exp_path.empty() ? NULL : exp_path.c_str();
    cfg->exe = exe_path.empty() ? NULL : exe_path.c_str();
    cfg->region = region.empty() ? cfg->region : region.c_str();
    cfg->model = model.empty() ? cfg->model : model.c_str();
    cfg->bios_search = bios_search.empty() ? cfg->bios_search : bios_search.c_str();
    cfg->settings_path = settings_path.empty() ? cfg->settings_path : settings_path.c_str();
    cfg->quiet = quiet ? 1 : 0;
    cfg->use_args = use_args ? 1 : 0;
    cfg->texture_scale_mode = texture_scale_mode ? 1 : 0;
    cfg->debug_panel = debug_panel ? 1 : 0;
    cfg->stretch_mode = stretch_mode ? 1 : 0;
    cfg->display_aspect = display_aspect;
    cfg->upscale_height = upscale_height;
    cfg->log_level = log_level;
    cfg->scale = scale;

    return psxe_run_configured(cfg, NULL, NULL);
}
#endif
