#include "gpu_hw.h"

#ifdef USE_HARDWARE

#include "diagnostics.h"

#include <stdlib.h>
#include <string.h>

static int g_hw_trace_mode = -1;

static bool hw_trace_enabled(void) {
    if (g_hw_trace_mode < 0) {
        const char* env = getenv("ARMSX_HW_TRACE");
        g_hw_trace_mode = (env && env[0] && env[0] != '0') ? 1 : 0;
    }

    return g_hw_trace_mode != 0;
}

static void hw_tracef(const char* fmt, ...) {
    if (!hw_trace_enabled()) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    psxe_diag_vlogf("hw", fmt, args);
    va_end(args);
}

static const char* hw_bool(bool value) {
    return value ? "true" : "false";
}

static void hw_describe_renderer_info(const SDL_Renderer* renderer) {
    SDL_RendererInfo info;

    if (SDL_GetRendererInfo((SDL_Renderer*)renderer, &info) != 0) {
        hw_tracef("renderer-info unavailable error=%s", SDL_GetError());
        return;
    }

    hw_tracef(
        "renderer-info name=%s flags=0x%x software=%s accelerated=%s present-vsync=%s target-texture=%s max_texture=%dx%d texture_formats=%u",
        info.name ? info.name : "(unknown)",
        info.flags,
        hw_bool((info.flags & SDL_RENDERER_SOFTWARE) != 0),
        hw_bool((info.flags & SDL_RENDERER_ACCELERATED) != 0),
        hw_bool((info.flags & SDL_RENDERER_PRESENTVSYNC) != 0),
        hw_bool((info.flags & SDL_RENDERER_TARGETTEXTURE) != 0),
        info.max_texture_width,
        info.max_texture_height,
        info.num_texture_formats
    );

    for (Uint32 index = 0; index < info.num_texture_formats; index++) {
        hw_tracef("renderer-format[%u]=%s (0x%08x)", index, SDL_GetPixelFormatName(info.texture_formats[index]), info.texture_formats[index]);
    }
}

struct armsx_hw_renderer {
    SDL_Renderer* renderer;
    SDL_Texture* page_texture;
    SDL_Texture* overlay_texture;
    SDL_Rect saved_viewport;
    SDL_Rect saved_clip_rect;
    int saved_logical_width;
    int saved_logical_height;
    float saved_scale_x;
    float saved_scale_y;
    SDL_bool saved_clip_enabled;
    SDL_bool saved_integer_scale;
    SDL_bool saved_state_valid;
    int width;
    int height;
    uint64_t frame_index;
    uint64_t triangle_index;
    uint64_t solid_triangle_count;
    uint64_t textured_fallback_count;
    uint64_t transparent_fallback_count;
    uint64_t geometry_draw_count;
    uint64_t geometry_fail_count;
    int trace_enabled;
    int fallback_warned;
};

static void hw_log_renderer_state(armsx_hw_renderer_t* hw, const char* stage) {
    if (!hw || !hw->renderer || !hw_trace_enabled()) {
        return;
    }

    SDL_Rect viewport = {0, 0, 0, 0};
    SDL_Rect clip_rect = {0, 0, 0, 0};
    int logical_w = 0;
    int logical_h = 0;
    float scale_x = 0.0f;
    float scale_y = 0.0f;
    int output_w = 0;
    int output_h = 0;
    SDL_Texture* target = SDL_GetRenderTarget(hw->renderer);

    SDL_GetRendererOutputSize(hw->renderer, &output_w, &output_h);
    SDL_RenderGetViewport(hw->renderer, &viewport);
    SDL_RenderGetClipRect(hw->renderer, &clip_rect);
    SDL_RenderGetLogicalSize(hw->renderer, &logical_w, &logical_h);
    SDL_RenderGetScale(hw->renderer, &scale_x, &scale_y);

    hw_tracef(
        "%s renderer=%p target=%p output=%dx%d viewport=%d,%d %dx%d clip=%d,%d %dx%d scale=(%f,%f) logical=%dx%d clip_enabled=%s integer_scale=%s overlay=%p page=%p size=%dx%d",
        stage ? stage : "(state)",
        (void*)hw->renderer,
        (void*)target,
        output_w,
        output_h,
        viewport.x,
        viewport.y,
        viewport.w,
        viewport.h,
        clip_rect.x,
        clip_rect.y,
        clip_rect.w,
        clip_rect.h,
        scale_x,
        scale_y,
        logical_w,
        logical_h,
        SDL_RenderIsClipEnabled(hw->renderer) == SDL_TRUE ? "true" : "false",
        SDL_RenderGetIntegerScale(hw->renderer) == SDL_TRUE ? "true" : "false",
        (void*)hw->overlay_texture,
        (void*)hw->page_texture,
        hw->width,
        hw->height
    );
}

static void destroy_overlay_texture(armsx_hw_renderer_t* hw) {
    if (!hw) {
        return;
    }

    if (hw->page_texture) {
        SDL_DestroyTexture(hw->page_texture);
        hw->page_texture = NULL;
    }

    if (hw->overlay_texture) {
        SDL_DestroyTexture(hw->overlay_texture);
        hw->overlay_texture = NULL;
    }

    hw->width = 0;
    hw->height = 0;
}

static void save_renderer_state(armsx_hw_renderer_t* hw) {
    if (!hw || !hw->renderer) {
        return;
    }

    SDL_RenderGetViewport(hw->renderer, &hw->saved_viewport);
    SDL_RenderGetClipRect(hw->renderer, &hw->saved_clip_rect);
    SDL_RenderGetLogicalSize(hw->renderer, &hw->saved_logical_width, &hw->saved_logical_height);
    SDL_RenderGetScale(hw->renderer, &hw->saved_scale_x, &hw->saved_scale_y);
    hw->saved_clip_enabled = SDL_RenderIsClipEnabled(hw->renderer);
    hw->saved_integer_scale = SDL_RenderGetIntegerScale(hw->renderer);
    hw->saved_state_valid = SDL_TRUE;

    hw_tracef(
        "save-state renderer=%p viewport=%d,%d %dx%d clip=%d,%d %dx%d scale=(%f,%f) logical=%dx%d clip_enabled=%s integer_scale=%s",
        (void*)hw->renderer,
        hw->saved_viewport.x,
        hw->saved_viewport.y,
        hw->saved_viewport.w,
        hw->saved_viewport.h,
        hw->saved_clip_rect.x,
        hw->saved_clip_rect.y,
        hw->saved_clip_rect.w,
        hw->saved_clip_rect.h,
        hw->saved_scale_x,
        hw->saved_scale_y,
        hw->saved_logical_width,
        hw->saved_logical_height,
        hw->saved_clip_enabled == SDL_TRUE ? "true" : "false",
        hw->saved_integer_scale == SDL_TRUE ? "true" : "false"
    );
}

static void restore_renderer_state(armsx_hw_renderer_t* hw) {
    if (!hw || !hw->renderer || !hw->saved_state_valid) {
        return;
    }

    SDL_RenderSetLogicalSize(hw->renderer, hw->saved_logical_width, hw->saved_logical_height);
    SDL_RenderSetViewport(hw->renderer, &hw->saved_viewport);
    SDL_RenderSetScale(hw->renderer, hw->saved_scale_x, hw->saved_scale_y);
    SDL_RenderSetIntegerScale(hw->renderer, hw->saved_integer_scale);

    if (hw->saved_clip_enabled == SDL_TRUE) {
        SDL_RenderSetClipRect(hw->renderer, &hw->saved_clip_rect);
    } else {
        SDL_RenderSetClipRect(hw->renderer, NULL);
    }

    hw_tracef(
        "restore-state renderer=%p viewport=%d,%d %dx%d clip=%d,%d %dx%d scale=(%f,%f) logical=%dx%d clip_enabled=%s integer_scale=%s",
        (void*)hw->renderer,
        hw->saved_viewport.x,
        hw->saved_viewport.y,
        hw->saved_viewport.w,
        hw->saved_viewport.h,
        hw->saved_clip_rect.x,
        hw->saved_clip_rect.y,
        hw->saved_clip_rect.w,
        hw->saved_clip_rect.h,
        hw->saved_scale_x,
        hw->saved_scale_y,
        hw->saved_logical_width,
        hw->saved_logical_height,
        hw->saved_clip_enabled == SDL_TRUE ? "true" : "false",
        hw->saved_integer_scale == SDL_TRUE ? "true" : "false"
    );
}

static bool ensure_overlay_texture(armsx_hw_renderer_t* hw) {
    if (!hw || !hw->renderer) {
        return false;
    }

    if (hw->page_texture) {
        return true;
    }

    if (hw->trace_enabled) {
        hw_describe_renderer_info(hw->renderer);
        hw_tracef("creating page texture size=1024x512 format=%s", SDL_GetPixelFormatName(SDL_PIXELFORMAT_BGR555));
    }

    hw->page_texture = SDL_CreateTexture(
        hw->renderer,
        SDL_PIXELFORMAT_BGR555,
        SDL_TEXTUREACCESS_STREAMING,
        1024,
        512
    );

    if (!hw->page_texture) {
        psxe_diag_logf("hw", "triangle page texture unavailable, continuing without it: %s", SDL_GetError());
    } else {
        SDL_SetTextureBlendMode(hw->page_texture, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(hw->page_texture, SDL_ScaleModeNearest);
    }

    if (hw->overlay_texture && hw->width > 0 && hw->height > 0) {
        return true;
    }

    return true;
}

bool armsx_hw_renderer_is_supported(void) {
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || defined(IOS_TARGET) || defined(UWP_TARGET) || defined(PSVITA_TARGET)
    return false;
#else
    return SDL_VERSION_ATLEAST(2, 0, 18);
#endif
}

armsx_hw_renderer_t* armsx_hw_renderer_create(SDL_Renderer* renderer) {
    if (!renderer || !armsx_hw_renderer_is_supported()) {
        return NULL;
    }

    armsx_hw_renderer_t* hw = (armsx_hw_renderer_t*)calloc(1, sizeof(armsx_hw_renderer_t));
    if (!hw) {
        return NULL;
    }

    hw->renderer = renderer;
    hw->trace_enabled = hw_trace_enabled();
    hw->frame_index = 0;
    hw->triangle_index = 0;

    if (!ensure_overlay_texture(hw)) {
        armsx_hw_renderer_destroy(hw);
        return NULL;
    }

    hw_tracef("renderer create complete supported=%s", hw_bool(armsx_hw_renderer_is_supported()));

    return hw;
}

void armsx_hw_renderer_destroy(armsx_hw_renderer_t* hw) {
    if (!hw) {
        return;
    }

    destroy_overlay_texture(hw);
    free(hw);
}

void armsx_hw_renderer_set_renderer(armsx_hw_renderer_t* hw, SDL_Renderer* renderer) {
    if (!hw) {
        return;
    }

    if (hw->renderer != renderer) {
        hw_tracef("renderer rebind old=%p new=%p", (void*)hw->renderer, (void*)renderer);
        hw->renderer = renderer;
        destroy_overlay_texture(hw);
        ensure_overlay_texture(hw);
    }
}

void armsx_hw_renderer_set_output_size(armsx_hw_renderer_t* hw, int width, int height) {
    if (!hw) {
        return;
    }

    width = (width > 0) ? width : 0;
    height = (height > 0) ? height : 0;

    if ((hw->width == width) && (hw->height == height) && hw->overlay_texture) {
        return;
    }

    hw_tracef("resize output old=%dx%d new=%dx%d renderer=%p overlay=%p", hw->width, hw->height, width, height, (void*)hw->renderer, (void*)hw->overlay_texture);

    if (!hw->renderer) {
        destroy_overlay_texture(hw);
        hw->width = width;
        hw->height = height;
        return;
    }

    if (hw->overlay_texture) {
        SDL_DestroyTexture(hw->overlay_texture);
        hw->overlay_texture = NULL;
    }

    hw->overlay_texture = SDL_CreateTexture(
        hw->renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        width > 0 ? width : 1,
        height > 0 ? height : 1
    );

    if (!hw->overlay_texture) {
        psxe_diag_logf("hw", "failed to resize overlay texture to %dx%d: %s", width, height, SDL_GetError());
        hw->width = 0;
        hw->height = 0;
        return;
    }

    SDL_SetTextureBlendMode(hw->overlay_texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(hw->overlay_texture, SDL_ScaleModeNearest);
    hw->width = width;
    hw->height = height;
    hw_tracef("resize complete overlay=%p blend=blend nearest", (void*)hw->overlay_texture);
}

void armsx_hw_renderer_begin_frame(armsx_hw_renderer_t* hw) {
    if (!hw || !hw->renderer || !hw->overlay_texture) {
        return;
    }

    hw->frame_index++;
    hw->triangle_index = 0;
    hw->solid_triangle_count = 0;
    hw->textured_fallback_count = 0;
    hw->transparent_fallback_count = 0;
    hw->geometry_draw_count = 0;
    hw->geometry_fail_count = 0;
    save_renderer_state(hw);
    hw_log_renderer_state(hw, "frame-pre-overlay");
    hw_tracef(
        "frame-begin frame=%llu renderer=%p overlay=%p page=%p size=%dx%d viewport=%dx%d scale=(%f,%f) logical=%dx%d clip=%s integer_scale=%s",
        (unsigned long long)hw->frame_index,
        (void*)hw->renderer,
        (void*)hw->overlay_texture,
        (void*)hw->page_texture,
        hw->width,
        hw->height,
        hw->saved_viewport.w,
        hw->saved_viewport.h,
        hw->saved_scale_x,
        hw->saved_scale_y,
        hw->saved_logical_width,
        hw->saved_logical_height,
        hw->saved_clip_enabled == SDL_TRUE ? "enabled" : "disabled",
        hw->saved_integer_scale == SDL_TRUE ? "true" : "false"
    );

    SDL_SetRenderTarget(hw->renderer, hw->overlay_texture);
    SDL_RenderSetViewport(hw->renderer, NULL);
    SDL_RenderSetClipRect(hw->renderer, NULL);
    SDL_RenderSetScale(hw->renderer, 1.0f, 1.0f);
    SDL_RenderSetIntegerScale(hw->renderer, SDL_FALSE);
    SDL_RenderSetLogicalSize(hw->renderer, 0, 0);
    SDL_SetRenderDrawColor(hw->renderer, 0, 0, 0, 0);
    SDL_RenderClear(hw->renderer);
    hw_log_renderer_state(hw, "frame-overlay-ready");
}

void armsx_hw_renderer_end_frame(armsx_hw_renderer_t* hw) {
    if (!hw || !hw->renderer) {
        return;
    }

    if (hw_trace_enabled()) {
        hw_tracef(
            "frame-summary frame=%llu solid=%llu textured_fallback=%llu transparent_fallback=%llu geometry_draws=%llu geometry_failures=%llu",
            (unsigned long long)hw->frame_index,
            (unsigned long long)hw->solid_triangle_count,
            (unsigned long long)hw->textured_fallback_count,
            (unsigned long long)hw->transparent_fallback_count,
            (unsigned long long)hw->geometry_draw_count,
            (unsigned long long)hw->geometry_fail_count
        );
    }

    hw_tracef("frame-end frame=%llu renderer=%p", (unsigned long long)(hw ? hw->frame_index : 0), (void*)hw->renderer);
    SDL_SetRenderTarget(hw->renderer, NULL);
    restore_renderer_state(hw);
    hw_log_renderer_state(hw, "frame-post-restore");
}

SDL_Texture* armsx_hw_renderer_overlay_texture(armsx_hw_renderer_t* hw) {
    if (!hw) {
        return NULL;
    }

    return hw->overlay_texture;
}

#define BGR555(c) \
    (((c & 0x0000f8) >> 3) | \
     ((c & 0x00f800) >> 6) | \
     ((c & 0xf80000) >> 9))

void gpu_hw_render_triangle(psx_gpu_t* gpu, vertex_t v0, vertex_t v1, vertex_t v2, poly_data_t data, int edge) {
    armsx_hw_renderer_t* hw = gpu ? (armsx_hw_renderer_t*)gpu->udata[2] : NULL;

    if (!hw || !hw->renderer || !hw->overlay_texture) {
        if (hw && !hw->fallback_warned) {
            hw->fallback_warned = 1;
            psxe_diag_logf(
                "hw",
                "fallback-to-software renderer=%p page=%p overlay=%p gpu=%p",
                hw ? (void*)hw->renderer : NULL,
                hw ? (void*)hw->page_texture : NULL,
                hw ? (void*)hw->overlay_texture : NULL,
                (void*)gpu
            );
        }
        gpu_render_triangle(gpu, v0, v1, v2, data, edge);
        return;
    }

    if ((data.attrib & (PA_TEXTURED | PA_TRANSP)) != 0) {
        if (data.attrib & PA_TEXTURED) {
            hw->textured_fallback_count++;
        }
        if (data.attrib & PA_TRANSP) {
            hw->transparent_fallback_count++;
        }
        hw_tracef(
            "fallback-to-software frame=%llu index=%llu reason=%s attrib=0x%02x edge=%d",
            (unsigned long long)hw->frame_index,
            (unsigned long long)hw->triangle_index,
            (data.attrib & PA_TEXTURED) ? "textured" : "transparent",
            data.attrib,
            edge
        );
        gpu_render_triangle(gpu, v0, v1, v2, data, edge);
        return;
    }

    vertex_t a, b, c;

    a = v0;

    if (((v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x)) < 0) {
        b = v2;
        c = v1;
    } else {
        b = v1;
        c = v2;
    }

    a.x += gpu->off_x;
    b.x += gpu->off_x;
    c.x += gpu->off_x;
    a.y += gpu->off_y;
    b.y += gpu->off_y;
    c.y += gpu->off_y;

    const int tpx = (data.texp & 0xf) << 6;
    const int tpy = (data.texp & 0x10) << 4;
    const int depth = (data.texp >> 7) & 3;

    hw->triangle_index++;
    hw->solid_triangle_count++;
    hw_tracef(
        "tri frame=%llu index=%llu edge=%d attrib=0x%02x clut=0x%04x texp=0x%04x page=(%d,%d) depth=%d "
        "v0=(%d,%d c=%08x tx=%u ty=%u) v1=(%d,%d c=%08x tx=%u ty=%u) v2=(%d,%d c=%08x tx=%u ty=%u)",
        (unsigned long long)hw->frame_index,
        (unsigned long long)hw->triangle_index,
        edge,
        data.attrib,
        data.clut,
        data.texp,
        tpx,
        tpy,
        depth,
        a.x, a.y, a.c, a.tx, a.ty,
        b.x, b.y, b.c, b.tx, b.ty,
        c.x, c.y, c.c, c.tx, c.ty
    );

    const int xmin = (a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x));
    const int ymin = (a.y < b.y ? (a.y < c.y ? a.y : c.y) : (b.y < c.y ? b.y : c.y));
    const int xmax = (a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x));
    const int ymax = (a.y > b.y ? (a.y > c.y ? a.y : c.y) : (b.y > c.y ? b.y : c.y));
    hw_tracef(
        "tri-bounds frame=%llu index=%llu bbox=(%d,%d)-(%d,%d) overlay=%dx%d",
        (unsigned long long)hw->frame_index,
        (unsigned long long)hw->triangle_index,
        xmin,
        ymin,
        xmax,
        ymax,
        hw->width,
        hw->height
    );

    SDL_Vertex verts[3];
    memset(verts, 0, sizeof(verts));

    verts[0].position.x = (float)a.x;
    verts[0].position.y = (float)a.y;
    verts[1].position.x = (float)b.x;
    verts[1].position.y = (float)b.y;
    verts[2].position.x = (float)c.x;
    verts[2].position.y = (float)c.y;
    verts[0].tex_coord.x = (float)a.tx / 1024.0f;
    verts[0].tex_coord.y = (float)a.ty / 512.0f;
    verts[1].tex_coord.x = (float)b.tx / 1024.0f;
    verts[1].tex_coord.y = (float)b.ty / 512.0f;
    verts[2].tex_coord.x = (float)c.tx / 1024.0f;
    verts[2].tex_coord.y = (float)c.ty / 512.0f;
    verts[0].color.a = 0xff;
    verts[0].color.r = (a.c >> 0) & 0xff;
    verts[0].color.g = (a.c >> 8) & 0xff;
    verts[0].color.b = (a.c >> 16) & 0xff;
    verts[1].color.a = 0xff;
    verts[1].color.r = (b.c >> 0) & 0xff;
    verts[1].color.g = (b.c >> 8) & 0xff;
    verts[1].color.b = (b.c >> 16) & 0xff;
    verts[2].color.a = 0xff;
    verts[2].color.r = (c.c >> 0) & 0xff;
    verts[2].color.g = (c.c >> 8) & 0xff;
    verts[2].color.b = (c.c >> 16) & 0xff;

#if SDL_VERSION_ATLEAST(2, 0, 18)
    hw_tracef(
        "geometry-draw frame=%llu index=%llu texture=null positions=[(%f,%f),(%f,%f),(%f,%f)] texcoords=[(%f,%f),(%f,%f),(%f,%f)] colors=[(%u,%u,%u,%u),(%u,%u,%u,%u),(%u,%u,%u,%u)]",
        (unsigned long long)hw->frame_index,
        (unsigned long long)hw->triangle_index,
        verts[0].position.x, verts[0].position.y,
        verts[1].position.x, verts[1].position.y,
        verts[2].position.x, verts[2].position.y,
        verts[0].tex_coord.x, verts[0].tex_coord.y,
        verts[1].tex_coord.x, verts[1].tex_coord.y,
        verts[2].tex_coord.x, verts[2].tex_coord.y,
        verts[0].color.r, verts[0].color.g, verts[0].color.b, verts[0].color.a,
        verts[1].color.r, verts[1].color.g, verts[1].color.b, verts[1].color.a,
        verts[2].color.r, verts[2].color.g, verts[2].color.b, verts[2].color.a
    );
    if (SDL_RenderGeometry(hw->renderer, NULL, verts, 3, NULL, 0) != 0) {
        hw->geometry_fail_count++;
        psxe_diag_logf("hw", "geometry-draw-failed frame=%llu index=%llu error=%s",
            (unsigned long long)hw->frame_index,
            (unsigned long long)hw->triangle_index,
            SDL_GetError());
        gpu_render_triangle(gpu, v0, v1, v2, data, edge);
        return;
    }
    hw->geometry_draw_count++;
#else
    gpu_render_triangle(gpu, v0, v1, v2, data, edge);
#endif
}

#endif
