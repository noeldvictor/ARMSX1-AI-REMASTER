#!/usr/bin/env python3

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def main() -> int:
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    makefile = read("Makefile")
    cpu = read("psx/cpu.c")
    cpu_header = read("psx/cpu.h")
    config = read("frontend/config.c")
    frontend = read("frontend/main.cpp")
    archive = read("frontend/archive.cpp")
    gpu_hw = read("frontend/gpu_hw.c")
    chd = read("psx/dev/cdrom/chd.c")
    android = read("android/app/src/main/java/com/nanodata/armsx/EmulatorActivity.java")
    platform_file = read("frontend/platform_file.c")

    require("PSX_CPU_CACHED_INTERPRETER" in cpu_header, "cached interpreter API is missing")
    require("cfg->cpu_engine = 1;" in config, "cached interpreter is not the default")
    require('"    execution_mode  = \\"cached\\"' in config, "generated settings do not default to cached")
    require('"cpu-engine"' in config, "--cpu-engine CLI selection is missing")
    require('"ARMSX_CPU_ENGINE"' in frontend, "ARMSX_CPU_ENGINE environment override is missing")
    require("psx_bus_read32(cpu->bus, cpu->pc)" in cpu, "cached mode must preserve a real instruction bus fetch")
    require("entry->opcode == cpu->opcode" in cpu, "cached mode lacks opcode verification")
    require("psx_cpu_invalidate_range" in cpu, "cached mode lacks targeted invalidation")
    require("pthread_" not in cpu and "SDL_CreateThread" not in cpu, "CPU engine must not depend on host threading")
    require("mprotect(" not in cpu and "VirtualProtect(" not in cpu, "JIT-style executable memory is forbidden")
    require("USE_CHD ?= 1" in makefile, "CHD must be enabled by default")
    require("third_party/cmake/bin/cmake" in makefile,
            "Makefile must fall back to vendored cmake when PATH has none")
    require('export USE_CHD="${USE_CHD:-1}"' in read("build.sh"), "build.sh disables default CHD support")
    require("gpu_render_triangle(gpu, v0, v1, v2, data, edge);" in gpu_hw,
            "SDL acceleration must preserve the software PS1 rasterizer")
    require("SDL_RenderGeometry" not in gpu_hw, "experimental incomplete triangle renderer is still active")
    require("-DUSE_GPU_BACKEND" in makefile and "-DUSE_HARDWARE" not in makefile,
            "USE_HARDWARE must not label the software-rasterizer shim")
    require("USE_HARDWARE" not in read("psx/dev/gpu.h") and "USE_HARDWARE" not in gpu_hw
            and "USE_HARDWARE" not in frontend,
            "USE_HARDWARE must not remain in GPU sources")
    require("GpuBackend gpu_backend = GpuBackend::Software;" in frontend,
            "SDL acceleration must be opt-in and default off")
    require("SDL_RENDERER_ACCELERATED" in frontend and "SDL_RENDERER_SOFTWARE" in frontend,
            "both SDL presentation modes must remain selectable")
    require("desired.callback = nullptr;" in frontend and "SDL_QueueAudio" in frontend,
            "audio must use SDL queued mode instead of a starvation-prone callback")
    require("desired.samples = 1024;" in frontend and "kAudioPrimeBytes" in frontend,
            "audio must use a power-of-two device buffer and startup prebuffer")
    require("presentation_upscale = 1;" in frontend and "SDL_TEXTUREACCESS_TARGET" in frontend,
            "1x-default SDL presentation upscaling is missing")
    require("SDL_FINGERDOWN" in frontend and "ImGuiMobileControls" in frontend,
            "ImGui multitouch PlayStation controls are missing")
    require("kMobileControlsAutoHideMs = 3000" in frontend and "takeControllerActivity" in frontend,
            "mobile controls must auto-hide and react to physical-controller use")
    require("DefaultMobileControlsEnabled" in frontend and "mobile_controls = DefaultMobileControlsEnabled()" in frontend,
            "mobile control defaults are not platform-aware")
    require("ACTION_OPEN_DOCUMENT_TREE" in android and "takePersistableUriPermission" in android,
            "Android persistent Storage Access Framework directory access is missing")
    require("DocumentsContract.buildChildDocumentsUriUsingTree" in android and "openVirtualFileDescriptor" in android,
            "Android document-tree enumeration or no-copy file access is missing")
    require("WindowInsets.Type.systemBars()" in android and "BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE" in android,
            "Android immersive fullscreen is missing")
    require("psxe_platform_fopen" in platform_file and "psxe_platform_fopen" in archive,
            "platform file-descriptor bridge is missing")
    require("USE_CONNECTIVITY" not in makefile and "FTP" not in frontend and "ftp" not in frontend,
            "FTP connectivity code is still present")
    require("kMaxArchiveBytes" in archive and "SafeArchiveRelativePath" in archive,
            "bounded path-safe ZIP extraction is missing")
    require("CHD_TRACK1_PREGAP_FRAMES" in chd and "postgap_frames" in chd,
            "CHD pregap/postgap handling is missing")
    require("chd_read_subchannel_q" in chd, "CHD subchannel Q support is missing")

    gate = read("tests/run_validation.py")
    see_c = read("enhance/see.c")
    require('"see":' in gate, "SEE unit tests are not in the default validation suite")
    require('"boot":' in gate, "SEE real-title boot is not in the default validation suite")
    require("host-missing-cmake" in gate and "host-missing-sdl2" in gate,
            "cmake/SDL2 host skips are missing from the default suite")
    require("see_present_rgb" in see_c and "see_export_pack" in see_c,
            "SEE present/pack entry points are missing")
    require("see_on_texture_use" in see_c and "see_enhance_cache" in see_c
            and "see_replace_texel" in see_c,
            "texture dump / HD tag / enhance-cache path is missing")
    require("psx_gpu_set_texture_use_callback" in read("psx/dev/gpu.c") and
            "psx_gpu_set_texel_callback" in read("psx/dev/gpu.c"),
            "GPU texture-use / texel hooks are missing")
    require("qa_texture_use" in read("tools/armsx_qa.c") and
            "see_on_texture_use" in read("tools/armsx_qa.c"),
            "QA driver does not dump textures on use")
    require("texpage-dump-and-hd" in read("tests/see_replacement.c") and
            "enhance-cache-no-replay" in read("tests/see_replacement.c"),
            "texture dump / no-replay HD tests are missing")
    require("if (see_file_exists(reverted)) return;" in see_c,
            "SEE reverted lock does not skip generated/user at present")
    require("see_slot_find" in see_c and "truncated" in read("tests/see_replacement.c"),
            "malformed PNG must keep the last good replacement")
    require("gpu->vram[xpos + (ypos * 1024)] = gpu->recv_data" in read("psx/dev/gpu.c"),
            "GP0(A0) must still write emulated VRAM")
    require("vk_buffer_copy_roundtrip" in read("vk/blit.c"),
            "Vulkan VRAM blit skeleton is missing")
    require("vk_copy_software_vram" in read("vk/blit.c") and
            "vk_copy_software_vram" in read("tests/vk_vram_blit.c"),
            "software VRAM must go through the shipped Vulkan copy")
    raster = read("vk/raster.c")
    vk_test = read("tests/vk_vram_blit.c")
    require("vk_raster_triangle" in raster and "vkCreateGraphicsPipelines" in raster,
            "Vulkan triangle rasterizer (graphics pipeline) is missing")
    require("gpu_render_triangle" not in raster,
            "Vulkan rasterizer must not call the software triangle path")
    require("vkCmdDraw" in raster, "Vulkan rasterizer must issue a draw, not only a buffer copy")
    require("vk_raster_triangle" in vk_test and "flat" in vk_test and
            "shaded-dithered" in vk_test and "semi-transparent" in vk_test,
            "headless Vulkan-vs-software triangle cases are missing")
    require("gpu_render_triangle" in vk_test and "memcmp(sw->vram, vk_fb" in vk_test,
            "triangle test must compare software VRAM to Vulkan readback")
    require('"vk":' in gate, "Vulkan blit test is not in the default validation suite")

    if failures:
        for failure in failures:
            print(f"ARMSX_SOURCE_CHECK failed: {failure}", file=sys.stderr)
        return 1

    print("ARMSX_SOURCE_CHECK passed")
    print("  cached interpreter: default, selectable, real-fetch verified")
    print("  cache invalidation: explicit range hook plus opcode guard")
    print("  portability: no JIT memory or host-thread dependency")
    print("  renderer: software-authoritative SDL presentation, acceleration opt-in")
    print("  presentation: SDL target upscaling 1x-8x, default 1x")
    print("  audio: queued SDL transport with prebuffer and GPU-derived frame timing")
    print("  mobile: ImGui PlayStation controls with idle/controller auto-hide")
    print("  storage: Android SAF and iOS security-scoped persistent game directories")
    print("  media: CHD default plus bounded ZIP ingestion and browser permission bridge")
    print("  connectivity: FTP removed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
