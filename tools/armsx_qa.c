/*
 * armsx-qa - deterministic headless QA driver for ARMSX1.
 *
 * Boots a disc or EXE with no window, no audio and no host input, runs a fixed
 * number of frames, replays a scripted input timeline, and emits frame hashes
 * and PNG captures. Everything it prints is machine-readable, so an AI agent
 * can drive a real game and inspect the result without a human at the keyboard.
 *
 * It links the psx/ core only. The core has no SDL or frontend dependency and
 * psx_run_frame() advances a fixed cycle count, so a run is reproducible: same
 * inputs in, same hashes out. That property is the whole point - it is what
 * makes a frame hash usable as a regression gate.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "psx.h"
#include "input/sda.h"
#include "dev/cdrom/cdrom.h"
#include "dev/cdrom/disc.h"
#include "dev/gpu.h"
#include "enhance/see.h"
#include "miniz.h"

/* ------------------------------------------------------------------ */
/* Button table - names are what appear in input scripts.             */
/* ------------------------------------------------------------------ */

typedef struct {
    const char* name;
    uint32_t mask;
} qa_button_t;

static const qa_button_t QA_BUTTONS[] = {
    { "select",   PSXI_SW_SDA_SELECT    },
    { "l3",       PSXI_SW_SDA_L3        },
    { "r3",       PSXI_SW_SDA_R3        },
    { "start",    PSXI_SW_SDA_START     },
    { "up",       PSXI_SW_SDA_PAD_UP    },
    { "right",    PSXI_SW_SDA_PAD_RIGHT },
    { "down",     PSXI_SW_SDA_PAD_DOWN  },
    { "left",     PSXI_SW_SDA_PAD_LEFT  },
    { "l2",       PSXI_SW_SDA_L2        },
    { "r2",       PSXI_SW_SDA_R2        },
    { "l1",       PSXI_SW_SDA_L1        },
    { "r1",       PSXI_SW_SDA_R1        },
    { "triangle", PSXI_SW_SDA_TRIANGLE  },
    { "circle",   PSXI_SW_SDA_CIRCLE    },
    { "cross",    PSXI_SW_SDA_CROSS     },
    { "square",   PSXI_SW_SDA_SQUARE    },
    { "analog",   PSXI_SW_SDA_ANALOG    },
};

static const int QA_BUTTON_COUNT = (int)(sizeof(QA_BUTTONS) / sizeof(QA_BUTTONS[0]));

static uint32_t qa_button_mask(const char* name) {
    for (int i = 0; i < QA_BUTTON_COUNT; i++)
        if (!strcmp(QA_BUTTONS[i].name, name))
            return QA_BUTTONS[i].mask;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Input script                                                        */
/* ------------------------------------------------------------------ */

enum {
    QA_EV_PRESS,
    QA_EV_RELEASE,
    QA_EV_CAPTURE,
    QA_EV_HASH,
    QA_EV_EXIT
};

typedef struct {
    uint64_t frame;
    int kind;
    uint32_t mask;
    char label[48];
} qa_event_t;

typedef struct {
    qa_event_t* items;
    size_t count, cap;
} qa_script_t;

static void qa_script_push(qa_script_t* s, qa_event_t ev) {
    if (s->count == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->items = (qa_event_t*)realloc(s->items, s->cap * sizeof(qa_event_t));
    }
    s->items[s->count++] = ev;
}

static int qa_event_cmp(const void* a, const void* b) {
    uint64_t fa = ((const qa_event_t*)a)->frame;
    uint64_t fb = ((const qa_event_t*)b)->frame;
    return (fa > fb) - (fa < fb);
}

/*
 * Script grammar, one command per line. '#' starts a comment.
 *
 *   <frame> press   <button>
 *   <frame> release <button>
 *   <frame> tap     <button> [hold_frames]   ; press then release, default 3
 *   <frame> capture [label]
 *   <frame> hash    [label]
 *   <frame> exit
 *
 * Frames are absolute and need not be ordered; the list is sorted on load.
 */
static int qa_script_load(qa_script_t* s, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "armsx-qa: cannot open script '%s'\n", path);
        return 1;
    }

    char line[512];
    int lineno = 0;
    int errors = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;

        char* hash = strchr(line, '#');
        if (hash) *hash = '\0';

        unsigned long long frame;
        char cmd[32] = {0}, arg[64] = {0};
        int extra = 0;

        int n = sscanf(line, "%llu %31s %63s %d", &frame, cmd, arg, &extra);
        if (n < 2) {
            /* blank or comment-only line */
            int only_space = 1;
            for (char* p = line; *p; p++)
                if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') { only_space = 0; break; }
            if (only_space) continue;
            fprintf(stderr, "armsx-qa: %s:%d: malformed line\n", path, lineno);
            errors++;
            continue;
        }

        qa_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.frame = (uint64_t)frame;
        snprintf(ev.label, sizeof(ev.label), "%s", n >= 3 ? arg : "");

        if (!strcmp(cmd, "press") || !strcmp(cmd, "release") || !strcmp(cmd, "tap")) {
            if (n < 3) {
                fprintf(stderr, "armsx-qa: %s:%d: '%s' needs a button\n", path, lineno, cmd);
                errors++;
                continue;
            }
            ev.mask = qa_button_mask(arg);
            if (!ev.mask) {
                fprintf(stderr, "armsx-qa: %s:%d: unknown button '%s'\n", path, lineno, arg);
                errors++;
                continue;
            }
            if (!strcmp(cmd, "release")) {
                ev.kind = QA_EV_RELEASE;
                qa_script_push(s, ev);
            } else {
                ev.kind = QA_EV_PRESS;
                qa_script_push(s, ev);
                if (!strcmp(cmd, "tap")) {
                    qa_event_t up = ev;
                    up.kind = QA_EV_RELEASE;
                    up.frame = ev.frame + (uint64_t)(n >= 4 && extra > 0 ? extra : 3);
                    qa_script_push(s, up);
                }
            }
        } else if (!strcmp(cmd, "capture")) {
            ev.kind = QA_EV_CAPTURE;
            qa_script_push(s, ev);
        } else if (!strcmp(cmd, "hash")) {
            ev.kind = QA_EV_HASH;
            qa_script_push(s, ev);
        } else if (!strcmp(cmd, "exit")) {
            ev.kind = QA_EV_EXIT;
            qa_script_push(s, ev);
        } else {
            fprintf(stderr, "armsx-qa: %s:%d: unknown command '%s'\n", path, lineno, cmd);
            errors++;
        }
    }

    fclose(f);
    qsort(s->items, s->count, sizeof(qa_event_t), qa_event_cmp);
    return errors ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Framebuffer -> RGB8                                                 */
/* ------------------------------------------------------------------ */

/*
 * The GPU presents either BGR555 (format 0) or packed RGB24 (format 1), always
 * at PSX_GPU_FB_STRIDE bytes per row regardless of visible width.
 */
static int qa_match_serial(const char* s, char* out, size_t outn) {
    static const char* prefixes[] = { "SCUS", "SLUS", "SLES", "SCES", "SLPS", "SCPS", NULL };
    for (int i = 0; prefixes[i]; i++) {
        const char* p = prefixes[i];
        if (strncmp(s, p, 4) != 0 || s[4] != '_') continue;
        if (outn < 12) return 0;
        if (!(s[5] >= '0' && s[5] <= '9') || !(s[6] >= '0' && s[6] <= '9') ||
            !(s[7] >= '0' && s[7] <= '9') || s[8] != '.' ||
            !(s[9] >= '0' && s[9] <= '9') || !(s[10] >= '0' && s[10] <= '9'))
            continue;
        snprintf(out, outn, "%c%c%c%c-%c%c%c.%c%c",
                 p[0], p[1], p[2], p[3], s[5], s[6], s[7], s[9], s[10]);
        return 1;
    }
    return 0;
}

static int qa_guess_serial(psx_cdrom_t* cdrom, char* out, size_t outn) {
    if (!cdrom || !cdrom->disc || !out || outn < 12) return 1;
    out[0] = 0;
    unsigned char buf[CD_SECTOR_SIZE];
    for (uint32_t lba = 150; lba < 214; lba++) {
        if (!psx_disc_read(cdrom->disc, lba, buf)) continue;
        for (int i = 0; i + 11 < CD_SECTOR_SIZE; i++) {
            if (qa_match_serial((const char*)buf + i, out, outn))
                return 0;
        }
    }
    return 1;
}

static void qa_vram_write(psx_gpu_t* gpu, unsigned x, unsigned y, unsigned w, unsigned h, void* user) {
    see_on_vram_write((see_engine_t*)user, gpu->vram, x, y, w, h);
}

static uint8_t* qa_frame_rgb(psx_t* psx, see_engine_t* see, int* out_w, int* out_h) {
    const int w = (int)psx_get_display_width(psx);
    const int h = (int)psx_get_display_height(psx);

    if (w <= 0 || h <= 0 || w > 1024 || h > 512)
        return NULL;

    const uint8_t* src = (const uint8_t*)psx_get_display_buffer(psx);
    if (!src)
        return NULL;

    uint8_t* rgb = (uint8_t*)malloc((size_t)w * h * 3);
    if (!rgb)
        return NULL;

    const int fmt24 = (int)psx_get_display_format(psx);

    for (int y = 0; y < h; y++) {
        const uint8_t* row = src + (size_t)y * PSX_GPU_FB_STRIDE;
        uint8_t* dst = rgb + (size_t)y * w * 3;

        if (fmt24) {
            memcpy(dst, row, (size_t)w * 3);
        } else {
            const uint16_t* row16 = (const uint16_t*)row;
            for (int x = 0; x < w; x++) {
                uint16_t p = row16[x];
                /* BGR555 -> RGB888, replicating high bits into the low ones */
                uint8_t r = (uint8_t)((p      ) & 0x1f);
                uint8_t g = (uint8_t)((p >>  5) & 0x1f);
                uint8_t b = (uint8_t)((p >> 10) & 0x1f);
                dst[x * 3 + 0] = (uint8_t)((r << 3) | (r >> 2));
                dst[x * 3 + 1] = (uint8_t)((g << 3) | (g >> 2));
                dst[x * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
            }
        }
    }

    *out_w = w;
    *out_h = h;
    if (see)
        see_present_rgb(see, rgb, w, h);
    return rgb;
}

/* FNV-1a over the visible RGB8 image. Resolution-sensitive on purpose: a mode
 * change is a real difference and should move the hash. */
static uint64_t qa_hash_rgb(const uint8_t* rgb, int w, int h) {
    uint64_t hv = 1469598103934665603ULL;
    const size_t n = (size_t)w * h * 3;
    for (size_t i = 0; i < n; i++) {
        hv ^= rgb[i];
        hv *= 1099511628211ULL;
    }
    hv ^= (uint64_t)w * 31u + (uint64_t)h;
    hv *= 1099511628211ULL;
    return hv;
}

/* ------------------------------------------------------------------ */
/* PNG writer (miniz deflate)                                          */
/* ------------------------------------------------------------------ */

static void qa_put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8); p[3] = (uint8_t)v;
}

static int qa_png_chunk(FILE* f, const char* tag, const uint8_t* data, uint32_t len) {
    uint8_t hdr[8];
    qa_put32(hdr, len);
    memcpy(hdr + 4, tag, 4);
    if (fwrite(hdr, 1, 8, f) != 8) return 1;
    if (len && fwrite(data, 1, len, f) != len) return 1;

    mz_ulong crc = mz_crc32(MZ_CRC32_INIT, (const unsigned char*)tag, 4);
    if (len) crc = mz_crc32(crc, data, len);

    uint8_t crcb[4];
    qa_put32(crcb, (uint32_t)crc);
    return fwrite(crcb, 1, 4, f) != 4;
}

static int qa_write_png(const char* path, const uint8_t* rgb, int w, int h) {
    /* Prefix each scanline with filter byte 0 (None). */
    const size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
    uint8_t* raw = (uint8_t*)malloc(raw_len);
    if (!raw) return 1;

    for (int y = 0; y < h; y++) {
        uint8_t* dst = raw + (size_t)y * (1 + (size_t)w * 3);
        dst[0] = 0;
        memcpy(dst + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }

    mz_ulong comp_cap = mz_compressBound((mz_ulong)raw_len);
    uint8_t* comp = (uint8_t*)malloc(comp_cap);
    if (!comp) { free(raw); return 1; }

    if (mz_compress2(comp, &comp_cap, raw, (mz_ulong)raw_len, MZ_DEFAULT_COMPRESSION) != MZ_OK) {
        free(raw); free(comp);
        return 1;
    }
    free(raw);

    FILE* f = fopen(path, "wb");
    if (!f) { free(comp); return 1; }

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    int bad = fwrite(sig, 1, 8, f) != 8;

    uint8_t ihdr[13];
    qa_put32(ihdr + 0, (uint32_t)w);
    qa_put32(ihdr + 4, (uint32_t)h);
    ihdr[8]  = 8;   /* bit depth   */
    ihdr[9]  = 2;   /* colour type: truecolour RGB */
    ihdr[10] = 0;   /* deflate     */
    ihdr[11] = 0;   /* filter      */
    ihdr[12] = 0;   /* no interlace*/

    bad |= qa_png_chunk(f, "IHDR", ihdr, sizeof(ihdr));
    bad |= qa_png_chunk(f, "IDAT", comp, (uint32_t)comp_cap);
    bad |= qa_png_chunk(f, "IEND", NULL, 0);

    fclose(f);
    free(comp);
    return bad;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static void qa_usage(void) {
    printf(
        "armsx-qa - deterministic headless QA driver\n"
        "\n"
        "Usage: armsx-qa --bios=PATH (--cdrom=PATH | --exe=PATH) [options]\n"
        "\n"
        "  --bios=PATH          BIOS image (required)\n"
        "  --cdrom=PATH         disc image (bin/cue or iso)\n"
        "  --exe=PATH           PS-X EXE to side-load\n"
        "  --frames=N           frames to run (default 600)\n"
        "  --script=PATH        input script; see below\n"
        "  --capture-dir=DIR    write PNG captures here\n"
        "  --capture-every=N    capture every N frames\n"
        "  --hash-every=N       print a frame hash every N frames\n"
        "  --stuck-frames=N     fail if the image is unchanged for N frames\n"
        "  --json               emit one JSON object per event on stdout\n"
        "  --quiet              suppress emulator logging\n"
        "  --enhance            enable the Super Enhancement Engine (presentation only)\n"
        "  --enhance-dir=PATH   per-game cache root (default: cache)\n"
        "  --serial=SCUS-942.54 disc serial override; otherwise guessed from the disc\n"
        "  --selftest=PATH      write a test-pattern PNG and exit (no BIOS needed)\n"
        "  --help               this text\n"
        "\n"
        "Script grammar (one per line, '#' comments):\n"
        "  <frame> press   <button>\n"
        "  <frame> release <button>\n"
        "  <frame> tap     <button> [hold_frames]\n"
        "  <frame> capture [label]\n"
        "  <frame> hash    [label]\n"
        "  <frame> exit\n"
        "\n"
        "Buttons: select l3 r3 start up right down left l2 r2 l1 r1\n"
        "         triangle circle cross square analog\n"
        "\n"
        "Runs are deterministic: identical inputs produce identical hashes.\n");
}

static const char* qa_opt(const char* arg, const char* key) {
    size_t n = strlen(key);
    if (strncmp(arg, key, n) == 0 && arg[n] == '=')
        return arg + n + 1;
    return NULL;
}

int main(int argc, const char** argv) {
    const char* bios = NULL;
    const char* cdrom = NULL;
    const char* exe = NULL;
    const char* script_path = NULL;
    const char* capture_dir = NULL;
    uint64_t frames = 600;
    uint64_t capture_every = 0;
    uint64_t hash_every = 0;
    uint64_t stuck_frames = 0;
    int json = 0, quiet = 0, enhance_on = 0;
    const char* enhance_dir = "cache";
    const char* serial_opt = NULL;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        const char* v;
        if ((v = qa_opt(a, "--bios")))              bios = v;
        else if ((v = qa_opt(a, "--cdrom")))        cdrom = v;
        else if ((v = qa_opt(a, "--exe")))          exe = v;
        else if ((v = qa_opt(a, "--script")))       script_path = v;
        else if ((v = qa_opt(a, "--capture-dir")))  capture_dir = v;
        else if ((v = qa_opt(a, "--frames")))       frames = strtoull(v, NULL, 10);
        else if ((v = qa_opt(a, "--capture-every"))) capture_every = strtoull(v, NULL, 10);
        else if ((v = qa_opt(a, "--hash-every")))   hash_every = strtoull(v, NULL, 10);
        else if ((v = qa_opt(a, "--stuck-frames")))  stuck_frames = strtoull(v, NULL, 10);
        else if (!strcmp(a, "--enhance"))           enhance_on = 1;
        else if ((v = qa_opt(a, "--enhance-dir")))  enhance_dir = v;
        else if ((v = qa_opt(a, "--serial")))       serial_opt = v;
        else if ((v = qa_opt(a, "--selftest"))) {
            /* Verifies the capture path end to end without a BIOS or a disc:
             * if this PNG is readable, the writer and the toolchain are good. */
            const int w = 256, h = 128;
            uint8_t* rgb = (uint8_t*)malloc((size_t)w * h * 3);
            if (!rgb) return 3;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    uint8_t* px = rgb + ((size_t)y * w + x) * 3;
                    px[0] = (uint8_t)x;
                    px[1] = (uint8_t)(y * 2);
                    px[2] = (uint8_t)((x ^ y) & 0xff);
                }
            }
            int bad = qa_write_png(v, rgb, w, h);
            uint64_t hv = qa_hash_rgb(rgb, w, h);
            free(rgb);
            if (bad) {
                fprintf(stderr, "armsx-qa: selftest failed to write '%s'\n", v);
                return 3;
            }
            printf("ARMSX_QA selftest file=%s %dx%d hash=%016llx\n",
                   v, w, h, (unsigned long long)hv);
            return 0;
        }
        else if (!strcmp(a, "--json"))              json = 1;
        else if (!strcmp(a, "--quiet"))             quiet = 1;
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { qa_usage(); return 0; }
        else {
            fprintf(stderr, "armsx-qa: unknown argument '%s'\n", a);
            return 2;
        }
    }

    if (!bios) {
        fprintf(stderr, "armsx-qa: --bios is required\n\n");
        qa_usage();
        return 2;
    }
    if (!cdrom && !exe) {
        fprintf(stderr, "armsx-qa: one of --cdrom or --exe is required\n\n");
        qa_usage();
        return 2;
    }

    /* A retail PS1 BIOS is 512 KiB. Anything else is almost certainly the
     * wrong file, and the failure mode otherwise is an illegal-instruction
     * spew at bfc00004 that looks like an emulator bug. */
    {
        FILE* bf = fopen(bios, "rb");
        if (!bf) {
            fprintf(stderr, "armsx-qa: cannot open BIOS '%s'\n", bios);
            return 3;
        }
        fseek(bf, 0, SEEK_END);
        long bsz = ftell(bf);
        fclose(bf);
        if (bsz != 512 * 1024) {
            fprintf(stderr, "armsx-qa: '%s' is %ld bytes; expected 524288 "
                            "(512 KiB). This is not a valid PS1 BIOS.\n", bios, bsz);
            return 3;
        }
    }

    log_set_quiet(quiet ? 1 : 0);

    qa_script_t script;
    memset(&script, 0, sizeof(script));
    if (script_path && qa_script_load(&script, script_path))
        return 2;

    psx_t* psx = psx_create();
    if (!psx) {
        fprintf(stderr, "armsx-qa: out of memory\n");
        return 3;
    }

    int rc = psx_init(psx, bios, NULL);
    if (rc) {
        fprintf(stderr, "armsx-qa: psx_init failed (%d) - is '%s' a valid BIOS?\n", rc, bios);
        return 3;
    }

    /* psx_cdrom_open returns 1 on success and 0 on failure - not the usual
     * C convention, and easy to get backwards. */
    if (cdrom && psx_cdrom_open(psx_get_cdrom(psx), cdrom) != 1) {
        fprintf(stderr, "armsx-qa: could not open disc '%s' "
                        "(missing, unreadable, or not a licensed PSX disc)\n", cdrom);
        return 3;
    }

    psxi_sda_t* pad_device = psxi_sda_create();
    psx_input_t* input = (psx_input_t*)malloc(sizeof(psx_input_t));
    if (!pad_device || !input) {
        fprintf(stderr, "armsx-qa: out of memory\n");
        return 3;
    }
    psxi_sda_init(pad_device, SDA_MODEL_DIGITAL);
    psxi_sda_init_input(pad_device, input);
    psx_pad_attach_joy(psx_get_pad(psx), 0, input);

    if (exe)
        psx_load_exe(psx, exe);

    see_engine_t* see = see_create();
    if (!see) {
        fprintf(stderr, "armsx-qa: out of memory\n");
        return 3;
    }
    see_set_cache_root(see, enhance_dir);
    see_set_enabled(see, enhance_on);
    if (serial_opt)
        see_set_serial(see, serial_opt);
    else if (cdrom) {
        char guessed[32];
        if (qa_guess_serial(psx_get_cdrom(psx), guessed, sizeof(guessed)) == 0)
            see_set_serial(see, guessed);
    }
    if (enhance_on)
        psx_gpu_set_vram_write_callback(psx_get_gpu(psx), qa_vram_write, see);

    if (json)
        printf("{\"event\":\"start\",\"frames\":%llu,\"bios\":\"%s\",\"media\":\"%s\","
               "\"serial\":\"%s\",\"enhance\":%s}\n",
               (unsigned long long)frames, bios, cdrom ? cdrom : exe,
               see_serial(see), enhance_on ? "true" : "false");
    else
        printf("ARMSX_QA begin frames=%llu media=%s serial=%s enhance=%s\n",
               (unsigned long long)frames, cdrom ? cdrom : exe,
               see_serial(see)[0] ? see_serial(see) : "none",
               enhance_on ? "on" : "off");
    fflush(stdout);

    size_t next_event = 0;
    uint64_t captures = 0;
    int stop = 0;

    /* Stuck detection: a frozen image means a hang, a crash, or a black
     * screen. Unattended runs need to notice that themselves. */
    uint64_t stuck_run = 0, stuck_at = 0;
    uint64_t last_hash = 0;
    int have_last = 0, stuck = 0;

    for (uint64_t frame = 0; frame < frames && !stop; frame++) {
        /* Apply every event scheduled for this frame before running it. */
        int want_capture = 0, want_hash = 0;
        char label[48] = {0};

        while (next_event < script.count && script.items[next_event].frame == frame) {
            qa_event_t* ev = &script.items[next_event++];
            switch (ev->kind) {
                case QA_EV_PRESS:
                    psx_pad_button_press(psx_get_pad(psx), 0, ev->mask);
                    break;
                case QA_EV_RELEASE:
                    psx_pad_button_release(psx_get_pad(psx), 0, ev->mask);
                    break;
                case QA_EV_CAPTURE:
                    want_capture = 1;
                    snprintf(label, sizeof(label), "%s", ev->label);
                    break;
                case QA_EV_HASH:
                    want_hash = 1;
                    break;
                case QA_EV_EXIT:
                    stop = 1;
                    break;
            }
        }

        psx_run_frame(psx);

        if (capture_every && (frame % capture_every) == 0) want_capture = 1;
        if (hash_every && (frame % hash_every) == 0)       want_hash = 1;

        if (stuck_frames) {
            int sw = 0, sh = 0;
            uint8_t* probe = qa_frame_rgb(psx, see, &sw, &sh);

            /* A frame with no usable display is itself a stall signal - a
             * blank or disabled output that never recovers is a failure, not
             * a reason to skip the check. Fold it in as a sentinel hash. */
            uint64_t ph;
            if (probe) {
                ph = qa_hash_rgb(probe, sw, sh);
                free(probe);
            } else {
                ph = 0xDEAD0000DEAD0000ULL;
            }

            if (have_last && ph == last_hash) {
                if (++stuck_run >= stuck_frames) {
                    stuck = 1;
                    stuck_at = frame;
                    stop = 1;
                }
            } else {
                stuck_run = 0;
            }
            last_hash = ph;
            have_last = 1;
        }

        if (!want_capture && !want_hash)
            continue;

        int w = 0, h = 0;
        uint8_t* rgb = qa_frame_rgb(psx, see, &w, &h);
        if (!rgb) {
            if (json)
                printf("{\"event\":\"frame\",\"frame\":%llu,\"error\":\"no display\"}\n",
                       (unsigned long long)frame);
            continue;
        }

        uint64_t hv = qa_hash_rgb(rgb, w, h);

        if (want_capture && capture_dir) {
            char path[1024];
            if (label[0])
                snprintf(path, sizeof(path), "%s/frame_%08llu_%s.png",
                         capture_dir, (unsigned long long)frame, label);
            else
                snprintf(path, sizeof(path), "%s/frame_%08llu.png",
                         capture_dir, (unsigned long long)frame);

            if (qa_write_png(path, rgb, w, h)) {
                fprintf(stderr, "armsx-qa: failed to write %s\n", path);
            } else {
                captures++;
                if (json)
                    printf("{\"event\":\"capture\",\"frame\":%llu,\"path\":\"%s\","
                           "\"w\":%d,\"h\":%d,\"hash\":\"%016llx\"}\n",
                           (unsigned long long)frame, path, w, h,
                           (unsigned long long)hv);
                else
                    printf("ARMSX_QA capture frame=%llu file=%s %dx%d hash=%016llx\n",
                           (unsigned long long)frame, path, w, h,
                           (unsigned long long)hv);
            }
        } else if (want_capture && !capture_dir) {
            fprintf(stderr, "armsx-qa: capture requested but --capture-dir not set\n");
        }

        if (want_hash) {
            if (json)
                printf("{\"event\":\"hash\",\"frame\":%llu,\"w\":%d,\"h\":%d,"
                       "\"hash\":\"%016llx\"}\n",
                       (unsigned long long)frame, w, h, (unsigned long long)hv);
            else
                printf("ARMSX_QA hash frame=%llu %dx%d hash=%016llx\n",
                       (unsigned long long)frame, w, h, (unsigned long long)hv);
        }

        fflush(stdout);
        free(rgb);
    }

    if (stuck) {
        if (json)
            printf("{\"event\":\"stuck\",\"frame\":%llu,\"frames_unchanged\":%llu,"
                   "\"hash\":\"%016llx\"}\n",
                   (unsigned long long)stuck_at, (unsigned long long)stuck_frames,
                   (unsigned long long)last_hash);
        else
            printf("ARMSX_QA stuck frame=%llu unchanged_for=%llu hash=%016llx\n",
                   (unsigned long long)stuck_at, (unsigned long long)stuck_frames,
                   (unsigned long long)last_hash);
    }

    if (json)
        printf("{\"event\":\"done\",\"captures\":%llu,\"stuck\":%s,"
               "\"serial\":\"%s\",\"enhance\":%s,\"enhance_applied\":%s,\"assets\":%d}\n",
               (unsigned long long)captures, stuck ? "true" : "false",
               see_serial(see), enhance_on ? "true" : "false",
               see_applied(see) ? "true" : "false", see_asset_count(see));
    else
        printf("ARMSX_QA done captures=%llu stuck=%s serial=%s enhance=%s applied=%s assets=%d\n",
               (unsigned long long)captures, stuck ? "yes" : "no",
               see_serial(see)[0] ? see_serial(see) : "none",
               enhance_on ? "on" : "off",
               see_applied(see) ? "yes" : "no", see_asset_count(see));

    see_destroy(see);
    free(script.items);
    return stuck ? 4 : 0;
}
