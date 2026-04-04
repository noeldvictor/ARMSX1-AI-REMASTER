#!/bin/sh

set -e

# Usage:
#   ./build.sh             -> desktop build (static SDL if available)
#   ./build.sh shared      -> build shared lib (forces dynamic SDL)
#   ./build.sh ios         -> build iOS dylib using iPhone SDK + ios/Frameworks/SDL2.xcframework
#   ./build.sh macosapp    -> build desktop exe and bundle armsx.app
#   ./build.sh wasm        -> build WebAssembly target to bin/wasm using emscripten
#   ./build.sh android     -> build SDL2/libarmsx for Android and stage under android/app/src/main/jniLibs

MODE="$1"
BUILD_JOBS="${BUILD_JOBS:-4}"

build_fsui_native() {
    if [ "$(uname -s)" = "Darwin" ]; then
        MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-10.15}"
        cmake -S third_party/fsui-lib -B build/fsui/native \
            -DFSUI_BUILD_SAMPLES=OFF \
            -DFSUI_PLATFORM_BACKEND=SDL2 \
            -DFSUI_USE_SYSTEM_SDL2=ON \
            -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET}"
    else
        cmake -S third_party/fsui-lib -B build/fsui/native \
            -DFSUI_BUILD_SAMPLES=OFF \
            -DFSUI_PLATFORM_BACKEND=SDL2 \
            -DFSUI_USE_SYSTEM_SDL2=ON
    fi
    cmake --build build/fsui/native -j"${BUILD_JOBS}"
}

build_fsui_wasm() {
    if ! command -v emcmake >/dev/null 2>&1; then
        echo "emcmake is required for the wasm FSUI build."
        exit 1
    fi

    emcmake cmake -S third_party/fsui-lib -B build/fsui/wasm \
        -DFSUI_BUILD_SAMPLES=OFF \
        -DFSUI_PLATFORM_BACKEND=SDL2
    cmake --build build/fsui/wasm -j"${BUILD_JOBS}"
}

bundle_macos_app() {
    mkdir -p armsx.app/Contents/MacOS/Libraries
    cp bin/armsx armsx.app/Contents/MacOS
    chmod 777 armsx.app/Contents/MacOS/armsx
    dylibbundler -b -x ./armsx.app/Contents/MacOS/armsx -d ./armsx.app/Contents/Libraries/ -p @executable_path/../Libraries/ -cd
    cp Info.plist armsx.app/Contents/Info.plist
}

prepare_ios_sdl_package() {
    IOS_SDL_FRAMEWORK="$1"
    IOS_SDL_PACKAGE_ROOT="$(pwd)/build/fsui/ios-sdl-package/SDL2.framework"

    rm -rf "${IOS_SDL_PACKAGE_ROOT}"
    mkdir -p "${IOS_SDL_PACKAGE_ROOT}/Versions/A/Resources/CMake"
    mkdir -p "${IOS_SDL_PACKAGE_ROOT}/Versions/A"
    ln -s "${IOS_SDL_FRAMEWORK}/Headers" "${IOS_SDL_PACKAGE_ROOT}/Headers"
    ln -s "${IOS_SDL_FRAMEWORK}/Headers" "${IOS_SDL_PACKAGE_ROOT}/Versions/A/Headers"
    ln -s "${IOS_SDL_FRAMEWORK}/SDL2" "${IOS_SDL_PACKAGE_ROOT}/Versions/A/SDL2"
    cp "${IOS_SDL_FRAMEWORK}/CMake/sdl2-config.cmake" "${IOS_SDL_PACKAGE_ROOT}/Versions/A/Resources/CMake/"
    cp "${IOS_SDL_FRAMEWORK}/CMake/sdl2-config-version.cmake" "${IOS_SDL_PACKAGE_ROOT}/Versions/A/Resources/CMake/"

    printf '%s\n' "${IOS_SDL_PACKAGE_ROOT}/Versions/A/Resources/CMake"
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

    IOS_SDL2_DIR="$(prepare_ios_sdl_package "${IOS_SDL_FRAMEWORK}")"

    echo "Building iOS dylib with SDK ${IOS_SDK} (${IOS_SDKROOT})"
    cmake -S third_party/fsui-lib -B build/fsui/ios \
        -DFSUI_BUILD_SAMPLES=OFF \
        -DFSUI_PLATFORM_BACKEND=SDL2 \
        -DFSUI_USE_SYSTEM_SDL2=ON \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET}" \
        -DCMAKE_OSX_SYSROOT="${IOS_SDKROOT}" \
        -DSDL2_DIR="${IOS_SDL2_DIR}"
    cmake --build build/fsui/ios -j"${BUILD_JOBS}"
    make clean
    IOS_ENV="IOS_TARGET=1 IOS_SDK=${IOS_SDK} IOS_DEPLOYMENT_TARGET=${IOS_DEPLOYMENT_TARGET} SDKROOT=${IOS_SDKROOT} CC=${IOS_CC} CXX=${IOS_CXX} SDL_STATIC=0 IOS_SDL_FRAMEWORK=${IOS_SDL_FRAMEWORK} FSUI_BUILD_DIR=$(pwd)/build/fsui/ios"
    eval "make shared ${IOS_ENV}"
    echo "Copying libarmsx.dylib to ios/Frameworks/"
    cp bin/libarmsx.dylib ios/Frameworks/

elif [ "$MODE" = "shared" ]; then
    build_fsui_native
    make clean
    SDL_STATIC=0 MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-10.15}" FSUI_BUILD_DIR="$(pwd)/build/fsui/native" make shared

elif [ "$MODE" = "android" ]; then
    ANDROID_NDK_ROOT="${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-${NDK_HOME:-}}}"
    if [ -z "${ANDROID_NDK_ROOT}" ]; then
        echo "ANDROID_NDK_ROOT (or ANDROID_NDK_HOME / NDK_HOME) must be set to a valid NDK path."
        exit 1
    fi
    if [ ! -d "${ANDROID_NDK_ROOT}" ]; then
        echo "Android NDK path ${ANDROID_NDK_ROOT} does not exist."
        exit 1
    fi

    ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"
    ANDROID_API="${ANDROID_API:-26}"
    ANDROID_PLATFORM="android-${ANDROID_API}"

    case "${ANDROID_ABI}" in
        arm64-v8a)
            ANDROID_TRIPLE="aarch64-linux-android"
            ;;
        armeabi-v7a)
            ANDROID_TRIPLE="armv7a-linux-androideabi"
            ;;
        x86_64)
            ANDROID_TRIPLE="x86_64-linux-android"
            ;;
        *)
            echo "Unsupported ANDROID_ABI '${ANDROID_ABI}'."
            exit 1
            ;;
    esac

    HOST_OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
    HOST_ARCH="$(uname -m)"
    case "${HOST_ARCH}" in
        arm64|aarch64)
            HOST_ARCH_TAG="arm64"
            ;;
        x86_64)
            HOST_ARCH_TAG="x86_64"
            ;;
        *)
            HOST_ARCH_TAG="${HOST_ARCH}"
            ;;
    esac

    HOST_TAG="${HOST_OS}-${HOST_ARCH_TAG}"
    TOOLCHAIN_DIR="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${HOST_TAG}"
    if [ ! -d "${TOOLCHAIN_DIR}" ]; then
        # Fallback to the first available prebuilt toolchain
        TOOLCHAIN_DIR="$(ls -d "${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/"* 2>/dev/null | head -n 1)"
        if [ -z "${TOOLCHAIN_DIR}" ]; then
            echo "Unable to locate LLVM toolchain inside ${ANDROID_NDK_ROOT}."
            exit 1
        fi
        HOST_TAG="$(basename "${TOOLCHAIN_DIR}")"
    fi

    echo "Using Android NDK at ${ANDROID_NDK_ROOT} (toolchain ${HOST_TAG}, ABI ${ANDROID_ABI}, API ${ANDROID_API})"

    SDL_BUILD_ROOT="build/android/sdl"
    SDL_INSTALL_DIR="${SDL_BUILD_ROOT}/install"
    mkdir -p "${SDL_BUILD_ROOT}"

    cmake -S third_party/SDL -B "${SDL_BUILD_ROOT}" \
        -DANDROID=ON \
        -DANDROID_ABI="${ANDROID_ABI}" \
        -DANDROID_PLATFORM="${ANDROID_PLATFORM}" \
        -DANDROID_STL=c++_shared \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_ANDROID_NDK="${ANDROID_NDK_ROOT}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TEST=OFF \
        -DCMAKE_INSTALL_PREFIX="${SDL_INSTALL_DIR}"

    cmake --build "${SDL_BUILD_ROOT}" --config Release
    cmake --install "${SDL_BUILD_ROOT}" --config Release

    SDL_LIB_PATH="${SDL_INSTALL_DIR}/lib"
    SDL_INCLUDE_PATH="${SDL_INSTALL_DIR}/include/SDL2"
    SDL_SHARED_LIB="${SDL_LIB_PATH}/libSDL2.so"
    if [ ! -f "${SDL_SHARED_LIB}" ]; then
        echo "SDL shared library not found at ${SDL_SHARED_LIB}"
        exit 1
    fi

    JNI_LIB_DIR="android/app/src/main/jniLibs/${ANDROID_ABI}"
    SDL_HEADER_STAGE="android/native-deps/SDL2/include"
    mkdir -p "${JNI_LIB_DIR}"
    rm -rf "${SDL_HEADER_STAGE}"
    mkdir -p "${SDL_HEADER_STAGE}"
    cp -R "${SDL_INCLUDE_PATH}/." "${SDL_HEADER_STAGE}/"

    TOOLCHAIN_BIN="${TOOLCHAIN_DIR}/bin"
    CC="${TOOLCHAIN_BIN}/${ANDROID_TRIPLE}${ANDROID_API}-clang"
    CXX="${TOOLCHAIN_BIN}/${ANDROID_TRIPLE}${ANDROID_API}-clang++"
    export CC
    export CXX

    SDL_CFLAGS="-D_REENTRANT -DANDROID -I${SDL_INCLUDE_PATH}"
    SDL_LIBS="-L${SDL_LIB_PATH} -lSDL2 -llog -landroid -lGLESv3 -lEGL -lOpenSLES -lm -lc++_shared"

    FSUI_BUILD_ROOT="build/fsui/android/${ANDROID_ABI}"
    cmake -S third_party/fsui-lib -B "${FSUI_BUILD_ROOT}" \
        -DFSUI_BUILD_SAMPLES=OFF \
        -DFSUI_PLATFORM_BACKEND=SDL2 \
        -DFSUI_USE_SYSTEM_SDL2=ON \
        -DANDROID=ON \
        -DANDROID_ABI="${ANDROID_ABI}" \
        -DANDROID_PLATFORM="${ANDROID_PLATFORM}" \
        -DANDROID_STL=c++_shared \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_ANDROID_NDK="${ANDROID_NDK_ROOT}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="${SDL_INSTALL_DIR}"
    cmake --build "${FSUI_BUILD_ROOT}" --config Release -j"${BUILD_JOBS}"

    make clean
    SDL_STATIC=0 SDL_CFLAGS="${SDL_CFLAGS}" SDL_LIBS_DYNAMIC="${SDL_LIBS}" FSUI_BUILD_DIR="$(pwd)/${FSUI_BUILD_ROOT}" make shared

    cp "${SDL_SHARED_LIB}" "${JNI_LIB_DIR}/"
    cp bin/libarmsx.so "${JNI_LIB_DIR}/"

elif [ "$MODE" = "wasm" ]; then
    build_fsui_wasm
    make clean
    if command -v emmake >/dev/null 2>&1; then
        emmake make wasm FSUI_BUILD_DIR="$(pwd)/build/fsui/wasm"
    else
        make wasm FSUI_BUILD_DIR="$(pwd)/build/fsui/wasm"
    fi

elif [ "$MODE" = "macosapp" ]; then
    build_fsui_native
    make clean
    SDL_STATIC=0 MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-10.15}" FSUI_BUILD_DIR="$(pwd)/build/fsui/native" make
    bundle_macos_app

else
    build_fsui_native
    make clean
    SDL_STATIC=0 MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-10.15}" FSUI_BUILD_DIR="$(pwd)/build/fsui/native" make
fi
