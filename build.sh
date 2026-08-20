#!/bin/sh

set -e

# Usage:
#   ./build.sh             -> desktop build (static SDL if available)
#   ./build.sh shared      -> build shared lib (forces dynamic SDL)
#   ./build.sh macosapp    -> build desktop exe and bundle armsx.app
#   ./build.sh android     -> build SDL2/libarmsx for Android and stage under android/app/src/main/jniLibs

MODE="$1"
BUILD_JOBS="${BUILD_JOBS:-4}"
export USE_CHD="${USE_CHD:-1}"

resolve_fsui_python() {
    if [ -n "${FSUI_PYTHON_EXECUTABLE:-}" ]; then
        if [ ! -x "${FSUI_PYTHON_EXECUTABLE}" ] ||
            ! "${FSUI_PYTHON_EXECUTABLE}" -c "import jinja2" >/dev/null 2>&1; then
            echo "FSUI_PYTHON_EXECUTABLE must name an executable Python with jinja2 installed." >&2
            return 1
        fi
        printf '%s\n' "${FSUI_PYTHON_EXECUTABLE}"
        return 0
    fi

    for candidate_name in python3 python; do
        candidate="$(command -v "${candidate_name}" 2>/dev/null || true)"
        if [ -n "${candidate}" ] &&
            "${candidate}" -c "import jinja2" >/dev/null 2>&1; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    echo "fsui requires a Python with jinja2. Set FSUI_PYTHON_EXECUTABLE to an existing interpreter." >&2
    return 1
}

build_fsui_native() {
    if [ "$(uname -s)" = "Darwin" ]; then
        MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-10.15}"
        cmake -S third_party/fuse-lib -B build/fsui/native \
            -DFSUI_BUILD_SAMPLES=OFF \
            -DFSUI_PLATFORM_BACKEND=SDL2 \
            -DFSUI_USE_SYSTEM_SDL2=ON \
            -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET}"
    else
        cmake -S third_party/fuse-lib -B build/fsui/native \
            -DFSUI_BUILD_SAMPLES=OFF \
            -DFSUI_PLATFORM_BACKEND=SDL2 \
            -DFSUI_USE_SYSTEM_SDL2=ON
    fi
    cmake --build build/fsui/native -j"${BUILD_JOBS}"
}

bundle_macos_app() {
		    rm -rf armsx.app/Contents/Libraries
		    mkdir -p armsx.app/Contents/MacOS
		    mkdir -p armsx.app/Contents/Resources/icons
		    cp bin/armsx armsx.app/Contents/MacOS
		    cp icons/ArmsxDesktop.icns armsx.app/Contents/Resources/armsx.icns
		    cp -R icons/. armsx.app/Contents/Resources/icons/
		    chmod 777 armsx.app/Contents/MacOS/armsx
		    dylibbundler -b -x ./armsx.app/Contents/MacOS/armsx -d ./armsx.app/Contents/Libraries/ -p @executable_path/../Libraries/ -cd
		    cp Info.plist armsx.app/Contents/Info.plist
}

stage_android_runtime_icons() {
	    mkdir -p android/app/src/main/assets/icons
	    cp -R icons/. android/app/src/main/assets/icons/
}

if [ "$MODE" = "shared" ]; then
    build_fsui_native
    make clean
    SDL_STATIC=0 MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-10.15}" FSUI_BUILD_DIR="$(pwd)/build/fsui/native" LIBCHDR_BUILD_DIR="$(pwd)/build/libchdr/native" make shared

elif [ "$MODE" = "android" ]; then
	    stage_android_runtime_icons
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
            ANDROID_CXX_RUNTIME_TRIPLE="aarch64-linux-android"
            ;;
        armeabi-v7a)
            ANDROID_TRIPLE="armv7a-linux-androideabi"
            ANDROID_CXX_RUNTIME_TRIPLE="arm-linux-androideabi"
            ;;
        x86)
            ANDROID_TRIPLE="i686-linux-android"
            ANDROID_CXX_RUNTIME_TRIPLE="i686-linux-android"
            ;;
        x86_64)
            ANDROID_TRIPLE="x86_64-linux-android"
            ANDROID_CXX_RUNTIME_TRIPLE="x86_64-linux-android"
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

    ANDROID_TOOLCHAIN_FILE="${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake"
    if [ ! -f "${ANDROID_TOOLCHAIN_FILE}" ]; then
        echo "Android toolchain file not found at ${ANDROID_TOOLCHAIN_FILE}"
        exit 1
    fi

    REPO_ROOT="$(pwd)"

    SDL_BUILD_ROOT="${REPO_ROOT}/build/android/sdl/${ANDROID_ABI}"
    SDL_INSTALL_DIR="${SDL_BUILD_ROOT}/install"
    rm -rf "${SDL_BUILD_ROOT}"
    mkdir -p "${SDL_BUILD_ROOT}"

    cmake -S third_party/SDL -B "${SDL_BUILD_ROOT}" \
        -DCMAKE_TOOLCHAIN_FILE="${ANDROID_TOOLCHAIN_FILE}" \
        -DANDROID_ABI="${ANDROID_ABI}" \
        -DANDROID_PLATFORM="${ANDROID_PLATFORM}" \
        -DANDROID_STL=c++_shared \
        -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DSDL_STATIC=OFF \
        -DSDL_TEST=OFF \
        -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-z,common-page-size=16384" \
        -DCMAKE_INSTALL_PREFIX="${SDL_INSTALL_DIR}"

    cmake --build "${SDL_BUILD_ROOT}" --config Release
    cmake --install "${SDL_BUILD_ROOT}" --config Release

    SDL_LIB_PATH="${SDL_INSTALL_DIR}/lib"
    SDL_INCLUDE_PATH="${SDL_INSTALL_DIR}/include/SDL2"
    SDL_SHARED_LIB="${SDL_LIB_PATH}/libSDL2.so"
    CXX_SHARED_RUNTIME="${TOOLCHAIN_DIR}/sysroot/usr/lib/${ANDROID_CXX_RUNTIME_TRIPLE}/libc++_shared.so"
    if [ ! -f "${SDL_SHARED_LIB}" ]; then
        echo "SDL shared library not found at ${SDL_SHARED_LIB}"
        exit 1
    fi
    if [ ! -f "${CXX_SHARED_RUNTIME}" ]; then
        echo "Android C++ shared runtime not found at ${CXX_SHARED_RUNTIME}"
        exit 1
    fi

    JNI_LIB_DIR="${REPO_ROOT}/android/app/src/main/jniLibs/${ANDROID_ABI}"
    SDL_HEADER_STAGE="${REPO_ROOT}/android/native-deps/SDL2/include"
    mkdir -p "${JNI_LIB_DIR}"
    rm -rf "${SDL_HEADER_STAGE}"
    mkdir -p "${SDL_HEADER_STAGE}"
    cp -R "${SDL_INCLUDE_PATH}/." "${SDL_HEADER_STAGE}/"

    TOOLCHAIN_BIN="${TOOLCHAIN_DIR}/bin"
    CC="${TOOLCHAIN_BIN}/${ANDROID_TRIPLE}${ANDROID_API}-clang"
    CXX="${TOOLCHAIN_BIN}/${ANDROID_TRIPLE}${ANDROID_API}-clang++"
    AR="${TOOLCHAIN_BIN}/llvm-ar"
    RANLIB="${TOOLCHAIN_BIN}/llvm-ranlib"
    export CC
    export CXX
    export AR
    export RANLIB

    SDL_CFLAGS="-D_REENTRANT -DANDROID -D__BIONIC_NO_PAGE_SIZE_MACRO -I${SDL_INCLUDE_PATH}"
    SDL_LIBS="-L${SDL_LIB_PATH} -lSDL2 -llog -landroid -lGLESv3 -lEGL -lOpenSLES -lm -lc++_shared"
    ANDROID_FLEXIBLE_PAGE_LDFLAGS="-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384"

    FSUI_BUILD_ROOT="${REPO_ROOT}/build/fsui/android/${ANDROID_ABI}"
    FSUI_PYTHON="$(resolve_fsui_python)"
    cmake -S third_party/fuse-lib -B "${FSUI_BUILD_ROOT}" \
        -DFSUI_BUILD_SAMPLES=OFF \
        -DFSUI_PLATFORM_BACKEND=SDL2 \
        -DFSUI_USE_SYSTEM_SDL2=ON \
        -DFSUI_IMGUI_OPENGL_ES3=ON \
        -DPython_EXECUTABLE="${FSUI_PYTHON}" \
        -DCMAKE_TOOLCHAIN_FILE="${ANDROID_TOOLCHAIN_FILE}" \
        -DANDROID_ABI="${ANDROID_ABI}" \
        -DANDROID_PLATFORM="${ANDROID_PLATFORM}" \
        -DANDROID_STL=c++_shared \
        -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="${SDL_INSTALL_DIR}" \
        -DSDL2_DIR="${SDL_INSTALL_DIR}/lib/cmake/SDL2"
    cmake --build "${FSUI_BUILD_ROOT}" --config Release -j"${BUILD_JOBS}"

    make clean
    make \
        SDL_STATIC=0 \
        SDL_CFLAGS="${SDL_CFLAGS}" \
        SDL_LIBS_DYNAMIC="${SDL_LIBS}" \
        FSUI_BUILD_DIR="${FSUI_BUILD_ROOT}" \
        LIBCHDR_BUILD_DIR="${REPO_ROOT}/build/libchdr/android/${ANDROID_ABI}" \
        LIBCHDR_CMAKE_TOOLCHAIN_FILE="${ANDROID_TOOLCHAIN_FILE}" \
        LIBCHDR_ANDROID_ABI="${ANDROID_ABI}" \
        LIBCHDR_ANDROID_PLATFORM="${ANDROID_PLATFORM}" \
        LIBCHDR_ANDROID_FLEXIBLE_PAGE_SIZES=ON \
        PLATFORM=Android \
        OS_INFO=Android \
        PLATFORM_EXTRA_LDFLAGS="${ANDROID_FLEXIBLE_PAGE_LDFLAGS}" \
        PLATFORM_EXTRA_LIBS= \
        shared

    cp "${SDL_SHARED_LIB}" "${JNI_LIB_DIR}/"
    cp "${CXX_SHARED_RUNTIME}" "${JNI_LIB_DIR}/"
    cp bin/libarmsx.so "${JNI_LIB_DIR}/"

elif [ "$MODE" = "macosapp" ]; then
    build_fsui_native
    make clean
    SDL_STATIC=0 MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-10.15}" FSUI_BUILD_DIR="$(pwd)/build/fsui/native" LIBCHDR_BUILD_DIR="$(pwd)/build/libchdr/native" make
    bundle_macos_app

else
    build_fsui_native
    make clean
    SDL_STATIC=0 MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-10.15}" FSUI_BUILD_DIR="$(pwd)/build/fsui/native" LIBCHDR_BUILD_DIR="$(pwd)/build/libchdr/native" make
fi
