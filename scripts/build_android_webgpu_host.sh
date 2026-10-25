#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: bash test_android_wgpu_surface.sh /path/to/TMB-Android-Port" >&2
  exit 2
fi

PROJECT_ROOT="$(cd "$1" && pwd)"
CACHE="$HOME/Library/Caches/tmb-ps2-web-recomp-android"
UPSTREAM="$CACHE/upstream"
WGPU="$UPSTREAM/third_party/wgpu-native"
SDL_PREFIX="$CACHE/sdl2-android-arm64-16k"
BUILD="$UPSTREAM/build/android-arm64-runtime"
STUB="$CACHE/android-host-smoke-stub"
MODULE="$PROJECT_ROOT/android-host-smoke"
NDK="$HOME/Library/Android/sdk/ndk/27.0.12077973"
JAVA17="$(brew --prefix openjdk@17)/libexec/openjdk.jdk/Contents/Home"
GRADLE="$CACHE/gradle-8.9/bin/gradle"

for f in \
  "$NDK/build/cmake/android.toolchain.cmake" \
  "$UPSTREAM/CMakeLists.txt" \
  "$WGPU/lib/libwgpu_native.a" \
  "$SDL_PREFIX/lib/libSDL2.so" \
  "$STUB/registration_stub.cpp" \
  "$MODULE/build.gradle.kts" \
  "$JAVA17/bin/java" \
  "$GRADLE"; do
  if [[ ! -e "$f" ]]; then
    echo "Required item missing: $f" >&2
    exit 2
  fi
done

SDL2_CONFIG="$(find "$SDL_PREFIX" -type f \( -iname 'SDL2Config.cmake' -o -iname 'sdl2-config.cmake' \) -print -quit)"
if [[ -z "$SDL2_CONFIG" ]]; then
  echo "SDL2 CMake package missing under $SDL_PREFIX" >&2
  exit 2
fi
SDL2_DIR="${SDL2_CONFIG%/*}"

# This probe replaces the visible SDL_Renderer smoke screen with a short,
# log-driven test of the actual PS2 WebGPU runtime and Android ANativeWindow path.
cat > "$UPSTREAM/host/android_entry.cpp" <<'CPP'
#define main ps2_host_main_impl
#include "main.cpp"
#undef main

#include <SDL.h>
#include <SDL_syswm.h>
#include <ps2/system.hpp>
#include "gpu.hpp"

#include <cstdint>

namespace {
int runGpuSurfaceSmoke() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("TMB_WGPU_SURFACE_SMOKE: SDL_INIT_FAIL: %s", SDL_GetError());
        return 10;
    }
    SDL_Window* window = SDL_CreateWindow(
        "TMB WebGPU surface smoke test", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 960, 540,
        SDL_WINDOW_VULKAN | SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        SDL_Log("TMB_WGPU_SURFACE_SMOKE: WINDOW_FAIL: %s", SDL_GetError());
        SDL_Quit();
        return 11;
    }

    SDL_version sdl_version;
    SDL_GetVersion(&sdl_version);
    SDL_Log("TMB_SDL_DIAG: version=%u.%u.%u flags=0x%08x Vulkan=0x%08x OpenGL=0x%08x",
        sdl_version.major, sdl_version.minor, sdl_version.patch,
        (unsigned int)SDL_GetWindowFlags(window),
        (unsigned int)SDL_WINDOW_VULKAN,
        (unsigned int)SDL_WINDOW_OPENGL);
    SDL_Log("TMB_WGPU_SURFACE_SMOKE: INIT_PS2_GRAPHICS_BEGIN");
    ps2::init_graphics();

    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) {
        SDL_Log("TMB_WGPU_SURFACE_SMOKE: SYSWM_FAIL: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 12;
    }
    if (info.subsystem != SDL_SYSWM_ANDROID) {
        SDL_Log("TMB_WGPU_SURFACE_SMOKE: WRONG_WINDOW_SUBSYSTEM: %d", static_cast<int>(info.subsystem));
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 13;
    }

    ps2::WindowTarget target{};
    target.kind = ps2::WindowTarget::Android;
    target.window = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(info.info.android.window));
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    target.width = static_cast<std::uint32_t>(width > 0 ? width : 960);
    target.height = static_cast<std::uint32_t>(height > 0 ? height : 540);

    if (!ps2::attach_window(target)) {
        SDL_Log("TMB_WGPU_SURFACE_SMOKE: ATTACH_FAIL: WebGPU device or Android native surface was rejected");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 14;
    }
    SDL_Log("TMB_WGPU_SURFACE_SMOKE: ATTACH_PASS: WebGPU surface created on Android ANativeWindow");

    unsigned int frames = 0;
    bool running = true;
    bool present_ok = true;
    for (; frames < 300 && running; ++frames) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || event.type == SDL_APP_TERMINATING) running = false;
            if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_AC_BACK)) running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                int w = 0, h = 0;
                SDL_GetWindowSizeInPixels(window, &w, &h);
                ps2::resize_window(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
            }
        }
        if (!running) break;
        // Drive the PS2 GS/WebGPU display path using a small synthetic framebuffer.
        // No copyrighted game code or game ISO is involved in this test.
        present_ok = ps2::gs::gpu::present({0, 5, 0, 0, 0, 320, 240, false});
        if (!present_ok) break;
        SDL_Delay(16);
    }

    SDL_Log("TMB_WGPU_SURFACE_SMOKE: PRESENT_%s: frames=%u", present_ok ? "PASS" : "FAIL", frames);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return present_ok && frames > 0 ? 0 : 15;
}
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
    if (argc > 1) return ps2_host_main_impl(argc, argv);
    return runGpuSurfaceSmoke();
}
CPP

python3 - "$UPSTREAM/CMakeLists.txt" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
old = '''            add_library(main SHARED host/android_entry.cpp host/window.cpp)
            target_compile_definitions(main PRIVATE PS2_WINDOW)
            target_link_libraries(main PRIVATE ps2game ps2runtime SDL2::SDL2)'''
new = '''            add_library(main SHARED host/android_entry.cpp host/window.cpp)
            target_compile_definitions(main PRIVATE PS2_WINDOW)
            target_include_directories(main PRIVATE "${CMAKE_SOURCE_DIR}/runtime/src/gs/gpu")
            target_link_libraries(main PRIVATE ps2game ps2runtime SDL2::SDL2)'''
if old not in s:
    if new in s:
        print("GPU smoke include path already configured")
    else:
        raise SystemExit("Android main target anchor not found; no CMake change made")
else:
    p.write_text(s.replace(old, new))
    print("Added private GPU header path for the surface probe")
PY

echo "== Configure Android ARM64 WebGPU surface probe"
cmake -S "$UPSTREAM" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384" \
  -DPS2_WGPU_DIR="$WGPU" \
  -DPS2_GENERATED="$STUB" \
  -DSDL2_DIR="$SDL2_DIR" \
  -DCMAKE_PREFIX_PATH="$SDL_PREFIX"

cmake --build "$BUILD" --target main --parallel 4

mkdir -p "$MODULE/src/main/jniLibs/arm64-v8a"
cp "$BUILD/libmain.so" "$MODULE/src/main/jniLibs/arm64-v8a/libmain.so"
cp "$SDL_PREFIX/lib/libSDL2.so" "$MODULE/src/main/jniLibs/arm64-v8a/libSDL2.so"

export JAVA_HOME="$JAVA17"
export PATH="$CACHE/gradle-8.9/bin:$JAVA_HOME/bin:$HOME/Library/Android/sdk/platform-tools:$PATH"

echo "== Build WebGPU surface diagnostic APK"
cd "$PROJECT_ROOT"
gradle --no-daemon :android-host-smoke:assembleDebug
APK="$MODULE/build/outputs/apk/debug/android-host-smoke-debug.apk"
test -f "$APK"
unzip -l "$APK" | grep -F 'lib/arm64-v8a/libmain.so' >/dev/null
echo
echo "== WGPU SURFACE PROBE APK BUILT"
echo "$APK"
echo "This variant intentionally runs a short log-driven GPU test and may show a blank/black surface."
