#!/bin/sh

# Build options:
#   BUILD_DLL=1       -> build shared library (libpsxe.* with __DLL_BUILD)
#   BUILD_DLL_ONLY=1  -> build only the shared library
#   BUNDLE_APP=1      -> bundle macOS .app (uses executable build)

make clean

if [ "${BUILD_DLL_ONLY:-0}" != "0" ]; then
    make shared
else
    make
    if [ "${BUILD_DLL:-0}" != "0" ]; then
        make shared
    fi

    if [ "${BUNDLE_APP:-0}" != "0" ]; then
        # Create bundle filesystem
        mkdir -p psxe.app/Contents/MacOS/Libraries

        # Copy executable to bundle folder (keep bin/psxe intact)
        cp bin/psxe psxe.app/Contents/MacOS

        # Make executable
        chmod 777 psxe.app/Contents/MacOS/psxe

        # Bundle required dylibs
        dylibbundler -b -x ./psxe.app/Contents/MacOS/psxe -d ./psxe.app/Contents/Libraries/ -p @executable_path/../Libraries/ -cd

        # Copy plist to Contents folder (keep a copy in repo for subsequent builds)
        cp Info.plist psxe.app/Contents/Info.plist
    else
        echo "Skipping macOS .app bundle. Set BUNDLE_APP=1 to build the application bundle."
    fi
fi
