#!/bin/sh

set -e

# Usage:
#   ./build.sh             -> desktop build (static SDL if available)
#   ./build.sh shared      -> build shared lib (forces dynamic SDL)
#   ./build.sh ios         -> build iOS dylib using iPhone SDK + ios/Frameworks/SDL2.xcframework
#   ./build.sh macosapp    -> build desktop exe and bundle armsx.app

MODE="$1"

bundle_macos_app() {
    mkdir -p armsx.app/Contents/MacOS/Libraries
    cp bin/armsx armsx.app/Contents/MacOS
    chmod 777 armsx.app/Contents/MacOS/armsx
    dylibbundler -b -x ./armsx.app/Contents/MacOS/armsx -d ./armsx.app/Contents/Libraries/ -p @executable_path/../Libraries/ -cd
    cp Info.plist armsx.app/Contents/Info.plist
}

if [ "$MODE" = "ios" ]; then
    IOS_SDK="${IOS_SDK:-iphoneos}"
    IOS_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-13.0}"
    IOS_SDKROOT="$(xcrun --sdk "${IOS_SDK}" --show-sdk-path)"
    IOS_CC="$(xcrun --sdk "${IOS_SDK}" --find clang)"
    IOS_CXX="$(xcrun --sdk "${IOS_SDK}" --find clang++)"

    DEFAULT_SDL_FW_ROOT="$(pwd)/ios/Frameworks/SDL2.xcframework"
    IOS_SDL_FRAMEWORK="${IOS_SDL_FRAMEWORK:-$DEFAULT_SDL_FW_ROOT}"

    # Resolve to the actual SDL2.framework path
    if [ -d "${IOS_SDL_FRAMEWORK}/SDL2.framework" ]; then
        IOS_SDL_FRAMEWORK="${IOS_SDL_FRAMEWORK}/SDL2.framework"
    elif [ -d "${IOS_SDL_FRAMEWORK}/ios-arm64/SDL2.framework" ]; then
        IOS_SDL_FRAMEWORK="${IOS_SDL_FRAMEWORK}/ios-arm64/SDL2.framework"
    elif [ ! -d "${IOS_SDL_FRAMEWORK}/Headers" ]; then
        echo "SDL2.framework not found under ${IOS_SDL_FRAMEWORK}"
        echo "Place the xcframework in ios/Frameworks or set IOS_SDL_FRAMEWORK to the SDL2.framework path."
        exit 1
    fi

    if [ ! -d "${IOS_SDL_FRAMEWORK}" ]; then
        echo "SDL2.xcframework not found at ${IOS_SDL_FRAMEWORK}"
        echo "Place the framework in ios/Frameworks (or set IOS_SDL_FRAMEWORK) and retry."
        exit 1
    fi

    echo "Building iOS dylib with SDK ${IOS_SDK} (${IOS_SDKROOT})"
    make clean
    IOS_ENV="IOS_TARGET=1 IOS_SDK=${IOS_SDK} IOS_DEPLOYMENT_TARGET=${IOS_DEPLOYMENT_TARGET} SDKROOT=${IOS_SDKROOT} CC=${IOS_CC} CXX=${IOS_CXX} SDL_STATIC=0 IOS_SDL_FRAMEWORK=${IOS_SDL_FRAMEWORK}"
    eval "make shared ${IOS_ENV}"
    echo "Copying libarmsx.dylib to ios/Frameworks/"
    cp bin/libarmsx.dylib ios/Frameworks/

elif [ "$MODE" = "shared" ]; then
    make clean
    SDL_STATIC=0 make shared

elif [ "$MODE" = "macosapp" ]; then
    make clean
    make SDL_STATIC="${SDL_STATIC:-1}"
    bundle_macos_app

else
    make clean
    make SDL_STATIC="${SDL_STATIC:-1}" IMGUI_FRONTEND=1
fi
