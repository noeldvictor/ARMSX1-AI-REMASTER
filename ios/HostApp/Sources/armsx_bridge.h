#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Provided by libarmsx.dylib (__DLL_BUILD)
int external_main(int argc, const char* argv[], void* external_window, void* external_renderer);
void psxe_enqueue_launch_argument(const char* argument);

#ifdef __cplusplus
}
#endif
