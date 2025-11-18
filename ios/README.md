# iOS host scaffold

This directory contains a minimal UIKit host for the shared `libpsxe` build. It embeds SDL2 as a dynamic framework and passes the host window into `external_main`.

## Layout
- `Frameworks/` – drop `SDL2.xcframework` (built from commit 385e995790930bc70ce43533f621caedec033895) and the iOS-built `libpsxe.dylib` here.
- `HostApp/` – source, Info.plist and an XcodeGen `project.yml` that describes a single-app target.
- `bin/bios.bin` – optional test BIOS is bundled (resource marked optional) so the iOS target can boot out-of-the-box during development.

## Building libpsxe for iOS
```
./build.sh IOS_TARGET=1 IOS_SDL_FRAMEWORK="$(pwd)/ios/Frameworks/SDL2.xcframework/ios-arm64"
```
This forces dynamic SDL, uses the iPhone SDK toolchain, and outputs `bin/libpsxe.dylib` for device (arm64). You can tweak `IOS_SDK`/`IOS_DEPLOYMENT_TARGET` as needed.

## Generating/opening the app project
The `HostApp/project.yml` is XcodeGen-ready. After placing the frameworks:
```
cd ios/HostApp
xcodegen generate
open PSXEHost.xcodeproj
```
The target embeds both `SDL2.xcframework` and `libpsxe.dylib` into the app bundle and runs `external_main` on launch using the host `UIWindow`.
