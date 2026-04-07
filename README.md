# ARMSX
ARMSX is a fork of `psxe`, rebuilt around a native FSUI donor shell and SDL2-hosted frontends for desktop, Android, iOS, UWP, web, and PSVita.

## What It Supports
ARMSX currently has the following emulator pieces wired up:
- CPU, DMA, GPU, SPU, MDEC, GTE, and timers
- CD-ROM loading
- memory cards
- BIOS selection and PS-X EXE boot
- screenshot capture
- logging controls
- VSync control
- fast forward
- protocol/file launch through `armsx:///...` on supported hosts
- the FSUI settings shell

Disc image support is:
- `BIN/CUE`
- single-track `BIN`
- `ISO`

Not implemented yet:
- save states
- netplay
- texture packs
- a separate hardware renderer backend
- CHD support, which is still stubbed in-tree

## Supported Targets
- Desktop: macOS, Linux, Windows
- Mobile and alternate hosts: Android, iOS, UWP, WebAssembly, PSVita
- CI also builds Linux `x64`, `x32`, `arm64`, `arm32` and Windows `x64`/`x32` packages

Desktop and Android are the main day-to-day targets. The other ports are present and buildable, but they are more platform-specific and may lag behind the desktop path.

## CI Runners
The Gitea workflow expects host-mode Linux runner labels for the Linux-based jobs, currently `linux_amd64`. Docker-mode runners create per-job bridge networks and can exhaust the daemon's address pools on long CI runs.

## Running
On desktop, launch the emulator directly:

```sh
./bin/armsx --bios=/path/to/bios.bin --cdrom=/path/to/game.cue
```

You can also launch a file with the protocol handler on supported hosts:

```text
armsx:///absolute/path/to/game.cue
```

The app opens an FSUI shell where you can choose BIOS files, start discs, change settings, take screenshots, and toggle fast forward or VSync.

Settings are stored under SDL's pref path, usually `SDL_GetPrefPath("nanodata", "armsx")`, in `settings.toml`.

## Building
### Linux Desktop
Install SDL2 development packages, then run:

```sh
./build.sh
```

### macOS
Install SDL2 and dylibbundler:

```sh
brew install sdl2 dylibbundler
```

Then build a bundle:

```sh
./build.sh macosapp
```

### Windows
Install a MinGW toolchain and SDL2, then run:

```powershell
./build-deps.ps1
./build-win64.ps1
./build-win32.ps1
```

### Android
Set `ANDROID_NDK_ROOT` to an NDK 27+ install and build:

```sh
./build.sh android
```

### iOS
Use the bundled SDL2 xcframework and run:

```sh
./build.sh ios
```

### UWP
Build the native runtime or solution from Windows:

```sh
./build-uwp-native.sh x64 Debug
./build-uwp-native.sh x86 Release
./build-uwp-all.ps1
```

### WebAssembly
Requires Emscripten:

```sh
./build.sh wasm
```

### PSVita
Requires VITASDK:

```sh
./build.sh psvita
```

## Configuration
`settings.toml` is generated on first run. CLI flags always override file settings for the current session. Some internal fields still use historical `psxe_*` names for compatibility.

## Acknowledgements
ARMSX uses:
- `argparse.c`
- `log.c`
- `tomlc99`
- SDL2
- FSUI donor / ImGui
