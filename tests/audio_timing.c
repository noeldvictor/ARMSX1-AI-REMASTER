#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "psx/dev/gpu.h"

static int validate_mode(uint32_t display_mode, double expected_rate) {
    psx_gpu_t gpu;
    memset(&gpu, 0, sizeof(gpu));
    gpu.display_mode = display_mode;

    const double frame_rate = (double)psx_gpu_frame_rate(&gpu);
    if (fabs(frame_rate - expected_rate) > 0.0001) {
        fprintf(
            stderr,
            "frame-rate mismatch mode=%s actual=%.9f expected=%.9f\n",
            display_mode & 0x8 ? "PAL" : "NTSC",
            frame_rate,
            expected_rate
        );
        return 1;
    }

    double accumulator = 0.0;
    uint64_t generated_samples = 0;
    const uint64_t frame_count = 36000;
    for (uint64_t frame = 0; frame < frame_count; frame++) {
        accumulator += 44100.0 / frame_rate;
        const uint64_t frame_samples = (uint64_t)accumulator;
        accumulator -= (double)frame_samples;
        generated_samples += frame_samples;
    }

    const double expected_samples = 44100.0 * ((double)frame_count / frame_rate);
    if (fabs((double)generated_samples - expected_samples) >= 1.0) {
        fprintf(
            stderr,
            "sample drift mode=%s generated=%llu expected=%.6f remainder=%.9f\n",
            display_mode & 0x8 ? "PAL" : "NTSC",
            (unsigned long long)generated_samples,
            expected_samples,
            accumulator
        );
        return 1;
    }

    printf(
        "audio-timing mode=%s frame_rate=%.6f frames=%llu samples=%llu remainder=%.6f\n",
        display_mode & 0x8 ? "PAL" : "NTSC",
        frame_rate,
        (unsigned long long)frame_count,
        (unsigned long long)generated_samples,
        accumulator
    );
    return 0;
}

int main(void) {
    const double ntsc_rate =
        ((double)PSX_GPU_CLOCK_FREQ_NTSC * 1000000.0) /
        ((double)PSX_GPU_CYCLES_PER_SCANLINE_NTSC * (double)PSX_GPU_SCANS_PER_FRAME_NTSC);
    const double pal_rate =
        ((double)PSX_GPU_CLOCK_FREQ_PAL * 1000000.0) /
        ((double)PSX_GPU_CYCLES_PER_SCANLINE_PAL * (double)PSX_GPU_SCANS_PER_FRAME_PAL);

    if (validate_mode(0, ntsc_rate) || validate_mode(0x8, pal_rate)) {
        return 1;
    }

    puts("audio timing validation passed");
    return 0;
}
