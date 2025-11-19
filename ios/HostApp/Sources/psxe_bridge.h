#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Provided by libpsxe.dylib (__DLL_BUILD)
int external_main(int argc, const char* argv[], void* external_window, void* external_renderer);

#ifdef __cplusplus
}
#endif
