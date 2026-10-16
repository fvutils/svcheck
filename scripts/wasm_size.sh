#!/usr/bin/env bash
# Builds svcheck for WebAssembly (WASI preview 1) and prints its size
# (svcheck-plan P1.7: note the WASM size, without a gate).
#
#   scripts/wasm_size.sh TOOLS_DIR BUILD_DIR
#
# Downloads wasi-sdk, binaryen (wasm-opt) and wasmtime into TOOLS_DIR when
# they are not there ($WASI_SDK names an existing wasi-sdk instead), builds
# the whole `svcheck` executable into BUILD_DIR, runs wasm-opt -Oz on it and
# prints the sizes: raw, stripped, -Oz, each also gzip -9. Then smoke-runs
# the -Oz module under wasmtime (`--version`, one lint).
#
# The build: no threads (SLANG_USE_THREADS=OFF), C++ exceptions on through
# wasm exception handling (-fwasm-exceptions, wasi-sdk's eh libc++ and
# libunwind), CMAKE_BUILD_TYPE=$BUILD_TYPE (MinSizeRel). The WASI fixes
# that are build settings are a CMake file injected with
# CMAKE_PROJECT_svcheck_INCLUDE, written into BUILD_DIR:
#  - slang's WASI branch adds -fno-exceptions; slang_slang gets them back.
#  - c4core (rapidyaml) knows wasm only as Emscripten: svcheck_engine is
#    built with __EMSCRIPTEN__ and __unix__ defined.
# LLVM emits the legacy EH encoding, which wasmtime does not run; wasm-opt
# translates it to exnref (--translate-to-exnref) in the same pass.
#
# Leaves BUILD_DIR/svcheck (raw) and BUILD_DIR/svcheck-oz.wasm.
set -euo pipefail

WASI_SDK_VER=${WASI_SDK_VER:-34}
BINARYEN_VER=${BINARYEN_VER:-133}
WASMTIME_VER=${WASMTIME_VER:-49.0.3}
BUILD_TYPE=${BUILD_TYPE:-MinSizeRel}

if [ $# -ne 2 ]; then
  echo "usage: $0 TOOLS_DIR BUILD_DIR" >&2
  exit 1
fi
mkdir -p "$1" "$2"
TOOLS=$(cd "$1" && pwd)
BUILD=$(cd "$2" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)

echo "=== tools ==="
if [ -z "${WASI_SDK:-}" ]; then
  WASI_SDK=$TOOLS/wasi-sdk-$WASI_SDK_VER.0-x86_64-linux
  [ -d "$WASI_SDK" ] || curl -sSfL \
    "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-$WASI_SDK_VER/wasi-sdk-$WASI_SDK_VER.0-x86_64-linux.tar.gz" \
    | tar xz -C "$TOOLS"
fi
BINARYEN=$TOOLS/binaryen-version_$BINARYEN_VER
[ -d "$BINARYEN" ] || curl -sSfL \
  "https://github.com/WebAssembly/binaryen/releases/download/version_$BINARYEN_VER/binaryen-version_$BINARYEN_VER-x86_64-linux.tar.gz" \
  | tar xz -C "$TOOLS"
WASMTIME=$TOOLS/wasmtime-v$WASMTIME_VER-x86_64-linux/wasmtime
[ -x "$WASMTIME" ] || curl -sSfL \
  "https://github.com/bytecodealliance/wasmtime/releases/download/v$WASMTIME_VER/wasmtime-v$WASMTIME_VER-x86_64-linux.tar.xz" \
  | tar xJ -C "$TOOLS"
"$WASI_SDK/bin/clang" --version | head -1

echo "=== build ($BUILD_TYPE) ==="
cat > "$BUILD/wasi-overrides.cmake" <<'EOF'
function(_svcheck_wasi_fixups)
  target_compile_options(slang_slang PRIVATE -fexceptions -fwasm-exceptions)
  target_compile_definitions(svcheck_engine PRIVATE __EMSCRIPTEN__ __unix__)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL _svcheck_wasi_fixups)
EOF
NINJA=$ROOT/packages/python/bin/ninja
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_TOOLCHAIN_FILE="$WASI_SDK/share/cmake/wasi-sdk-p1.cmake" \
  -DWASI_SDK_PREFIX="$WASI_SDK" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DSLANG_USE_THREADS=OFF \
  -DCMAKE_CXX_FLAGS="-fwasm-exceptions" \
  -DCMAKE_EXE_LINKER_FLAGS="-fwasm-exceptions -lunwind" \
  -DCMAKE_PROJECT_svcheck_INCLUDE="$BUILD/wasi-overrides.cmake" \
  -DPython3_EXECUTABLE="$ROOT/packages/python/bin/python" > "$BUILD/configure.log"
"$NINJA" -C "$BUILD" svcheck > "$BUILD/build.log" || { tail -40 "$BUILD/build.log"; exit 1; }

echo "=== sizes ==="
"$WASI_SDK/bin/llvm-strip" -o "$BUILD/svcheck-strip.wasm" "$BUILD/svcheck"
# From the raw module: its target_features section gives wasm-opt the features.
"$BINARYEN/bin/wasm-opt" --enable-reference-types --translate-to-exnref -Oz \
  --strip-debug --strip-producers "$BUILD/svcheck" -o "$BUILD/svcheck-oz.wasm"
for f in svcheck svcheck-strip.wasm svcheck-oz.wasm; do
  printf '%-20s %10d bytes  gzip -9 %10d bytes\n' "$f" \
    "$(stat -c %s "$BUILD/$f")" "$(gzip -9c "$BUILD/$f" | wc -c)"
done

echo "=== smoke (wasmtime) ==="
SMOKE=$BUILD/smoke
mkdir -p "$SMOKE"
cat > "$SMOKE/t.sv" <<'EOF'
module m(input logic [1:0] s, output logic y);
  always_comb begin
    case (s)
      2'd0: y = 0;
    endcase
  end
endmodule
EOF
echo t.sv > "$SMOKE/t.f"
cp "$ROOT/rules/spike/verible.yaml" "$SMOKE/"
"$WASMTIME" run -W exceptions=y "$BUILD/svcheck-oz.wasm" --version
(cd "$SMOKE" && "$WASMTIME" run -W exceptions=y --dir . "$BUILD/svcheck-oz.wasm" \
  --rules verible.yaml -f t.f)
