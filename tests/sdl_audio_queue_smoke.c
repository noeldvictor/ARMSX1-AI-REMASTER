#include <SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    if (SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) != 0) {
        fprintf(stderr, "failed to select SDL dummy audio driver: %s\n", SDL_GetError());
        return 1;
    }
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL audio initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    SDL_zero(desired);
    SDL_zero(obtained);
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = NULL;

    const SDL_AudioDeviceID device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (!device) {
        fprintf(stderr, "SDL queued audio device failed to open: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int16_t samples[4096 * 2];
    memset(samples, 0, sizeof(samples));
    if (SDL_QueueAudio(device, samples, (Uint32)sizeof(samples)) != 0) {
        fprintf(stderr, "SDL_QueueAudio failed: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(device);
        SDL_Quit();
        return 1;
    }

    const Uint32 queued_before = SDL_GetQueuedAudioSize(device);
    if (queued_before != sizeof(samples)) {
        fprintf(stderr, "unexpected initial queued size actual=%u expected=%zu\n", queued_before, sizeof(samples));
        SDL_CloseAudioDevice(device);
        SDL_Quit();
        return 1;
    }

    SDL_PauseAudioDevice(device, 0);
    Uint32 queued_after = queued_before;
    for (int attempt = 0; attempt < 50 && queued_after >= queued_before; attempt++) {
        SDL_Delay(10);
        queued_after = SDL_GetQueuedAudioSize(device);
    }
    if (queued_after >= queued_before) {
        fprintf(stderr, "queued audio did not advance before=%u after=%u\n", queued_before, queued_after);
        SDL_CloseAudioDevice(device);
        SDL_Quit();
        return 1;
    }

    SDL_PauseAudioDevice(device, 1);
    SDL_ClearQueuedAudio(device);
    if (SDL_GetQueuedAudioSize(device) != 0) {
        fputs("SDL_ClearQueuedAudio left queued bytes\n", stderr);
        SDL_CloseAudioDevice(device);
        SDL_Quit();
        return 1;
    }

    printf(
        "SDL_AUDIO_QUEUE passed driver=%s frequency=%d channels=%u samples=%u consumed=%u\n",
        SDL_GetCurrentAudioDriver(),
        obtained.freq,
        (unsigned)obtained.channels,
        (unsigned)obtained.samples,
        queued_before - queued_after
    );
    SDL_CloseAudioDevice(device);
    SDL_Quit();
    return 0;
}
