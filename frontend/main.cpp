#include <SDL.h>
#include <SDL_gamecontroller.h>
#include <SDL_render.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/html5.h>
#endif

extern "C" {
#include "../psx/psx.h"
#include "../psx/dev/cdrom/cdrom.h"
#include "../psx/dev/gpu.h"
#include "../psx/dev/input.h"
#include "../psx/dev/pad.h"
#include "../psx/dev/timer.h"
#include "../psx/input/sda.h"
#include "common.h"
#include "config.h"
#include "toml.h"
}

#include "IconsFontAwesome5.h"
#include "fsui/backend_sdl.hpp"
#include "fsui/fsui.hpp"
#include "fsui/imgui_fullscreen.hpp"
#include "imgui.h"

#undef main

namespace {

constexpr Uint32 kPauseChordGraceMs = 120;
constexpr std::string_view kCustomFsuiAppIconPath = "icons/AppIconLarge.png";

constexpr bool SupportsManagedWindowSizing() {
#if defined(__ANDROID__) || defined(IOS_TARGET) || defined(__EMSCRIPTEN__) || defined(UWP_TARGET)
    return false;
#else
    return true;
#endif
}

class ArmsxApp;

ArmsxApp* g_active_app = nullptr;
std::optional<std::filesystem::path> g_pending_wasm_path;

enum class LaunchKind {
    None,
    Bios,
    Disc,
    Exe,
};

struct CliFlags {
    bool bios = false;
    bool bios_folder = false;
    bool model = false;
    bool region = false;
    bool scale = false;
    bool settings_file = false;
    bool quiet = false;
    bool log_level = false;
    bool exp_rom = false;
    bool exe = false;
    bool cdrom = false;
    bool has_boot_request = false;
};

struct LaunchRequest {
    LaunchKind kind = LaunchKind::None;
    std::filesystem::path path;
    std::string label;
};

struct FrontendSettings {
    std::string settings_path;
    std::string bios_override;
    std::string bios_search = "bios";
    std::string model = "scph1001";
    std::string region = "auto";
    std::string exp_path;
    std::string default_exe_path;
    int scale = 3;
    int log_level = LOG_FATAL;
    bool quiet = false;
    bool texture_scale_mode = false;
    bool debug_panel = false;
    bool stretch_mode = false;
    int display_aspect = 0;
    int upscale_height = 480;
    fsui::UiState ui_state{
        .theme = "Dark",
        .prompt_icon_pack = fsui::PromptIconPack::Auto,
        .background_image_path = {},
        .default_game_view = 0,
        .game_sort = 2,
        .game_sort_reverse = false,
        .game_list_paths = {},
        .game_list_recursive_paths = {},
        .covers_path = {},
        .show_inputs_overlay = false,
        .show_settings_overlay = false,
        .show_performance_overlay = false,
    };
};

struct PendingChordButton {
    bool physical_down = false;
    bool pending = false;
    bool forwarded = false;
    Uint32 pending_since = 0;
};

std::string ToLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::string Trim(std::string_view value) {
    size_t begin = 0;
    size_t end = value.size();

    while ((begin < end) && std::isspace(static_cast<unsigned char>(value[begin]))) {
        begin++;
    }

    while ((end > begin) && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string NormalizeModel(std::string_view value) {
    std::string out;

    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
    }

    return out;
}

std::string DefaultSettingsPath() {
    const char* pref = psxe_cfg_get_pref_path();

    if (pref && pref[0]) {
        return std::string(pref) + "settings.toml";
    }

    return "settings.toml";
}

std::filesystem::path DefaultBrowseDirectory() {
    const char* pref = psxe_cfg_get_pref_path();

    if (pref && pref[0]) {
        return std::filesystem::path(pref);
    }

    return std::filesystem::current_path();
}

bool ReadSdlFileBytes(std::string_view path, std::vector<unsigned char>& bytes) {
    SDL_RWops* handle = SDL_RWFromFile(std::string(path).c_str(), "rb");
    if (!handle) {
        return false;
    }

    bytes.clear();
    std::array<unsigned char, 16 * 1024> buffer{};
    for (;;) {
        const size_t read = SDL_RWread(handle, buffer.data(), 1, buffer.size());
        if (read == 0) {
            break;
        }
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(read));
    }

    SDL_RWclose(handle);
    return !bytes.empty();
}

std::string MaterializeBundledFsuiAppIcon() {
    const std::filesystem::path destination = DefaultBrowseDirectory() / kCustomFsuiAppIconPath;
    std::error_code ec;

    if (std::filesystem::exists(destination, ec) && std::filesystem::is_regular_file(destination, ec)) {
        return destination.string();
    }

    std::vector<unsigned char> bytes;
    if (!ReadSdlFileBytes(kCustomFsuiAppIconPath, bytes)) {
        return {};
    }

    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        return {};
    }

    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output.good()) {
        return {};
    }

    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output.good()) {
        return {};
    }

    return destination.string();
}

std::string ResolveFsuiAppIconPath() {
    const std::filesystem::path relative(kCustomFsuiAppIconPath);
    std::error_code ec;

    auto try_path = [&](const std::filesystem::path& candidate) -> std::string {
        if (candidate.empty()) {
            return {};
        }
        ec.clear();
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
            return candidate.lexically_normal().string();
        }
        return {};
    };

    std::unique_ptr<char, decltype(&SDL_free)> base_path(SDL_GetBasePath(), &SDL_free);
    if (base_path) {
        const std::filesystem::path base(base_path.get());

        if (std::string resolved = try_path(base / relative); !resolved.empty()) {
            return resolved;
        }
        if (std::string resolved = try_path((base / ".." / "Resources" / relative).lexically_normal()); !resolved.empty()) {
            return resolved;
        }
    }

    if (std::string resolved = try_path(relative); !resolved.empty()) {
        return resolved;
    }
    if (std::string resolved = try_path(std::filesystem::current_path(ec) / relative); !resolved.empty()) {
        return resolved;
    }
    if (std::string resolved = MaterializeBundledFsuiAppIcon(); !resolved.empty()) {
        return resolved;
    }

    return std::string(kCustomFsuiAppIconPath);
}

bool IsDiscPath(const std::filesystem::path& path) {
    const std::string ext = ToLower(path.extension().string());
    return ext == ".cue" || ext == ".bin" || ext == ".iso" || ext == ".img";
}

bool IsExePath(const std::filesystem::path& path) {
    const std::string ext = ToLower(path.extension().string());
    return ext == ".exe" || ext == ".ps-exe" || ext == ".psexe";
}

std::string StemToTitle(const std::filesystem::path& path) {
    std::string value = path.stem().string();

    if (value.empty()) {
        value = path.filename().string();
    }

    std::replace(value.begin(), value.end(), '_', ' ');

    return Trim(value);
}

std::string EscapeTomlString(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);

    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }

    return out;
}

const char* AspectToString(int aspect) {
    switch (aspect) {
        case 1: return "square";
        case 2: return "wide16x9";
        default: return "classic";
    }
}

const char* AspectTitle(int aspect) {
    switch (aspect) {
        case 1: return "Square";
        case 2: return "Wide 16:9";
        default: return "Classic";
    }
}

const char* UpscaleToString(int height) {
    switch (height) {
        case 720: return "720p";
        case 1080: return "1080p";
        case 1440: return "1440p";
        case 2160: return "2160p";
        default: return "480p";
    }
}

std::string BoolTitle(bool value) {
    return value ? "On" : "Off";
}

std::string RendererValueTitle(const FrontendSettings& settings) {
    std::string value = settings.texture_scale_mode ? "Linear" : "Nearest";

    if (settings.stretch_mode) {
        value += ", Stretch";
    } else {
        value += ", Fit";
    }

    return value;
}

int RegionFromSettings(const FrontendSettings& settings) {
    const std::string region = ToLower(settings.region);

    if (region == "pal") {
        return CDR_REGION_EUROPE;
    }

    if (region == "ntsc") {
        const std::string model = NormalizeModel(settings.model);
        static const std::array<const char*, 8> japan_models = {
            "scph1000",
            "scph3000",
            "scph3500",
            "scph5000",
            "scph5500",
            "scph7000",
            "scph7003",
            "scph100",
        };

        if (std::find(japan_models.begin(), japan_models.end(), model) != japan_models.end()) {
            return CDR_REGION_JAPAN;
        }

        return CDR_REGION_AMERICA;
    }

    const std::string model = NormalizeModel(settings.model);
    static const std::array<const char*, 8> europe_models = {
        "scph1002",
        "scph5502",
        "scph5552",
        "scph7002",
        "scph7502",
        "scph9002",
        "scph102a",
        "scph102b",
    };
    static const std::array<const char*, 8> japan_models = {
        "scph1000",
        "scph3000",
        "scph3500",
        "scph5000",
        "scph5500",
        "scph7000",
        "scph7003",
        "scph100",
    };

    if (std::find(europe_models.begin(), europe_models.end(), model) != europe_models.end()) {
        return CDR_REGION_EUROPE;
    }

    if (std::find(japan_models.begin(), japan_models.end(), model) != japan_models.end()) {
        return CDR_REGION_JAPAN;
    }

    return CDR_REGION_AMERICA;
}

LaunchRequest LaunchForPath(const std::filesystem::path& path) {
    LaunchRequest request;
    request.path = path;
    request.label = StemToTitle(path);
    request.kind = IsExePath(path) ? LaunchKind::Exe : LaunchKind::Disc;
    return request;
}

std::vector<fsui::SettingsChoiceOption> BuildChoices(const std::vector<std::string>& titles, int selected_index) {
    std::vector<fsui::SettingsChoiceOption> options;
    options.reserve(titles.size());

    for (size_t index = 0; index < titles.size(); index++) {
        fsui::SettingsChoiceOption option;
        option.title = titles[index];
        option.selected = (static_cast<int>(index) == selected_index);
        options.push_back(std::move(option));
    }

    return options;
}

std::vector<fsui::SettingsChoiceOption> BuildAspectChoices(int current) {
    return BuildChoices({"Classic", "Square", "Wide 16:9"}, current);
}

std::vector<fsui::SettingsChoiceOption> BuildUpscaleChoices(int current_height) {
    const std::vector<int> heights = {480, 720, 1080, 1440, 2160};
    const std::vector<std::string> labels = {"480p", "720p", "1080p", "1440p", "2160p"};
    int selected = 0;

    for (size_t index = 0; index < heights.size(); index++) {
        if (heights[index] == current_height) {
            selected = static_cast<int>(index);
            break;
        }
    }

    return BuildChoices(labels, selected);
}

std::vector<fsui::SettingsChoiceOption> BuildScaleChoices(int current_scale) {
    const std::vector<int> scales = {1, 2, 3, 4, 5, 6};
    std::vector<std::string> labels;
    labels.reserve(scales.size());

    int selected = 0;
    for (size_t index = 0; index < scales.size(); index++) {
        labels.push_back(std::to_string(scales[index]) + "x");
        if (scales[index] == current_scale) {
            selected = static_cast<int>(index);
        }
    }

    return BuildChoices(labels, selected);
}

std::vector<fsui::SettingsChoiceOption> BuildRegionChoices(const std::string& current_region) {
    const std::vector<std::string> values = {"auto", "ntsc", "pal"};
    const std::vector<std::string> labels = {"Auto", "NTSC", "PAL"};
    const std::string current = ToLower(current_region);
    int selected = 0;

    for (size_t index = 0; index < values.size(); index++) {
        if (values[index] == current) {
            selected = static_cast<int>(index);
            break;
        }
    }

    return BuildChoices(labels, selected);
}

std::vector<std::string> ModelChoices() {
    return {
        "scph1000", "scph1001", "scph1002", "scph3000", "scph3500", "scph5000", "scph5500",
        "scph5501", "scph5502", "scph5552", "scph7000", "scph7001", "scph7002", "scph7003",
        "scph7501", "scph7502", "scph9002", "scph100", "scph101", "scph102a", "scph102b", "scph102c",
    };
}

std::vector<fsui::SettingsChoiceOption> BuildModelChoices(const std::string& current_model) {
    const std::vector<std::string> models = ModelChoices();
    const std::string current = NormalizeModel(current_model);
    int selected = 0;

    for (size_t index = 0; index < models.size(); index++) {
        if (NormalizeModel(models[index]) == current) {
            selected = static_cast<int>(index);
            break;
        }
    }

    return BuildChoices(models, selected);
}

std::vector<fsui::SettingsChoiceOption> BuildLogLevelChoices(int current_level) {
    const std::vector<std::string> labels = {"Trace", "Debug", "Info", "Warn", "Error", "Fatal"};
    const int clamped = std::clamp(current_level, static_cast<int>(LOG_TRACE), static_cast<int>(LOG_FATAL));
    return BuildChoices(labels, clamped);
}

std::time_t FileTimeToTimeT(const std::filesystem::file_time_type& value) {
    using namespace std::chrono;
    const auto adjusted = time_point_cast<system_clock::duration>(
        value - std::filesystem::file_time_type::clock::now() + system_clock::now()
    );
    return system_clock::to_time_t(adjusted);
}

void CopyTomlString(const toml_datum_t& datum, std::string& out) {
    if (!datum.ok || !datum.u.s) {
        return;
    }

    out = datum.u.s;
    free(datum.u.s);
}

void CopyTomlArrayStrings(const toml_array_t* array, std::vector<std::filesystem::path>& out) {
    out.clear();

    if (!array) {
        return;
    }

    const int count = toml_array_nelem(array);
    out.reserve(static_cast<size_t>(count));

    for (int index = 0; index < count; index++) {
        toml_datum_t value = toml_string_at(array, index);

        if (!value.ok || !value.u.s) {
            continue;
        }

        out.emplace_back(value.u.s);
        free(value.u.s);
    }
}

CliFlags ScanCliFlags(int argc, const char* argv[]) {
    CliFlags flags;

    for (int index = 1; index < argc; index++) {
        const std::string_view arg(argv[index] ? argv[index] : "");

        auto mark_value_opt = [&](const char* short_name, const char* long_name, bool* target) {
            const std::string_view short_opt(short_name);
            const std::string_view long_opt(long_name);

            if (arg == short_opt || arg == long_opt) {
                *target = true;
                index++;
                return true;
            }

            if (arg.starts_with(long_opt) && arg.size() > (long_opt.size() + 1) && arg[long_opt.size()] == '=') {
                *target = true;
                return true;
            }

            return false;
        };

        if (mark_value_opt("-b", "--bios", &flags.bios) ||
            mark_value_opt("-B", "--bios-folder", &flags.bios_folder) ||
            mark_value_opt("-M", "--model", &flags.model) ||
            mark_value_opt("-r", "--region", &flags.region) ||
            mark_value_opt("-s", "--scale", &flags.scale) ||
            mark_value_opt("-S", "--settings-file", &flags.settings_file) ||
            mark_value_opt("-L", "--log-level", &flags.log_level) ||
            mark_value_opt("-e", "--exp-rom", &flags.exp_rom) ||
            mark_value_opt("-x", "--exe", &flags.exe) ||
            mark_value_opt("", "--cdrom", &flags.cdrom)) {
            continue;
        }

        if (arg == "-q" || arg == "--quiet") {
            flags.quiet = true;
            continue;
        }

        if (!arg.empty() && arg[0] != '-') {
            flags.cdrom = true;
        }
    }

    flags.has_boot_request = flags.cdrom || flags.exe;

    return flags;
}

void LoadExtraSettings(FrontendSettings& settings, const CliFlags& cli) {
    if (settings.settings_path.empty()) {
        return;
    }

    FILE* file = fopen(settings.settings_path.c_str(), "rb");

    if (!file) {
        return;
    }

    char error[256] = {};
    toml_table_t* root = toml_parse_file(file, error, sizeof(error));
    fclose(file);

    if (!root) {
        return;
    }

    if (toml_table_t* runtime = toml_table_in(root, "runtime")) {
        if (!cli.scale) {
            toml_datum_t value = toml_int_in(runtime, "display_scale");
            if (value.ok) {
                settings.scale = static_cast<int>(value.u.i);
            }
        }

        if (!cli.log_level) {
            toml_datum_t value = toml_int_in(runtime, "log_level");
            if (value.ok) {
                settings.log_level = static_cast<int>(value.u.i);
            }
        }

        if (!cli.quiet) {
            toml_datum_t value = toml_bool_in(runtime, "quiet");
            if (value.ok) {
                settings.quiet = value.u.b != 0;
            }
        }
    }

    if (toml_table_t* paths = toml_table_in(root, "paths")) {
        if (!cli.exp_rom) {
            CopyTomlString(toml_string_in(paths, "expansion_rom"), settings.exp_path);
        }

        if (!cli.exe) {
            CopyTomlString(toml_string_in(paths, "default_psx_exe"), settings.default_exe_path);
        }
    }

    if (toml_table_t* library = toml_table_in(root, "library")) {
        CopyTomlArrayStrings(toml_array_in(library, "folders"), settings.ui_state.game_list_paths);
        CopyTomlArrayStrings(toml_array_in(library, "recursive_folders"), settings.ui_state.game_list_recursive_paths);
    }

    if (toml_table_t* fsui_state = toml_table_in(root, "fsui")) {
        toml_datum_t default_view = toml_int_in(fsui_state, "default_game_view");
        toml_datum_t sort = toml_int_in(fsui_state, "game_sort");
        toml_datum_t reverse = toml_bool_in(fsui_state, "game_sort_reverse");

        if (default_view.ok) {
            settings.ui_state.default_game_view = static_cast<int>(default_view.u.i);
        }
        if (sort.ok) {
            settings.ui_state.game_sort = static_cast<int>(sort.u.i);
        }
        if (reverse.ok) {
            settings.ui_state.game_sort_reverse = reverse.u.b != 0;
        }
    }

    toml_free(root);
}

FrontendSettings BuildSettings(const psxe_config_t* cfg, const CliFlags& cli) {
    FrontendSettings settings;

    settings.settings_path = (cfg && cfg->settings_path && cfg->settings_path[0]) ? cfg->settings_path : DefaultSettingsPath();
    settings.bios_override = (cfg && cfg->bios && cfg->bios[0]) ? cfg->bios : "";
    settings.bios_search = (cfg && cfg->bios_search && cfg->bios_search[0]) ? cfg->bios_search : "bios";
    settings.model = (cfg && cfg->model && cfg->model[0]) ? cfg->model : "scph1001";
    settings.region = (cfg && cfg->region && cfg->region[0]) ? cfg->region : "auto";
    settings.exp_path = (cfg && cfg->exp_path && cfg->exp_path[0]) ? cfg->exp_path : "";
    settings.default_exe_path = (cfg && cfg->exe && cfg->exe[0]) ? cfg->exe : "";
    settings.scale = cfg ? cfg->scale : 3;
    settings.log_level = cfg ? cfg->log_level : LOG_FATAL;
    settings.quiet = cfg ? (cfg->quiet != 0) : false;
    settings.texture_scale_mode = cfg ? (cfg->texture_scale_mode != 0) : false;
    settings.debug_panel = cfg ? (cfg->debug_panel != 0) : false;
    settings.stretch_mode = cfg ? (cfg->stretch_mode != 0) : false;
    settings.display_aspect = cfg ? cfg->display_aspect : 0;
    settings.upscale_height = cfg ? cfg->upscale_height : 480;
    settings.ui_state.show_settings_overlay = settings.debug_panel;
    settings.ui_state.show_performance_overlay = settings.debug_panel;

    if (!cli.bios && !settings.bios_override.empty()) {
        const std::filesystem::path bios_path(settings.bios_override);
        if (!std::filesystem::exists(bios_path) && (settings.bios_override == "bios.bin")) {
            settings.bios_override.clear();
        }
    }

    LoadExtraSettings(settings, cli);

    settings.scale = std::max(1, settings.scale);
    settings.log_level = std::clamp(settings.log_level, static_cast<int>(LOG_TRACE), static_cast<int>(LOG_FATAL));
    settings.ui_state.show_settings_overlay = settings.debug_panel;
    settings.ui_state.show_performance_overlay = settings.debug_panel;

    if (settings.ui_state.covers_path.empty()) {
        settings.ui_state.covers_path = DefaultBrowseDirectory() / "covers";
    }

    return settings;
}

bool SaveSettings(const FrontendSettings& settings) {
    const std::filesystem::path target = settings.settings_path.empty() ? std::filesystem::path(DefaultSettingsPath()) : std::filesystem::path(settings.settings_path);

    try {
        if (target.has_parent_path()) {
            std::filesystem::create_directories(target.parent_path());
        }
    } catch (...) {
        return false;
    }

    std::ofstream out(target);

    if (!out.is_open()) {
        return false;
    }

    auto write_path_array = [&](const std::vector<std::filesystem::path>& paths) {
        out << "[";
        for (size_t index = 0; index < paths.size(); index++) {
            if (index) {
                out << ", ";
            }
            out << "\"" << EscapeTomlString(paths[index].string()) << "\"";
        }
        out << "]";
    };

    out
        << "# Settings file generated by ARMSX\n\n"
        << "psxe_version = \"" << STR(REP_VERSION) << "\"\n\n"
        << "[bios]\n"
        << "    search_path     = \"" << EscapeTomlString(settings.bios_search) << "\"\n"
        << "    preferred_model = \"" << EscapeTomlString(settings.model) << "\"\n"
        << "    override_file   = \"" << EscapeTomlString(settings.bios_override) << "\"\n\n"
        << "[console]\n"
        << "    region          = \"" << EscapeTomlString(settings.region) << "\"\n\n"
        << "[runtime]\n"
        << "    display_scale = " << settings.scale << "\n"
        << "    log_level = " << settings.log_level << "\n"
        << "    quiet = " << (settings.quiet ? "true" : "false") << "\n\n"
        << "[paths]\n"
        << "    expansion_rom = \"" << EscapeTomlString(settings.exp_path) << "\"\n"
        << "    default_psx_exe = \"" << EscapeTomlString(settings.default_exe_path) << "\"\n\n"
        << "[video]\n"
        << "    texture_scale_mode = " << (settings.texture_scale_mode ? "true" : "false") << "\n"
        << "    debug_panel = " << (settings.debug_panel ? "true" : "false") << "\n"
        << "    stretch_mode = " << (settings.stretch_mode ? "true" : "false") << "\n"
        << "    display_aspect = \"" << AspectToString(settings.display_aspect) << "\"\n"
        << "    wide_upscale = \"" << UpscaleToString(settings.upscale_height) << "\"\n\n"
        << "[library]\n"
        << "    folders = ";
    write_path_array(settings.ui_state.game_list_paths);
    out << "\n    recursive_folders = ";
    write_path_array(settings.ui_state.game_list_recursive_paths);
    out
        << "\n\n[fsui]\n"
        << "    default_game_view = " << settings.ui_state.default_game_view << "\n"
        << "    game_sort = " << settings.ui_state.game_sort << "\n"
        << "    game_sort_reverse = " << (settings.ui_state.game_sort_reverse ? "true" : "false") << "\n";

    return out.good();
}

void AudioUpdate(void* userdata, uint8_t* buffer, int size) {
    psx_t* psx = static_cast<psx_t*>(userdata);
    psx_cdrom_t* cdrom = psx->cdrom;
    psx_spu_t* spu = psx->spu;

    std::memset(buffer, 0, static_cast<size_t>(size));

    psx_cdrom_get_audio_samples(cdrom, buffer, size);
    psx_spu_update_cdda_buffer(spu, cdrom->cdda_buf);

    for (int sample = 0; sample < (size >> 2); sample++) {
        const uint32_t value = psx_spu_get_sample(spu);
        const int16_t left = static_cast<int16_t>(value & 0xffff);
        const int16_t right = static_cast<int16_t>(value >> 16);

        *reinterpret_cast<int16_t*>(&buffer[(sample << 2) + 0]) += left;
        *reinterpret_cast<int16_t*>(&buffer[(sample << 2) + 2]) += right;
    }
}

class ArmsxSession {
  public:
    ~ArmsxSession() {
        destroy();
    }

    bool create(SDL_Renderer* renderer, const FrontendSettings& settings, const LaunchRequest& request, std::string& error) {
        destroy();

        if (!renderer) {
            error = "SDL renderer is not initialized.";
            return false;
        }

        renderer_ = renderer;

        psxe_config_t cfg{};
        cfg.bios = settings.bios_override.empty() ? nullptr : settings.bios_override.c_str();
        cfg.bios_search = settings.bios_search.c_str();
        cfg.model = settings.model.c_str();

        char* bios_path = psxe_cfg_get_bios_path(&cfg);
        std::unique_ptr<char, decltype(&free)> bios_guard(bios_path, &free);

        if (!bios_path || !bios_path[0]) {
            error = "No BIOS could be resolved. Set a BIOS override file or a BIOS folder that contains the selected model.";
            return false;
        }

        psx_ = psx_create();
        if (!psx_) {
            error = "Failed to allocate a PSX session.";
            return false;
        }

        const char* expansion = settings.exp_path.empty() ? nullptr : settings.exp_path.c_str();
        const int init_result = psx_init(psx_, bios_path, expansion);

        if (init_result != 0) {
            error = "Failed to initialize the emulator core. Check the BIOS and expansion ROM paths.";
            destroy();
            return false;
        }

        psx_gpu_t* gpu = psx_get_gpu(psx_);
        psx_gpu_set_event_callback(gpu, GPU_EVENT_DMODE, nullptr);
        psx_gpu_set_event_callback(gpu, GPU_EVENT_VBLANK, psxe_gpu_vblank_timer_event_cb);
        psx_gpu_set_event_callback(gpu, GPU_EVENT_HBLANK, psxe_gpu_hblank_event_cb);
        psx_gpu_set_event_callback(gpu, GPU_EVENT_VBLANK_END, psxe_gpu_vblank_end_event_cb);
        psx_gpu_set_event_callback(gpu, GPU_EVENT_HBLANK_END, psxe_gpu_hblank_end_event_cb);
        psx_gpu_set_udata(gpu, 1, psx_->timer);

        psx_cdrom_set_region(psx_get_cdrom(psx_), RegionFromSettings(settings));

        input_ = psx_input_create();
        if (input_) {
            psx_input_init(input_);
        }
        pad_device_ = psxi_sda_create();

        if (!input_ || !pad_device_) {
            error = "Failed to create the controller input bridge.";
            destroy();
            return false;
        }

        psxi_sda_init(pad_device_, SDA_MODEL_DIGITAL);
        psxi_sda_init_input(pad_device_, input_);
        psx_pad_attach_joy(psx_->pad, 0, input_);
        pad_device_ = nullptr;

        const std::string slot1 = std::string(psxe_cfg_get_pref_path() ? psxe_cfg_get_pref_path() : "") + "slot1.mcd";
        const std::string slot2 = std::string(psxe_cfg_get_pref_path() ? psxe_cfg_get_pref_path() : "") + "slot2.mcd";
        psx_pad_attach_mcd(psx_->pad, 0, slot1.c_str());
        psx_pad_attach_mcd(psx_->pad, 1, slot2.c_str());

        switch (request.kind) {
            case LaunchKind::Disc:
                if (psx_cdrom_open(psx_get_cdrom(psx_), request.path.string().c_str()) != 0) {
                    error = "Failed to open the selected disc image.";
                    destroy();
                    return false;
                }
                disc_path_ = request.path;
                exe_path_.clear();
                title_ = request.label.empty() ? StemToTitle(request.path) : request.label;
                break;

            case LaunchKind::Exe:
                while (psx_->cpu->pc != 0x80030000) {
                    psx_update(psx_);
                }
                psx_load_exe(psx_, request.path.string().c_str());
                exe_path_ = request.path;
                disc_path_.clear();
                title_ = request.label.empty() ? StemToTitle(request.path) : request.label;
                break;

            case LaunchKind::Bios:
                disc_path_.clear();
                exe_path_.clear();
                title_ = "PlayStation BIOS";
                break;

            case LaunchKind::None:
            default:
                error = "Invalid launch request.";
                destroy();
                return false;
        }

        launch_kind_ = request.kind;

        SDL_AudioSpec desired{};
        SDL_AudioSpec obtained{};
        desired.freq = 44100;
        desired.format = AUDIO_S16SYS;
        desired.channels = 2;
        desired.samples = CD_SECTOR_SIZE >> 2;
        desired.callback = AudioUpdate;
        desired.userdata = psx_;

        audio_dev_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
        if (audio_dev_) {
            SDL_PauseAudioDevice(audio_dev_, 0);
        }

        paused_ = false;
        debug_view_ = false;
        texture_width_ = 0;
        texture_height_ = 0;
        texture_format_ = SDL_PIXELFORMAT_UNKNOWN;
        updateTexture(settings);

        return true;
    }

    void destroy() {
        if (audio_dev_) {
            SDL_PauseAudioDevice(audio_dev_, 1);
            SDL_CloseAudioDevice(audio_dev_);
            audio_dev_ = 0;
        }

        if (texture_) {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }

        const bool input_attached = psx_ && psx_->pad && (psx_->pad->joy_slot[0] == input_);

        if (psx_) {
            psx_destroy(psx_);
            psx_ = nullptr;
        }

        if (input_ && !input_attached) {
            psx_input_destroy(input_);
        }

        if (pad_device_) {
            psxi_sda_destroy(pad_device_);
            pad_device_ = nullptr;
        }

        input_ = nullptr;
        renderer_ = nullptr;
        disc_path_.clear();
        exe_path_.clear();
        title_.clear();
        launch_kind_ = LaunchKind::None;
        paused_ = false;
        debug_view_ = false;
    }

    bool valid() const {
        return psx_ != nullptr;
    }

    psx_t* psx() const {
        return psx_;
    }

    psx_pad_t* pad() const {
        return psx_ ? psx_->pad : nullptr;
    }

    void setPaused(bool paused) {
        paused_ = paused;
        if (audio_dev_) {
            SDL_PauseAudioDevice(audio_dev_, paused ? 1 : 0);
        }
    }

    bool paused() const {
        return paused_;
    }

    void runFrame() {
        if (!psx_ || paused_) {
            return;
        }

        psx_run_frame(psx_);
    }

    void setDebugView(bool enabled) {
        debug_view_ = enabled;
    }

    bool debugView() const {
        return debug_view_;
    }

    bool reset() {
        if (!psx_) {
            return false;
        }

        psx_soft_reset(psx_);
        return true;
    }

    bool swapDisc(const std::filesystem::path& path) {
        if (!psx_) {
            return false;
        }

        if (psx_swap_disc(psx_, path.string().c_str()) != 0) {
            return false;
        }

        disc_path_ = path;
        launch_kind_ = LaunchKind::Disc;
        title_ = StemToTitle(path);
        psx_soft_reset(psx_);
        return true;
    }

    fsui::CurrentGameInfo currentGameInfo() const {
        fsui::CurrentGameInfo info;

        if (!psx_) {
            return info;
        }

        info.has_game = true;
        info.title = title_.empty() ? "ARMSX" : title_;
        info.subtitle = launch_kind_ == LaunchKind::Bios ? "No disc inserted" : (disc_path_.empty() ? exe_path_.filename().string() : disc_path_.filename().string());
        info.title_id = NormalizeModel(info.title);
        info.path = launch_kind_ == LaunchKind::Bios ? std::filesystem::path("bios://boot") : (disc_path_.empty() ? exe_path_ : disc_path_);
        return info;
    }

    std::vector<fsui::OverlayTextLine> settingsOverlayLines(const FrontendSettings& settings) const {
        std::vector<fsui::OverlayTextLine> lines;
        lines.push_back(fsui::OverlayTextLine{.text = std::string("Aspect: ") + AspectTitle(settings.display_aspect)});
        lines.push_back(fsui::OverlayTextLine{.text = std::string("Video: ") + RendererValueTitle(settings)});
        lines.push_back(fsui::OverlayTextLine{.text = std::string("Debug View: ") + BoolTitle(debug_view_)});
        return lines;
    }

    std::vector<fsui::OverlayTextLine> performanceOverlayLines(const FrontendSettings& settings) const {
        (void)settings;
        std::vector<fsui::OverlayTextLine> lines;
        char fps[64] = {};
        char video[64] = {};
        std::snprintf(fps, sizeof(fps), "FPS: %.1f", ImGui::GetIO().Framerate);
        std::snprintf(video, sizeof(video), "Video: %dx%d", texture_width_, texture_height_);
        lines.push_back(fsui::OverlayTextLine{.text = fps});
        lines.push_back(fsui::OverlayTextLine{.text = video});
        lines.push_back(fsui::OverlayTextLine{.text = std::string("State: ") + (paused_ ? "Paused" : "Running")});
        return lines;
    }

    void updateTexture(const FrontendSettings& settings) {
        if (!psx_ || !renderer_) {
            return;
        }

        const int next_width = debug_view_ ? PSX_GPU_FB_WIDTH : static_cast<int>(psx_get_display_width(psx_));
        const int next_height = debug_view_ ? PSX_GPU_FB_HEIGHT : static_cast<int>(psx_get_display_height(psx_));
        const Uint32 next_format = debug_view_ || !psx_get_display_format(psx_) ? SDL_PIXELFORMAT_BGR555 : SDL_PIXELFORMAT_RGB24;

        if ((next_width != texture_width_) || (next_height != texture_height_) || (next_format != texture_format_) || !texture_) {
            if (texture_) {
                SDL_DestroyTexture(texture_);
                texture_ = nullptr;
            }

            texture_ = SDL_CreateTexture(renderer_, next_format, SDL_TEXTUREACCESS_STREAMING, next_width, next_height);
            texture_width_ = next_width;
            texture_height_ = next_height;
            texture_format_ = next_format;
        }

        if (!texture_) {
            return;
        }

        if (settings.texture_scale_mode) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
            SDL_SetTextureScaleMode(texture_, SDL_ScaleModeLinear);
#endif
        } else {
#if SDL_VERSION_ATLEAST(2, 0, 12)
            SDL_SetTextureScaleMode(texture_, SDL_ScaleModeNearest);
#endif
        }

        void* display_buffer = debug_view_ ? psx_get_vram(psx_) : psx_get_display_buffer(psx_);
        if (!debug_view_ && ((psx_->gpu->disp_y + texture_height_) > PSX_GPU_FB_HEIGHT)) {
            display_buffer = psx_get_vram(psx_);
        }

        SDL_UpdateTexture(texture_, nullptr, display_buffer, PSX_GPU_FB_STRIDE);
    }

    void draw(const FrontendSettings& settings) {
        if (!texture_) {
            return;
        }

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float display_width = std::max(display.x, 1.0f);
        const float display_height = std::max(display.y, 1.0f);

        float aspect = 4.0f / 3.0f;
        if (debug_view_) {
            aspect = static_cast<float>(texture_width_) / static_cast<float>(std::max(texture_height_, 1));
        } else if (settings.display_aspect == 2) {
            aspect = 16.0f / 9.0f;
        } else if (settings.display_aspect == 1) {
            aspect = 1.0f;
        } else {
            aspect = static_cast<float>(psx_get_display_aspect(psx_));
        }

        float target_width = display_width;
        float target_height = display_height;
        float offset_x = 0.0f;
        float offset_y = 0.0f;

        if (!settings.stretch_mode) {
            target_width = display_width;
            target_height = target_width / aspect;

            if (target_height > display_height) {
                target_height = display_height;
                target_width = target_height * aspect;
            }

            offset_x = (display_width - target_width) * 0.5f;
            offset_y = (display_height - target_height) * 0.5f;
        }

        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
        draw_list->AddRectFilled(ImVec2(0.0f, 0.0f), display, IM_COL32(0, 0, 0, 255));
        draw_list->AddImage(
            reinterpret_cast<ImTextureID>(texture_),
            ImVec2(offset_x, offset_y),
            ImVec2(offset_x + target_width, offset_y + target_height)
        );
    }

    bool saveScreenshot(const std::filesystem::path& path) {
        if (!psx_) {
            return false;
        }

        const int width = texture_width_;
        const int height = texture_height_;
        const Uint32 format = texture_format_;
        void* source = debug_view_ ? psx_get_vram(psx_) : psx_get_display_buffer(psx_);

        if (!debug_view_ && ((psx_->gpu->disp_y + texture_height_) > PSX_GPU_FB_HEIGHT)) {
            source = psx_get_vram(psx_);
        }

        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, SDL_BITSPERPIXEL(format), format);

        if (!surface) {
            return false;
        }

        const size_t row_size = static_cast<size_t>(surface->pitch);
        const uint8_t* src = static_cast<const uint8_t*>(source);
        uint8_t* dst = static_cast<uint8_t*>(surface->pixels);

        for (int row = 0; row < height; row++) {
            std::memcpy(dst + (static_cast<size_t>(row) * static_cast<size_t>(surface->pitch)), src + (static_cast<size_t>(row) * static_cast<size_t>(PSX_GPU_FB_STRIDE)), row_size);
        }

        const int result = SDL_SaveBMP(surface, path.string().c_str());
        SDL_FreeSurface(surface);
        return result == 0;
    }

  private:
    SDL_Renderer* renderer_ = nullptr;
    psx_t* psx_ = nullptr;
    psx_input_t* input_ = nullptr;
    psxi_sda_t* pad_device_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    SDL_AudioDeviceID audio_dev_ = 0;
    std::filesystem::path disc_path_;
    std::filesystem::path exe_path_;
    std::string title_;
    LaunchKind launch_kind_ = LaunchKind::None;
    bool paused_ = false;
    bool debug_view_ = false;
    int texture_width_ = 0;
    int texture_height_ = 0;
    Uint32 texture_format_ = SDL_PIXELFORMAT_UNKNOWN;
};

class GameplayInputRouter {
  public:
    ~GameplayInputRouter() {
        if (controller_) {
            SDL_GameControllerClose(controller_);
            controller_ = nullptr;
        }
    }

    void attach(psx_pad_t* pad) {
        clearAll();
        pad_ = pad;
    }

    void detach() {
        clearAll();
        pad_ = nullptr;
    }

    void onFsuiOpened() {
        fsui_owns_input_ = true;
        clearAll();
    }

    void onFsuiClosed() {
        fsui_owns_input_ = false;
        clearAll();
    }

    bool takePauseRequest() {
        const bool requested = pause_requested_;
        pause_requested_ = false;
        return requested;
    }

    void tick(bool fsui_active) {
        if (!fsui_active) {
            flushChordButtons(SDL_GetTicks());
        }
    }

    std::vector<fsui::InputOverlayDeviceState> buildInputOverlay() const {
        fsui::InputOverlayDeviceState device;
        device.title = "P1";
        device.bindings = {
            fsui::InputOverlayBinding{.label = "Up", .glyph = "^", .kind = fsui::InputOverlayBindingKind::Button, .value = (active_digital_mask_ & PSXI_SW_SDA_PAD_UP) ? 1.0f : 0.0f},
            fsui::InputOverlayBinding{.label = "Down", .glyph = "v", .kind = fsui::InputOverlayBindingKind::Button, .value = (active_digital_mask_ & PSXI_SW_SDA_PAD_DOWN) ? 1.0f : 0.0f},
            fsui::InputOverlayBinding{.label = "Left", .glyph = "<", .kind = fsui::InputOverlayBindingKind::Button, .value = (active_digital_mask_ & PSXI_SW_SDA_PAD_LEFT) ? 1.0f : 0.0f},
            fsui::InputOverlayBinding{.label = "Right", .glyph = ">", .kind = fsui::InputOverlayBindingKind::Button, .value = (active_digital_mask_ & PSXI_SW_SDA_PAD_RIGHT) ? 1.0f : 0.0f},
            fsui::InputOverlayBinding{.label = "Start", .glyph = "S", .kind = fsui::InputOverlayBindingKind::Button, .value = (active_digital_mask_ & PSXI_SW_SDA_START) ? 1.0f : 0.0f},
            fsui::InputOverlayBinding{.label = "Select", .glyph = "=", .kind = fsui::InputOverlayBindingKind::Button, .value = (active_digital_mask_ & PSXI_SW_SDA_SELECT) ? 1.0f : 0.0f},
        };
        return {device};
    }

    void processEvent(const SDL_Event& event, ArmsxSession* session, bool fsui_active) {
        flushChordButtons(SDL_GetTicks());
        handleControllerLifecycle(event);

        if (!session || !session->valid()) {
            return;
        }

        if (event.type == SDL_KEYDOWN) {
            if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_ESCAPE && !fsui_active) {
                pause_requested_ = true;
                return;
            }

            if (fsui_active || fsui_owns_input_ || event.key.repeat != 0) {
                return;
            }

            const uint32_t mask = buttonForKey(event.key.keysym.sym);
            if (mask) {
                press(mask);
            }
            return;
        }

        if (event.type == SDL_KEYUP) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                return;
            }

            if (fsui_active || fsui_owns_input_) {
                return;
            }

            const uint32_t mask = buttonForKey(event.key.keysym.sym);
            if (mask) {
                release(mask);
            }
            return;
        }

        if (!controller_) {
            return;
        }

        SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller_);
        if (!joystick) {
            return;
        }

        const SDL_JoystickID controller_id = SDL_JoystickInstanceID(joystick);

        if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
            if (event.cbutton.which != controller_id) {
                return;
            }

            const bool pressed = event.type == SDL_CONTROLLERBUTTONDOWN;
            const SDL_GameControllerButton button = static_cast<SDL_GameControllerButton>(event.cbutton.button);

            if (button == SDL_CONTROLLER_BUTTON_START || button == SDL_CONTROLLER_BUTTON_BACK ||
                button == SDL_CONTROLLER_BUTTON_GUIDE || button == SDL_CONTROLLER_BUTTON_MISC1) {
                handlePauseChordButton(button, pressed, fsui_active);
                return;
            }

            if (fsui_active || fsui_owns_input_) {
                return;
            }

            const uint32_t mask = buttonForController(button);
            if (mask) {
                if (pressed) {
                    press(mask);
                } else {
                    release(mask);
                }
            }
            return;
        }

        if (event.type == SDL_CONTROLLERAXISMOTION) {
            if (event.caxis.which != controller_id || fsui_active || fsui_owns_input_ || !pad_) {
                return;
            }

            const uint16_t mapped = static_cast<uint16_t>((static_cast<int>(event.caxis.value) + INT16_MAX + 1) / 0x100);
            switch (event.caxis.axis) {
                case SDL_CONTROLLER_AXIS_RIGHTX:
                    psx_pad_analog_change(pad_, 0, PSXI_AX_SDA_RIGHT_HORZ, mapped);
                    break;
                case SDL_CONTROLLER_AXIS_RIGHTY:
                    psx_pad_analog_change(pad_, 0, PSXI_AX_SDA_RIGHT_VERT, mapped);
                    break;
                case SDL_CONTROLLER_AXIS_LEFTX:
                    psx_pad_analog_change(pad_, 0, PSXI_AX_SDA_LEFT_HORZ, mapped);
                    break;
                case SDL_CONTROLLER_AXIS_LEFTY:
                    psx_pad_analog_change(pad_, 0, PSXI_AX_SDA_LEFT_VERT, mapped);
                    break;
#ifdef CONTROLLER_GENERIC
                case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
                    handleTrigger(PSXI_SW_SDA_L2, trigger_left_down_, event.caxis.value);
                    break;
                case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
                    handleTrigger(PSXI_SW_SDA_R2, trigger_right_down_, event.caxis.value);
                    break;
#endif
                default:
                    break;
            }
        }
    }

  private:
    void handleControllerLifecycle(const SDL_Event& event) {
        if (event.type == SDL_CONTROLLERDEVICEADDED) {
            if (!controller_ && SDL_IsGameController(event.cdevice.which)) {
                controller_ = SDL_GameControllerOpen(event.cdevice.which);
            }
            return;
        }

        if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller_) {
            SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller_);
            if (joystick && SDL_JoystickInstanceID(joystick) == event.cdevice.which) {
                SDL_GameControllerClose(controller_);
                controller_ = nullptr;
                start_button_ = {};
                select_button_ = {};
                trigger_left_down_ = false;
                trigger_right_down_ = false;
            }
        }
    }

    void handlePauseChordButton(SDL_GameControllerButton button, bool pressed, bool fsui_active) {
        PendingChordButton* state = nullptr;
        PendingChordButton* other = nullptr;

        if (button == SDL_CONTROLLER_BUTTON_START) {
            state = &start_button_;
            other = &select_button_;
        } else {
            state = &select_button_;
            other = &start_button_;
        }

        if (pressed) {
            state->physical_down = true;
            if (!fsui_active && !fsui_owns_input_ && !pause_latched_) {
                state->pending = true;
                state->pending_since = SDL_GetTicks();
                if (other->physical_down && other->pending) {
                    pause_latched_ = true;
                    pause_requested_ = true;
                    state->pending = false;
                    other->pending = false;
                    clearAll();
                }
            }
            return;
        }

        state->physical_down = false;

        if (pause_latched_) {
            if (!start_button_.physical_down && !select_button_.physical_down) {
                pause_latched_ = false;
                start_button_ = {};
                select_button_ = {};
            }
            return;
        }

        if (state->pending) {
            const uint32_t mask = (state == &start_button_) ? PSXI_SW_SDA_START : PSXI_SW_SDA_SELECT;
            press(mask);
            release(mask);
            state->pending = false;
            return;
        }

        if (state->forwarded) {
            const uint32_t mask = (state == &start_button_) ? PSXI_SW_SDA_START : PSXI_SW_SDA_SELECT;
            release(mask);
            state->forwarded = false;
        }
    }

    void flushChordButtons(Uint32 now) {
        if (pause_latched_ || fsui_owns_input_) {
            return;
        }

        flushOneChordButton(start_button_, select_button_, PSXI_SW_SDA_START, now);
        flushOneChordButton(select_button_, start_button_, PSXI_SW_SDA_SELECT, now);
    }

    void flushOneChordButton(PendingChordButton& state, PendingChordButton& other, uint32_t mask, Uint32 now) {
        if (!state.pending || other.physical_down || !pad_) {
            return;
        }

        if ((now - state.pending_since) < kPauseChordGraceMs) {
            return;
        }

        press(mask);
        state.pending = false;
        state.forwarded = true;
    }

    void press(uint32_t mask) {
        if (!pad_ || !mask) {
            return;
        }

        if ((active_digital_mask_ & mask) == 0) {
            psx_pad_button_press(pad_, 0, mask);
            active_digital_mask_ |= mask;
        }
    }

    void release(uint32_t mask) {
        if (!pad_ || !mask) {
            return;
        }

        if ((active_digital_mask_ & mask) != 0) {
            psx_pad_button_release(pad_, 0, mask);
            active_digital_mask_ &= ~mask;
        }
    }

    void clearAll() {
        if (!pad_) {
            active_digital_mask_ = 0;
            return;
        }

        static const std::array<uint32_t, 17> masks = {
            PSXI_SW_SDA_SELECT, PSXI_SW_SDA_L3, PSXI_SW_SDA_R3, PSXI_SW_SDA_START,
            PSXI_SW_SDA_PAD_UP, PSXI_SW_SDA_PAD_RIGHT, PSXI_SW_SDA_PAD_DOWN, PSXI_SW_SDA_PAD_LEFT,
            PSXI_SW_SDA_L2, PSXI_SW_SDA_R2, PSXI_SW_SDA_L1, PSXI_SW_SDA_R1,
            PSXI_SW_SDA_TRIANGLE, PSXI_SW_SDA_CIRCLE, PSXI_SW_SDA_CROSS, PSXI_SW_SDA_SQUARE,
            PSXI_SW_SDA_ANALOG,
        };

        for (uint32_t mask : masks) {
            if ((active_digital_mask_ & mask) != 0) {
                psx_pad_button_release(pad_, 0, mask);
            }
        }

        active_digital_mask_ = 0;
        psx_pad_analog_change(pad_, 0, PSXI_AX_SDA_RIGHT_HORZ, 0x80);
        psx_pad_analog_change(pad_, 0, PSXI_AX_SDA_RIGHT_VERT, 0x80);
        psx_pad_analog_change(pad_, 0, PSXI_AX_SDA_LEFT_HORZ, 0x80);
        psx_pad_analog_change(pad_, 0, PSXI_AX_SDA_LEFT_VERT, 0x80);
    }

#ifdef CONTROLLER_GENERIC
    void handleTrigger(uint32_t mask, bool& down_flag, Sint16 value) {
        const bool pressed = value > 8000;
        if (pressed == down_flag) {
            return;
        }

        down_flag = pressed;
        if (pressed) {
            press(mask);
        } else {
            release(mask);
        }
    }
#endif

    static uint32_t buttonForKey(SDL_Keycode key) {
        switch (key) {
            case SDLK_x: return PSXI_SW_SDA_CROSS;
            case SDLK_a: return PSXI_SW_SDA_SQUARE;
            case SDLK_w: return PSXI_SW_SDA_TRIANGLE;
            case SDLK_d: return PSXI_SW_SDA_CIRCLE;
            case SDLK_RETURN: return PSXI_SW_SDA_START;
            case SDLK_s: return PSXI_SW_SDA_SELECT;
            case SDLK_UP: return PSXI_SW_SDA_PAD_UP;
            case SDLK_DOWN: return PSXI_SW_SDA_PAD_DOWN;
            case SDLK_LEFT: return PSXI_SW_SDA_PAD_LEFT;
            case SDLK_RIGHT: return PSXI_SW_SDA_PAD_RIGHT;
            case SDLK_q: return PSXI_SW_SDA_L1;
            case SDLK_e: return PSXI_SW_SDA_R1;
            case SDLK_1: return PSXI_SW_SDA_L2;
            case SDLK_3: return PSXI_SW_SDA_R2;
            case SDLK_z: return PSXI_SW_SDA_L3;
            case SDLK_c: return PSXI_SW_SDA_R3;
            case SDLK_2: return PSXI_SW_SDA_ANALOG;
            default: return 0;
        }
    }

    static uint32_t buttonForController(SDL_GameControllerButton button) {
        switch (button) {
            case SDL_CONTROLLER_BUTTON_A: return PSXI_SW_SDA_CROSS;
            case SDL_CONTROLLER_BUTTON_X: return PSXI_SW_SDA_SQUARE;
            case SDL_CONTROLLER_BUTTON_Y: return PSXI_SW_SDA_TRIANGLE;
            case SDL_CONTROLLER_BUTTON_B: return PSXI_SW_SDA_CIRCLE;
            case SDL_CONTROLLER_BUTTON_DPAD_UP: return PSXI_SW_SDA_PAD_UP;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return PSXI_SW_SDA_PAD_DOWN;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return PSXI_SW_SDA_PAD_LEFT;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return PSXI_SW_SDA_PAD_RIGHT;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return PSXI_SW_SDA_L1;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return PSXI_SW_SDA_R1;
            case SDL_CONTROLLER_BUTTON_LEFTSTICK: return PSXI_SW_SDA_L3;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return PSXI_SW_SDA_R3;
            default: return 0;
        }
    }

    psx_pad_t* pad_ = nullptr;
    SDL_GameController* controller_ = nullptr;
    uint32_t active_digital_mask_ = 0;
    PendingChordButton start_button_{};
    PendingChordButton select_button_{};
    bool pause_requested_ = false;
    bool pause_latched_ = false;
    bool fsui_owns_input_ = false;
    bool trigger_left_down_ = false;
    bool trigger_right_down_ = false;
};

class ArmsxApp {
  public:
    ArmsxApp(int argc, const char* argv[], void* external_window, void* external_renderer)
        : argc_(argc), argv_(argv), external_window_(static_cast<SDL_Window*>(external_window)),
          external_renderer_(static_cast<SDL_Renderer*>(external_renderer)), cli_(ScanCliFlags(argc, argv)) {}

    int run() {
        psxe_config_t* cfg = psxe_cfg_create();
        if (!cfg) {
            return 1;
        }

        psxe_cfg_init(cfg);
        psxe_cfg_load_defaults(cfg);
        psxe_cfg_load(cfg, argc_, const_cast<const char**>(argv_));
        settings_ = BuildSettings(cfg, cli_);
        psxe_cfg_destroy(cfg);

        log_set_quiet(settings_.quiet ? 1 : 0);
        log_set_level(settings_.log_level);

        if (!initializeSdl() || !initializeWindowAndRenderer() || !initializeFsui()) {
            shutdown();
            return 1;
        }

        refreshGameList(true);

        g_active_app = this;

        if (g_pending_wasm_path.has_value()) {
            onWasmFile(*g_pending_wasm_path);
            g_pending_wasm_path.reset();
        } else if (cli_.has_boot_request) {
            LaunchRequest request;
            if (cli_.exe && !pending_cli_path_.empty()) {
                request.kind = LaunchKind::Exe;
                request.path = pending_cli_path_;
                request.label = StemToTitle(request.path);
            } else if ((cli_.cdrom || !pending_cli_path_.empty()) && !pending_cli_path_.empty()) {
                request.kind = IsExePath(pending_cli_path_) ? LaunchKind::Exe : LaunchKind::Disc;
                request.path = pending_cli_path_;
                request.label = StemToTitle(request.path);
            }

            if (request.kind != LaunchKind::None) {
                launchSession(request, false);
            }
        }

        if (!session_.valid()) {
            fsui::ShowLandingWindow();
        }

        fsui::SdlMainLoopCallbacks loop;
        loop.handle_event = [this](const SDL_Event& event) { handleEvent(event); };
        loop.run_frame = [this]() { runFrame(); };
        loop.should_continue = [this]() { return running_; };
        loop.on_exit = [this]() { shutdown(); };
        fsui::RunSdlMainLoop(loop);
        g_active_app = nullptr;

        return 0;
    }

    void onWasmFile(const std::filesystem::path& path) {
        if (path.empty()) {
            return;
        }

        const LaunchRequest request = LaunchForPath(path);
        if (request.kind == LaunchKind::None) {
            pending_error_dialog_ = "Unsupported file type for browser launch.";
            if (!session_.valid()) {
                fsui::ShowLandingWindow();
            }
            return;
        }

        deferred_launch_ = request;
        close_ui_after_launch_ = true;
    }

  private:
    bool initializeSdl() {
        const Uint32 required = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER;

        if (SDL_WasInit(0) == 0) {
            owns_sdl_ = true;
            if (SDL_Init(required) != 0) {
                return false;
            }
        } else if (SDL_InitSubSystem(required) != 0) {
            return false;
        }

        if (argc_ > 1) {
            for (int index = 1; index < argc_; index++) {
                const std::string_view arg(argv_[index] ? argv_[index] : "");
                if (arg == "--cdrom" || arg == "-x" || arg == "--exe") {
                    if ((index + 1) < argc_) {
                        pending_cli_path_ = argv_[index + 1];
                    }
                } else if (arg.starts_with("--cdrom=")) {
                    pending_cli_path_ = std::string(arg.substr(8));
                } else if (arg.starts_with("--exe=")) {
                    pending_cli_path_ = std::string(arg.substr(6));
                } else if (!arg.empty() && arg[0] != '-') {
                    pending_cli_path_ = argv_[index];
                }
            }
        }

        return true;
    }

    bool initializeWindowAndRenderer() {
        if (external_window_) {
            window_ = external_window_;
        } else {
            int window_width = 1280;
            int window_height = 720;
#if defined(__EMSCRIPTEN__)
            double css_width = 0.0;
            double css_height = 0.0;
            if (emscripten_get_element_css_size("#canvas", &css_width, &css_height) == EMSCRIPTEN_RESULT_SUCCESS &&
                css_width > 0.0 && css_height > 0.0) {
                window_width = std::max(1, static_cast<int>(std::lround(css_width)));
                window_height = std::max(1, static_cast<int>(std::lround(css_height)));
            }
#endif
            Uint32 flags = 0;
            if (SupportsManagedWindowSizing()) {
                flags |= SDL_WINDOW_RESIZABLE;
            }
            window_ = SDL_CreateWindow(
                "ARMSX",
                SDL_WINDOWPOS_CENTERED,
                SDL_WINDOWPOS_CENTERED,
                window_width,
                window_height,
                flags
            );
            owns_window_ = window_ != nullptr;
        }

        if (!window_) {
            return false;
        }

        if (external_renderer_) {
            renderer_ = external_renderer_;
        } else {
            Uint32 renderer_flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
#if defined(__EMSCRIPTEN__)
            // Browser presentation is already frame-paced by the main loop.
            renderer_flags = SDL_RENDERER_ACCELERATED;
#endif
            renderer_ = SDL_CreateRenderer(window_, -1, renderer_flags);
            owns_renderer_ = renderer_ != nullptr;
        }

        return renderer_ != nullptr;
    }

    bool initializeFsui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.IniFilename = nullptr;

        fsui::SdlImGuiBackendConfig backend_config;
        backend_config.window = window_;
        backend_config.sdl_renderer = renderer_;
        backend_config.renderer_backend = fsui::RendererBackend::SDLRenderer;
#if defined(__EMSCRIPTEN__)
        // SDL_Renderer on Emscripten renders correctly when the canvas backing store
        // matches the CSS size instead of a DPR-scaled framebuffer.
        backend_config.web_canvas.max_device_pixel_ratio = 1.0;
#endif

        if (!imgui_backend_.initialize(backend_config)) {
            return false;
        }

#if defined(__EMSCRIPTEN__)
        fsui::SyncSdl2WebCanvasLayout(window_, backend_config.web_canvas, true);
#endif

        fsui::FontStack fonts;
        if (!fsui::BuildDefaultFontStack(fsui::DescribeSdl2FontBootstrap(window_), fonts)) {
            return false;
        }

        fsui::UiContext context;
        context.window = window_;
        context.fonts = fonts;
        context.app_title = "ARMSX";
        context.app_icon_path = ResolveFsuiAppIconPath();
        context.platform_backend = fsui::PlatformBackendKind::SDL2;
        context.renderer_backend = fsui::RendererBackend::SDLRenderer;

        fsui::Host host;
        host.ui_state = &settings_.ui_state;
        host.has_running_game = [this]() { return session_.valid(); };
        host.get_current_game = [this]() { return session_.currentGameInfo(); };
        host.get_game_list = [this]() { return game_list_; };
        host.refresh_game_list = [this](bool full_rescan) { refreshGameList(full_rescan); };
        host.persist_ui_state = [this](bool reload) {
            if (reload) {
                refreshGameList(true);
            }
            SaveSettings(settings_);
        };
        host.set_paused = [this](bool paused) {
            if (!session_.valid()) {
                return;
            }
            session_.setPaused(paused);
            if (paused) {
                input_router_.onFsuiOpened();
            }
        };
        host.resume_game = [this]() {
            if (session_.valid()) {
                session_.setPaused(false);
            }
        };
        host.reset_system = [this]() {
            if (session_.valid()) {
                session_.reset();
            }
        };
        host.exit_to_library = [this]() { exitToLibrary(); };
        host.request_quit = [this]() { running_ = false; };
        host.request_classic_ui = []() {};
        host.close_selector = []() {};
        host.detect_prompt_icon_pack = []() { return fsui::DetectPromptIconPackFromSDL(); };
        host.detect_swap_north_west_gamepad_buttons = []() { return false; };
        host.runtime_overlay_options = fsui::RuntimeOverlayOptions{
            .show_inputs = true,
            .show_settings = true,
            .show_performance = true,
        };
        host.get_input_overlay_devices = [this]() { return input_router_.buildInputOverlay(); };
        host.get_settings_overlay_lines = [this]() { return session_.settingsOverlayLines(settings_); };
        host.get_performance_overlay_lines = [this]() { return session_.performanceOverlayLines(settings_); };
        host.get_landing_items = [this]() { return buildLandingItems(); };
        host.get_start_items = [this]() { return buildStartItems(); };
        host.get_exit_items = [this]() { return buildExitItems(); };
        host.get_pause_items = [this]() { return buildPauseItems(); };
        host.get_game_launch_options = [this](const fsui::GameEntry& entry) { return buildGameLaunchOptions(entry); };
        host.get_settings_pages = [this](fsui::SettingsScope scope) { return buildSettingsPages(scope); };

        context.host = std::move(host);

        if (!fsui::Initialize(context)) {
            return false;
        }

        fsui_initialized_ = true;
        return true;
    }

    void shutdown() {
        input_router_.detach();
        session_.destroy();

        if (fsui_initialized_) {
            fsui::Shutdown(true);
            fsui_initialized_ = false;
        }

        imgui_backend_.shutdown();
        ImGui::DestroyContext();

        if (owns_renderer_ && renderer_) {
            SDL_DestroyRenderer(renderer_);
        }
        if (owns_window_ && window_) {
            SDL_DestroyWindow(window_);
        }

        renderer_ = nullptr;
        window_ = nullptr;

        if (owns_sdl_) {
            SDL_Quit();
        }
    }

    void handleEvent(const SDL_Event& event) {
        imgui_backend_.processEvent(event);

        if (event.type == SDL_QUIT) {
            running_ = false;
            return;
        }

        const bool fsui_active = fsui::HasActiveWindow();
        input_router_.processEvent(event, session_.valid() ? &session_ : nullptr, fsui_active);

        if (event.type == SDL_KEYDOWN && !fsui_active && session_.valid() && event.key.repeat == 0 && event.key.keysym.sym == SDLK_RETURN) {
            psx_exp2_atcons_put(session_.psx()->exp2, 13);
        }

        if (session_.valid() && !fsui_active && input_router_.takePauseRequest()) {
            openPauseMenu();
        }
    }

    void runFrame() {
        const bool fsui_was_active = fsui::HasActiveWindow();

        input_router_.tick(fsui_was_active);

        if (session_.valid()) {
            session_.runFrame();
            session_.updateTexture(settings_);
        }

        imgui_backend_.newFrame();
        ImGui::NewFrame();

        if (session_.valid()) {
            session_.draw(settings_);
        }

        if (pending_error_dialog_.has_value()) {
            ImGuiFullscreen::OpenInfoMessageDialog("ARMSX", *pending_error_dialog_);
            pending_error_dialog_.reset();
        }

        fsui::Render();

        for (const fsui::Command& command : fsui::ConsumeCommands()) {
            applyCommand(command);
        }

        applyDeferredActions();

        ImGui::Render();
        imgui_backend_.renderDrawData(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

        const bool fsui_is_active = fsui::HasActiveWindow();

        if (fsui_was_active && !fsui_is_active && session_.valid()) {
            input_router_.onFsuiClosed();
        } else if (!fsui_was_active && fsui_is_active && session_.valid()) {
            input_router_.onFsuiOpened();
        }
    }

    void applyCommand(const fsui::Command& command) {
        switch (command.type) {
            case fsui::CommandType::LaunchPath:
                deferred_launch_ = LaunchForPath(command.path);
                close_ui_after_launch_ = true;
                break;

            case fsui::CommandType::RequestQuit:
                if (session_.valid()) {
                    exitToLibrary();
                } else {
                    running_ = false;
                }
                break;

            case fsui::CommandType::Resume:
                if (session_.valid()) {
                    session_.setPaused(false);
                }
                break;

            case fsui::CommandType::Reset:
                if (session_.valid()) {
                    session_.reset();
                    session_.setPaused(false);
                }
                break;

            case fsui::CommandType::ExitToLibrary:
                exitToLibrary();
                break;

            case fsui::CommandType::CloseSelector:
            case fsui::CommandType::ExitFullscreenUI:
            case fsui::CommandType::SwitchShell:
            case fsui::CommandType::Custom:
            default:
                break;
        }
    }

    void applyDeferredActions() {
        if (deferred_launch_.has_value()) {
            const LaunchRequest request = *deferred_launch_;
            deferred_launch_.reset();
            launchSession(request, close_ui_after_launch_);
            close_ui_after_launch_ = false;
        }

        if (deferred_exit_to_library_) {
            deferred_exit_to_library_ = false;
            exitToLibrary();
        }

        if (deferred_reset_) {
            deferred_reset_ = false;
            if (session_.valid()) {
                session_.reset();
                session_.setPaused(false);
            }
        }

        if (deferred_change_disc_.has_value()) {
            const std::filesystem::path path = *deferred_change_disc_;
            deferred_change_disc_.reset();

            if (!session_.swapDisc(path)) {
                pending_error_dialog_ = "Failed to swap to the selected disc image.";
            } else {
                session_.setPaused(false);
                refreshGameList(false);
                fsui::ReturnToMainWindow();
            }
        }

        if (deferred_screenshot_) {
            deferred_screenshot_ = false;

            const std::filesystem::path snap_dir = DefaultBrowseDirectory() / "snap";
            try {
                std::filesystem::create_directories(snap_dir);
                const std::time_t now = std::time(nullptr);
                char name[64] = {};
                std::strftime(name, sizeof(name), "armsx-%Y%m%d-%H%M%S.bmp", std::localtime(&now));
                if (!session_.saveScreenshot(snap_dir / name)) {
                    pending_error_dialog_ = "Failed to write the screenshot.";
                }
            } catch (...) {
                pending_error_dialog_ = "Failed to prepare the screenshot folder.";
            }
        }
    }

    bool launchSession(const LaunchRequest& request, bool close_ui) {
        std::string error;

        if (!session_.create(renderer_, settings_, request, error)) {
            pending_error_dialog_ = error;
            return false;
        }

        input_router_.attach(session_.pad());
        applyWindowMetrics();
        refreshGameList(false);

        if (close_ui) {
            fsui::ReturnToMainWindow();
        }

        return true;
    }

    void exitToLibrary() {
        input_router_.detach();
        session_.destroy();
        fsui::ShowLandingWindow();
    }

    void openPauseMenu() {
        if (!session_.valid()) {
            return;
        }

        session_.setPaused(true);
        input_router_.onFsuiOpened();
        fsui::OpenPauseMenu();
    }

    void refreshGameList(bool full_rescan) {
        (void)full_rescan;
        game_list_.clear();

        auto scan_root = [&](const std::filesystem::path& root, bool recursive) {
            try {
                if (!std::filesystem::exists(root)) {
                    return;
                }

                auto append_entry = [&](const std::filesystem::directory_entry& item) {
                    if (!item.is_regular_file()) {
                        return;
                    }

                    const std::filesystem::path path = item.path();
                    if (!IsDiscPath(path) && !IsExePath(path)) {
                        return;
                    }

                    fsui::GameEntry entry;
                    entry.path = path;
                    entry.type = IsExePath(path) ? fsui::GameEntryType::Homebrew : fsui::GameEntryType::Application;
                    entry.title = StemToTitle(path);
                    entry.title_sort = ToLower(entry.title);
                    entry.english_title = entry.title;
                    entry.title_id = NormalizeModel(entry.title);
                    entry.file_size = item.file_size();
                    entry.modified_time = FileTimeToTimeT(item.last_write_time());

                    const int region = RegionFromSettings(settings_);
                    if (region == CDR_REGION_EUROPE) {
                        entry.region = "Europe";
                        entry.region_flag_path = "icons/flags/eu.png";
                    } else if (region == CDR_REGION_JAPAN) {
                        entry.region = "Japan";
                        entry.region_flag_path = "icons/flags/jp.png";
                    } else {
                        entry.region = "North America";
                        entry.region_flag_path = "icons/flags/us.png";
                    }

                    game_list_.push_back(std::move(entry));
                };

                if (recursive) {
                    for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
                        append_entry(item);
                    }
                } else {
                    for (const auto& item : std::filesystem::directory_iterator(root)) {
                        append_entry(item);
                    }
                }
            } catch (...) {
            }
        };

        for (const auto& root : settings_.ui_state.game_list_paths) {
            scan_root(root, false);
        }

        for (const auto& root : settings_.ui_state.game_list_recursive_paths) {
            scan_root(root, true);
        }

        std::sort(game_list_.begin(), game_list_.end(), [](const fsui::GameEntry& left, const fsui::GameEntry& right) {
            if (left.title_sort == right.title_sort) {
                return left.path < right.path;
            }
            return left.title_sort < right.title_sort;
        });
    }

    std::vector<fsui::MenuItemDescriptor> buildLandingItems() {
        std::vector<fsui::MenuItemDescriptor> items;

        fsui::MenuItemDescriptor game_list;
        game_list.id = "game-list";
        game_list.icon_path = fsui::GetBuiltInStartupIconPath(fsui::BuiltInStartupIcon::GameList);
        game_list.title = "Game List";
        game_list.summary = "Browse library folders and boot a title through the native SDL shell.";
        game_list.on_activate = []() { fsui::ShowGameListWindow(); };
        items.push_back(std::move(game_list));

        fsui::MenuItemDescriptor start;
        start.id = "start-game";
        start.icon_path = fsui::GetBuiltInStartupIconPath(fsui::BuiltInStartupIcon::StartGame);
        start.title = "Start Game";
        start.summary = "Boot a disc, PS-X EXE, or the BIOS directly.";
        start.on_activate = []() { fsui::ShowStartGameWindow(); };
        items.push_back(std::move(start));

        fsui::MenuItemDescriptor settings;
        settings.id = "settings";
        settings.icon_path = fsui::GetBuiltInStartupIconPath(fsui::BuiltInStartupIcon::Settings);
        settings.title = "Settings";
        settings.summary = "Edit the real ARMSX runtime, BIOS, video, and library settings.";
        settings.on_activate = []() { fsui::SwitchToSettings(); };
        items.push_back(std::move(settings));

        fsui::MenuItemDescriptor exit;
        exit.id = "exit";
        exit.icon_path = fsui::GetBuiltInStartupIconPath(fsui::BuiltInStartupIcon::Exit);
        exit.title = "Exit";
        exit.summary = "Quit ARMSX.";
        exit.on_activate = []() { fsui::ShowExitWindow(); };
        items.push_back(std::move(exit));

        return items;
    }

    std::vector<fsui::MenuItemDescriptor> buildStartItems() {
        std::vector<fsui::MenuItemDescriptor> items;

        auto default_dir = initialBrowseDirectory();

        fsui::MenuItemDescriptor start_file;
        start_file.id = "start-file";
        start_file.icon_path = fsui::GetBuiltInStartupIconPath(fsui::BuiltInStartupIcon::StartFile);
        start_file.title = "Start File";
        start_file.summary = "Pick a disc image or PS-X EXE from the managed filesystem.";
        start_file.on_activate = [this, default_dir]() {
            ImGuiFullscreen::OpenFileSelector(
                "Start File",
                false,
                [this](const std::string& path) {
                    deferred_launch_ = LaunchForPath(path);
                    close_ui_after_launch_ = true;
                },
                {".cue", ".bin", ".iso", ".img", ".exe", ".ps-exe", ".psexe"},
                default_dir.string()
            );
        };
        items.push_back(std::move(start_file));

        if (!settings_.default_exe_path.empty()) {
            fsui::MenuItemDescriptor start_saved_exe;
            start_saved_exe.id = "start-default-exe";
            start_saved_exe.icon_path = fsui::GetBuiltInStartupIconPath(fsui::BuiltInStartupIcon::StartFile);
            start_saved_exe.title = "Start Saved PS-X EXE";
            start_saved_exe.summary = settings_.default_exe_path;
            start_saved_exe.on_activate = [this]() {
                deferred_launch_ = LaunchRequest{
                    .kind = LaunchKind::Exe,
                    .path = std::filesystem::path(settings_.default_exe_path),
                    .label = StemToTitle(settings_.default_exe_path),
                };
                close_ui_after_launch_ = true;
            };
            items.push_back(std::move(start_saved_exe));
        }

        fsui::MenuItemDescriptor start_disc;
        start_disc.id = "start-disc";
        start_disc.icon_path = fsui::GetBuiltInStartupIconPath(fsui::BuiltInStartupIcon::StartDisc);
        start_disc.title = "Start Disc";
        start_disc.summary = "Pick a disc image directly.";
        start_disc.on_activate = [this, default_dir]() {
            ImGuiFullscreen::OpenFileSelector(
                "Start Disc",
                false,
                [this](const std::string& path) {
                    deferred_launch_ = LaunchForPath(path);
                    close_ui_after_launch_ = true;
                },
                {".cue", ".bin", ".iso", ".img"},
                default_dir.string()
            );
        };
        items.push_back(std::move(start_disc));

        fsui::MenuItemDescriptor start_bios;
        start_bios.id = "start-bios";
        start_bios.icon_path = fsui::GetBuiltInStartupIconPath(fsui::BuiltInStartupIcon::StartBios);
        start_bios.title = "Start BIOS";
        start_bios.summary = "Boot into the BIOS without media.";
        start_bios.on_activate = [this]() {
            deferred_launch_ = LaunchRequest{.kind = LaunchKind::Bios, .path = {}, .label = "PlayStation BIOS"};
            close_ui_after_launch_ = true;
        };
        items.push_back(std::move(start_bios));

        fsui::MenuItemDescriptor back;
        back.id = "back";
        back.icon_path = fsui::GetBuiltInStartupIconPath(fsui::BuiltInStartupIcon::Back);
        back.title = "Back";
        back.summary = "Return to the landing screen.";
        back.on_activate = []() { fsui::ShowLandingWindow(); };
        items.push_back(std::move(back));

        return items;
    }

    std::vector<fsui::MenuItemDescriptor> buildExitItems() {
        std::vector<fsui::MenuItemDescriptor> items;

        fsui::MenuItemDescriptor quit;
        quit.id = "quit";
        quit.icon_path = fsui::GetBuiltInStartupIconPath(fsui::BuiltInStartupIcon::Exit);
        quit.title = "Quit";
        quit.summary = "Close ARMSX.";
        quit.command = fsui::MakeRequestQuitCommand();
        items.push_back(std::move(quit));

        fsui::MenuItemDescriptor back;
        back.id = "back";
        back.icon_path = fsui::GetBuiltInStartupIconPath(fsui::BuiltInStartupIcon::Back);
        back.title = "Back";
        back.summary = "Return to the landing screen.";
        back.on_activate = []() { fsui::ShowLandingWindow(); };
        items.push_back(std::move(back));

        return items;
    }

    std::vector<fsui::MenuItemDescriptor> buildPauseItems() {
        std::vector<fsui::MenuItemDescriptor> items;

        fsui::MenuItemDescriptor resume;
        resume.id = "resume";
        resume.title = ICON_FA_PLAY " Resume";
        resume.summary = "Return to gameplay.";
        resume.command = fsui::MakeResumeCommand();
        items.push_back(std::move(resume));

        fsui::MenuItemDescriptor reset;
        reset.id = "reset";
        reset.title = ICON_FA_SYNC " Reset";
        reset.summary = "Soft-reset the active session.";
        reset.command = fsui::MakeResetCommand();
        items.push_back(std::move(reset));

        fsui::MenuItemDescriptor change_disc;
        change_disc.id = "change-disc";
        change_disc.title = ICON_FA_COMPACT_DISC " Change Disc";
        change_disc.summary = "Swap to another disc image and resume.";
        change_disc.on_activate = [this]() {
            ImGuiFullscreen::OpenFileSelector(
                "Change Disc",
                false,
                [this](const std::string& path) { deferred_change_disc_ = std::filesystem::path(path); },
                {".cue", ".bin", ".iso", ".img"},
                initialBrowseDirectory().string()
            );
        };
        items.push_back(std::move(change_disc));

        fsui::MenuItemDescriptor screenshot;
        screenshot.id = "screenshot";
        screenshot.title = ICON_FA_CAMERA " Save Screenshot";
        screenshot.summary = "Write a BMP screenshot to the managed snapshot folder.";
        screenshot.on_activate = [this]() { deferred_screenshot_ = true; };
        items.push_back(std::move(screenshot));

        fsui::MenuItemDescriptor settings;
        settings.id = "settings";
        settings.title = ICON_FA_SLIDERS_H " Settings";
        settings.summary = "Open the in-game settings pages.";
        settings.on_activate = []() { fsui::SwitchToSettings(); };
        items.push_back(std::move(settings));

        fsui::MenuItemDescriptor quit;
        quit.id = "quit-game";
        quit.title = ICON_FA_POWER_OFF " Quit Game";
        quit.summary = "Destroy the active session and return to the FSUI shell.";
        quit.command = fsui::MakeExitToLibraryCommand();
        items.push_back(std::move(quit));

        return items;
    }

    std::vector<fsui::MenuItemDescriptor> buildGameLaunchOptions(const fsui::GameEntry&) {
        return {};
    }

    std::vector<fsui::SettingsPageDescriptor> buildSettingsPages(fsui::SettingsScope scope) {
        if (scope == fsui::SettingsScope::PerGame) {
            return buildPerGameSettingsPages();
        }

        return buildGlobalSettingsPages();
    }

    std::vector<fsui::SettingsPageDescriptor> buildGlobalSettingsPages() {
        std::vector<fsui::SettingsPageDescriptor> pages;

        fsui::SettingsPageDescriptor bios_page;
        bios_page.id = "BIOS";
        bios_page.scope = fsui::SettingsScope::Global;
        bios_page.built_in_page = fsui::BuiltInSettingsPage::BIOS;
        bios_page.build_rows = [this]() {
            std::vector<fsui::SettingsRowDescriptor> rows;

            fsui::SettingsRowDescriptor model;
            model.kind = fsui::SettingsRowKind::Choice;
            model.id = "model";
            model.title = ICON_FA_MICROCHIP " Model";
            model.summary = "Select the preferred BIOS model when using a BIOS folder.";
            model.value = settings_.model;
            model.dialog_title = model.title;
            model.choices = BuildModelChoices(settings_.model);
            model.on_choice = [this](int index) {
                const auto models = ModelChoices();
                if (index >= 0 && index < static_cast<int>(models.size())) {
                    settings_.model = models[static_cast<size_t>(index)];
                    SaveSettings(settings_);
                }
            };
            rows.push_back(std::move(model));

            fsui::SettingsRowDescriptor folder;
            folder.kind = fsui::SettingsRowKind::Action;
            folder.id = "bios-folder";
            folder.title = ICON_FA_FOLDER " BIOS Folder";
            folder.summary = settings_.bios_search.empty() ? "Choose a folder that contains BIOS files." : settings_.bios_search;
            folder.on_activate = [this]() {
                ImGuiFullscreen::OpenFileSelector(
                    "BIOS Folder",
                    true,
                    [this](const std::string& path) {
                        settings_.bios_search = path;
                        SaveSettings(settings_);
                    },
                    {},
                    browseDirectoryForPath(settings_.bios_search, true).string()
                );
            };
            rows.push_back(std::move(folder));

            fsui::SettingsRowDescriptor override;
            override.kind = fsui::SettingsRowKind::Action;
            override.id = "bios-override";
            override.title = ICON_FA_FILE " BIOS Override";
            override.summary = settings_.bios_override.empty() ? "Auto-select from the BIOS folder." : settings_.bios_override;
            override.on_activate = [this]() {
                const std::string browse_seed = settings_.bios_override.empty() ? settings_.bios_search : settings_.bios_override;
                ImGuiFullscreen::OpenFileSelector(
                    "BIOS Override",
                    false,
                    [this](const std::string& path) {
                        settings_.bios_override = path;
                        SaveSettings(settings_);
                    },
                    {".bin", ".rom"},
                    browseDirectoryForPath(browse_seed, false).string()
                );
            };
            rows.push_back(std::move(override));

            fsui::SettingsRowDescriptor clear_override;
            clear_override.kind = fsui::SettingsRowKind::Action;
            clear_override.id = "clear-bios-override";
            clear_override.title = ICON_FA_TIMES " Clear BIOS Override";
            clear_override.summary = "Fall back to the BIOS folder + preferred model.";
            clear_override.enabled = !settings_.bios_override.empty();
            clear_override.on_activate = [this]() {
                settings_.bios_override.clear();
                SaveSettings(settings_);
            };
            rows.push_back(std::move(clear_override));

            return rows;
        };
        pages.push_back(std::move(bios_page));

        fsui::SettingsPageDescriptor emulation_page;
        emulation_page.id = "Emulation";
        emulation_page.scope = fsui::SettingsScope::Global;
        emulation_page.built_in_page = fsui::BuiltInSettingsPage::Emulation;
        emulation_page.build_rows = [this]() {
            std::vector<fsui::SettingsRowDescriptor> rows;

            fsui::SettingsRowDescriptor region;
            region.kind = fsui::SettingsRowKind::Choice;
            region.id = "region";
            region.title = ICON_FA_GLOBE " Region";
            region.summary = "Select the firmware region policy used for disc boot.";
            region.value = ToLower(settings_.region);
            region.dialog_title = region.title;
            region.choices = BuildRegionChoices(settings_.region);
            region.on_choice = [this](int index) {
                static const std::array<const char*, 3> values = {"auto", "ntsc", "pal"};
                if (index >= 0 && index < static_cast<int>(values.size())) {
                    settings_.region = values[static_cast<size_t>(index)];
                    SaveSettings(settings_);
                }
            };
            rows.push_back(std::move(region));

            fsui::SettingsRowDescriptor expansion;
            expansion.kind = fsui::SettingsRowKind::Action;
            expansion.id = "expansion-rom";
            expansion.title = ICON_FA_MEMORY " Expansion ROM";
            expansion.summary = settings_.exp_path.empty() ? "No expansion ROM selected." : settings_.exp_path;
            expansion.on_activate = [this]() {
                ImGuiFullscreen::OpenFileSelector(
                    "Expansion ROM",
                    false,
                    [this](const std::string& path) {
                        settings_.exp_path = path;
                        SaveSettings(settings_);
                    },
                    {".bin", ".rom"},
                    initialBrowseDirectory().string()
                );
            };
            rows.push_back(std::move(expansion));

            fsui::SettingsRowDescriptor default_exe;
            default_exe.kind = fsui::SettingsRowKind::Action;
            default_exe.id = "default-exe";
            default_exe.title = ICON_FA_FILE_CODE " Default PS-X EXE";
            default_exe.summary = settings_.default_exe_path.empty() ? "No default executable selected." : settings_.default_exe_path;
            default_exe.on_activate = [this]() {
                ImGuiFullscreen::OpenFileSelector(
                    "Default PS-X EXE",
                    false,
                    [this](const std::string& path) {
                        settings_.default_exe_path = path;
                        SaveSettings(settings_);
                    },
                    {".exe", ".ps-exe", ".psexe"},
                    initialBrowseDirectory().string()
                );
            };
            rows.push_back(std::move(default_exe));

            return rows;
        };
        pages.push_back(std::move(emulation_page));

        fsui::SettingsPageDescriptor graphics_page;
        graphics_page.id = "Graphics";
        graphics_page.scope = fsui::SettingsScope::Global;
        graphics_page.built_in_page = fsui::BuiltInSettingsPage::Graphics;
        graphics_page.build_rows = [this]() { return buildGraphicsRows(SupportsManagedWindowSizing()); };
        pages.push_back(std::move(graphics_page));

        fsui::SettingsPageDescriptor folders_page;
        folders_page.id = "Folders";
        folders_page.scope = fsui::SettingsScope::Global;
        folders_page.built_in_page = fsui::BuiltInSettingsPage::Folders;
        folders_page.build_rows = [this]() {
            std::vector<fsui::SettingsRowDescriptor> rows;

            fsui::SettingsRowDescriptor add_folder;
            add_folder.kind = fsui::SettingsRowKind::Action;
            add_folder.id = "add-folder";
            add_folder.title = ICON_FA_FOLDER_PLUS " Add Library Folder";
            add_folder.summary = "Scan only the selected directory.";
            add_folder.on_activate = [this]() {
                ImGuiFullscreen::OpenFileSelector(
                    "Add Library Folder",
                    true,
                    [this](const std::string& path) {
                        settings_.ui_state.game_list_paths.emplace_back(path);
                        refreshGameList(true);
                        SaveSettings(settings_);
                    },
                    {},
                    initialBrowseDirectory().string()
                );
            };
            rows.push_back(std::move(add_folder));

            fsui::SettingsRowDescriptor add_recursive;
            add_recursive.kind = fsui::SettingsRowKind::Action;
            add_recursive.id = "add-recursive-folder";
            add_recursive.title = ICON_FA_FOLDER_PLUS " Add Recursive Folder";
            add_recursive.summary = "Scan the selected directory and its children.";
            add_recursive.on_activate = [this]() {
                ImGuiFullscreen::OpenFileSelector(
                    "Add Recursive Library Folder",
                    true,
                    [this](const std::string& path) {
                        settings_.ui_state.game_list_recursive_paths.emplace_back(path);
                        refreshGameList(true);
                        SaveSettings(settings_);
                    },
                    {},
                    initialBrowseDirectory().string()
                );
            };
            rows.push_back(std::move(add_recursive));

            fsui::SettingsRowDescriptor refresh;
            refresh.kind = fsui::SettingsRowKind::Action;
            refresh.id = "refresh-library";
            refresh.title = ICON_FA_SYNC " Refresh Library";
            refresh.summary = "Rescan every configured library folder.";
            refresh.on_activate = [this]() { refreshGameList(true); };
            rows.push_back(std::move(refresh));

            auto append_folder_rows = [&](const std::vector<std::filesystem::path>& folders, bool recursive) {
                for (size_t index = 0; index < folders.size(); index++) {
                    fsui::SettingsRowDescriptor row;
                    row.kind = fsui::SettingsRowKind::Action;
                    row.id = std::string(recursive ? "recursive-" : "folder-") + std::to_string(index);
                    row.title = recursive ? ICON_FA_FOLDER_OPEN " Recursive Folder" : ICON_FA_FOLDER " Library Folder";
                    row.summary = folders[index].string();
                    row.on_activate = [this, index, recursive]() {
                        auto& list = recursive ? settings_.ui_state.game_list_recursive_paths : settings_.ui_state.game_list_paths;
                        if (index < list.size()) {
                            list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
                            refreshGameList(true);
                            SaveSettings(settings_);
                        }
                    };
                    rows.push_back(std::move(row));
                }
            };

            append_folder_rows(settings_.ui_state.game_list_paths, false);
            append_folder_rows(settings_.ui_state.game_list_recursive_paths, true);

            return rows;
        };
        pages.push_back(std::move(folders_page));

        fsui::SettingsPageDescriptor advanced_page;
        advanced_page.id = "Advanced";
        advanced_page.scope = fsui::SettingsScope::Global;
        advanced_page.built_in_page = fsui::BuiltInSettingsPage::Advanced;
        advanced_page.build_rows = [this]() {
            std::vector<fsui::SettingsRowDescriptor> rows;

            fsui::SettingsRowDescriptor log_level;
            log_level.kind = fsui::SettingsRowKind::Choice;
            log_level.id = "log-level";
            log_level.title = ICON_FA_TERMINAL " Log Level";
            log_level.summary = "Adjust ARMSX logging verbosity.";
            log_level.value = log_level_string(settings_.log_level);
            log_level.dialog_title = log_level.title;
            log_level.choices = BuildLogLevelChoices(settings_.log_level);
            log_level.on_choice = [this](int index) {
                settings_.log_level = std::clamp(index, static_cast<int>(LOG_TRACE), static_cast<int>(LOG_FATAL));
                log_set_level(settings_.log_level);
                SaveSettings(settings_);
            };
            rows.push_back(std::move(log_level));

            fsui::SettingsRowDescriptor quiet;
            quiet.kind = fsui::SettingsRowKind::Toggle;
            quiet.id = "quiet";
            quiet.title = ICON_FA_VOLUME_MUTE " Quiet Logs";
            quiet.summary = "Silence all emulator logs.";
            quiet.toggle_value = settings_.quiet;
            quiet.on_toggle = [this](bool value) {
                settings_.quiet = value;
                log_set_quiet(value ? 1 : 0);
                SaveSettings(settings_);
            };
            rows.push_back(std::move(quiet));

            fsui::SettingsRowDescriptor settings_path;
            settings_path.kind = fsui::SettingsRowKind::Notice;
            settings_path.id = "settings-path";
            settings_path.title = ICON_FA_FILE_ALT " Settings File";
            settings_path.summary = settings_.settings_path;
            rows.push_back(std::move(settings_path));

            return rows;
        };
        pages.push_back(std::move(advanced_page));

        return pages;
    }

    std::vector<fsui::SettingsPageDescriptor> buildPerGameSettingsPages() {
        std::vector<fsui::SettingsPageDescriptor> pages;

        fsui::SettingsPageDescriptor summary;
        summary.id = "Summary";
        summary.scope = fsui::SettingsScope::PerGame;
        summary.built_in_page = fsui::BuiltInSettingsPage::Summary;
        summary.build_rows = [this]() {
            std::vector<fsui::SettingsRowDescriptor> rows;
            const fsui::CurrentGameInfo info = session_.currentGameInfo();

            fsui::SettingsRowDescriptor title;
            title.kind = fsui::SettingsRowKind::Notice;
            title.id = "current-game";
            title.title = info.title;
            title.summary = info.path.empty() ? info.subtitle : info.path.string();
            rows.push_back(std::move(title));

            return rows;
        };
        pages.push_back(std::move(summary));

        fsui::SettingsPageDescriptor graphics;
        graphics.id = "Graphics";
        graphics.scope = fsui::SettingsScope::PerGame;
        graphics.built_in_page = fsui::BuiltInSettingsPage::Graphics;
        graphics.build_rows = [this]() { return buildGraphicsRows(false); };
        pages.push_back(std::move(graphics));

        fsui::SettingsPageDescriptor emulation;
        emulation.id = "Emulation";
        emulation.scope = fsui::SettingsScope::PerGame;
        emulation.built_in_page = fsui::BuiltInSettingsPage::Emulation;
        emulation.build_rows = [this]() {
            std::vector<fsui::SettingsRowDescriptor> rows;

            fsui::SettingsRowDescriptor reset;
            reset.kind = fsui::SettingsRowKind::Action;
            reset.id = "reset-now";
            reset.title = ICON_FA_SYNC " Reset";
            reset.summary = "Soft-reset the active session.";
            reset.on_activate = [this]() { deferred_reset_ = true; };
            rows.push_back(std::move(reset));

            fsui::SettingsRowDescriptor disc;
            disc.kind = fsui::SettingsRowKind::Action;
            disc.id = "change-disc-now";
            disc.title = ICON_FA_COMPACT_DISC " Change Disc";
            disc.summary = "Swap to a different disc image.";
            disc.on_activate = [this]() {
                ImGuiFullscreen::OpenFileSelector(
                    "Change Disc",
                    false,
                    [this](const std::string& path) { deferred_change_disc_ = std::filesystem::path(path); },
                    {".cue", ".bin", ".iso", ".img"},
                    initialBrowseDirectory().string()
                );
            };
            rows.push_back(std::move(disc));

            fsui::SettingsRowDescriptor screenshot;
            screenshot.kind = fsui::SettingsRowKind::Action;
            screenshot.id = "save-screenshot-now";
            screenshot.title = ICON_FA_CAMERA " Save Screenshot";
            screenshot.summary = "Write a BMP screenshot to the snapshot folder.";
            screenshot.on_activate = [this]() { deferred_screenshot_ = true; };
            rows.push_back(std::move(screenshot));

            fsui::SettingsRowDescriptor exit_library;
            exit_library.kind = fsui::SettingsRowKind::Action;
            exit_library.id = "exit-library-now";
            exit_library.title = ICON_FA_FOLDER_OPEN " Exit To Library";
            exit_library.summary = "Destroy the session and return to the landing screen.";
            exit_library.on_activate = [this]() { deferred_exit_to_library_ = true; };
            rows.push_back(std::move(exit_library));

            return rows;
        };
        pages.push_back(std::move(emulation));

        return pages;
    }

    std::vector<fsui::SettingsRowDescriptor> buildGraphicsRows(bool include_scale) {
        std::vector<fsui::SettingsRowDescriptor> rows;

        if (include_scale) {
            fsui::SettingsRowDescriptor scale;
            scale.kind = fsui::SettingsRowKind::Choice;
            scale.id = "display-scale";
            scale.title = ICON_FA_EXPAND " Display Scale";
            scale.summary = "Resize the managed desktop window.";
            scale.value = std::to_string(settings_.scale) + "x";
            scale.dialog_title = scale.title;
            scale.choices = BuildScaleChoices(settings_.scale);
            scale.on_choice = [this](int index) {
                static const std::array<int, 6> values = {1, 2, 3, 4, 5, 6};
                if (index >= 0 && index < static_cast<int>(values.size())) {
                    settings_.scale = values[static_cast<size_t>(index)];
                    applyWindowMetrics();
                    SaveSettings(settings_);
                }
            };
            rows.push_back(std::move(scale));
        }

        fsui::SettingsRowDescriptor filter;
        filter.kind = fsui::SettingsRowKind::Toggle;
        filter.id = "texture-filter";
        filter.title = ICON_FA_ADJUST " Texture Scaling / Bilinear";
        filter.summary = "Use linear filtering for the SDL presentation texture.";
        filter.toggle_value = settings_.texture_scale_mode;
        filter.on_toggle = [this](bool value) {
            settings_.texture_scale_mode = value;
            SaveSettings(settings_);
        };
        rows.push_back(std::move(filter));

        fsui::SettingsRowDescriptor stretch;
        stretch.kind = fsui::SettingsRowKind::Toggle;
        stretch.id = "stretch";
        stretch.title = ICON_FA_EXPAND_ARROWS_ALT " Stretch";
        stretch.summary = "Fill the SDL window instead of preserving the content aspect ratio.";
        stretch.toggle_value = settings_.stretch_mode;
        stretch.on_toggle = [this](bool value) {
            settings_.stretch_mode = value;
            SaveSettings(settings_);
        };
        rows.push_back(std::move(stretch));

        fsui::SettingsRowDescriptor aspect;
        aspect.kind = fsui::SettingsRowKind::Choice;
        aspect.id = "aspect";
        aspect.title = ICON_FA_TV " Display Aspect";
        aspect.summary = "Choose the presentation aspect ratio.";
        aspect.value = AspectTitle(settings_.display_aspect);
        aspect.dialog_title = aspect.title;
        aspect.choices = BuildAspectChoices(settings_.display_aspect);
        aspect.on_choice = [this](int index) {
            settings_.display_aspect = std::clamp(index, 0, 2);
            applyWindowMetrics();
            SaveSettings(settings_);
        };
        rows.push_back(std::move(aspect));

        fsui::SettingsRowDescriptor upscale;
        upscale.kind = fsui::SettingsRowKind::Choice;
        upscale.id = "wide-upscale";
        upscale.title = ICON_FA_EXPAND " Wide Upscale";
        upscale.summary = "Choose the wide-mode output height.";
        upscale.value = UpscaleToString(settings_.upscale_height);
        upscale.dialog_title = upscale.title;
        upscale.choices = BuildUpscaleChoices(settings_.upscale_height);
        upscale.on_choice = [this](int index) {
            static const std::array<int, 5> heights = {480, 720, 1080, 1440, 2160};
            if (index >= 0 && index < static_cast<int>(heights.size())) {
                settings_.upscale_height = heights[static_cast<size_t>(index)];
                applyWindowMetrics();
                SaveSettings(settings_);
            }
        };
        rows.push_back(std::move(upscale));

        fsui::SettingsRowDescriptor debug_panel;
        debug_panel.kind = fsui::SettingsRowKind::Toggle;
        debug_panel.id = "debug-panel";
        debug_panel.title = ICON_FA_TACHOMETER_ALT " Debug Panel";
        debug_panel.summary = "Expose the FSUI runtime settings/performance overlays.";
        debug_panel.toggle_value = settings_.debug_panel;
        debug_panel.on_toggle = [this](bool value) {
            settings_.debug_panel = value;
            settings_.ui_state.show_settings_overlay = value;
            settings_.ui_state.show_performance_overlay = value;
            SaveSettings(settings_);
        };
        rows.push_back(std::move(debug_panel));

        if (session_.valid()) {
            fsui::SettingsRowDescriptor debug_view;
            debug_view.kind = fsui::SettingsRowKind::Toggle;
            debug_view.id = "debug-view";
            debug_view.title = ICON_FA_BUG " VRAM Debug View";
            debug_view.summary = "Render the raw PSX VRAM buffer instead of the display output.";
            debug_view.toggle_value = session_.debugView();
            debug_view.on_toggle = [this](bool value) {
                session_.setDebugView(value);
                applyWindowMetrics();
            };
            rows.push_back(std::move(debug_view));
        }

        return rows;
    }

    void applyWindowMetrics() {
        if (!SupportsManagedWindowSizing() || !owns_window_ || !window_) {
            return;
        }

        int width = 1280;
        int height = 720;

        if (session_.valid()) {
            if (session_.debugView()) {
                width = PSX_GPU_FB_WIDTH;
                height = PSX_GPU_FB_HEIGHT;
            } else if (settings_.display_aspect == 2) {
                height = std::max(240, settings_.upscale_height);
                width = static_cast<int>((16.0f / 9.0f) * static_cast<float>(height));
            } else if (settings_.display_aspect == 1) {
                width = 320 * std::max(1, settings_.scale);
                height = width;
            } else {
                int base_width = 320;
                if (session_.psx()) {
                    const int display_width = static_cast<int>(psx_get_dmode_width(session_.psx()));
                    if (display_width == 256 || display_width == 320) {
                        base_width = display_width;
                    } else if (display_width == 368) {
                        base_width = 384;
                    }
                }

                width = base_width * std::max(1, settings_.scale);
                height = 240 * std::max(1, settings_.scale);
            }
        }
        SDL_SetWindowSize(window_, width, height);
    }

    std::filesystem::path initialBrowseDirectory() const {
        if (session_.valid()) {
            const fsui::CurrentGameInfo info = session_.currentGameInfo();
            if (!info.path.empty() && info.path.has_parent_path()) {
                return info.path.parent_path();
            }
        }

        if (!settings_.ui_state.game_list_paths.empty()) {
            return settings_.ui_state.game_list_paths.front();
        }

        if (!settings_.ui_state.game_list_recursive_paths.empty()) {
            return settings_.ui_state.game_list_recursive_paths.front();
        }

        return DefaultBrowseDirectory();
    }

    std::filesystem::path browseDirectoryForPath(const std::string& configured_path, bool expect_directory) const {
        std::error_code ec;

        if (!configured_path.empty()) {
            const std::filesystem::path candidate(configured_path);

            if (std::filesystem::exists(candidate, ec)) {
                if (expect_directory && std::filesystem::is_directory(candidate, ec)) {
                    return candidate;
                }

                if (!expect_directory) {
                    if (std::filesystem::is_directory(candidate, ec)) {
                        return candidate;
                    }

                    if (candidate.has_parent_path() && std::filesystem::exists(candidate.parent_path(), ec)) {
                        return candidate.parent_path();
                    }
                }
            }

            if (candidate.has_parent_path() && std::filesystem::exists(candidate.parent_path(), ec)) {
                return candidate.parent_path();
            }
        }

        return initialBrowseDirectory();
    }

    int argc_ = 0;
    const char* const* argv_ = nullptr;
    SDL_Window* external_window_ = nullptr;
    SDL_Renderer* external_renderer_ = nullptr;
    CliFlags cli_{};
    FrontendSettings settings_{};
    bool running_ = true;
    bool owns_sdl_ = false;
    bool owns_window_ = false;
    bool owns_renderer_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    fsui::SdlImGuiBackend imgui_backend_{};
    ArmsxSession session_{};
    GameplayInputRouter input_router_{};
    std::vector<fsui::GameEntry> game_list_{};
    std::optional<std::string> pending_error_dialog_{};
    std::optional<LaunchRequest> deferred_launch_{};
    std::optional<std::filesystem::path> deferred_change_disc_{};
    std::string pending_cli_path_;
    bool close_ui_after_launch_ = false;
    bool deferred_exit_to_library_ = false;
    bool deferred_reset_ = false;
    bool deferred_screenshot_ = false;
    bool fsui_initialized_ = false;
};

} // namespace

extern "C" int psxe_run(int argc, const char* argv[], void* external_window, void* external_renderer) {
    ArmsxApp app(argc, argv, external_window, external_renderer);
    return app.run();
}

extern "C" PSXE_API void psxe_wasm_on_file(const char* path) {
    if (!path || !*path) {
        return;
    }

    if (g_active_app) {
        g_active_app->onWasmFile(path);
    } else {
        g_pending_wasm_path = std::filesystem::path(path);
    }
}

extern "C" PSXE_API int external_main(int argc, const char* argv[], void* external_window, void* external_renderer) {
    return psxe_run(argc, argv, external_window, external_renderer);
}

#ifndef __DLL_BUILD
int main(int argc, const char* argv[]) {
    return external_main(argc, argv, nullptr, nullptr);
}
#endif
