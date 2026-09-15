#!/usr/bin/env bash
# ============================================================================
#  CAR_REMOTE firmware build (Linux / macOS / CI)
#  Same settings as build.ps1 and MounRiver's obj/Release configuration.
#
#  Usage:  bash build.sh
#  Output: build/CAR_REMOTE.elf / .hex / .bin / .map
#
#  Toolchain: riscv-none-embed-gcc (WCH ships 8.2.0; CI uses xpack 8.3.0)
#             override with CC=/path/to/riscv-none-embed-gcc
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_ROOT="${CH32V307_SDK:-$ROOT/sdk}"
BUILD="$ROOT/build"
TARGET=CAR_REMOTE

# SDK 目录里必须能直接看到 Peripheral/ ...
if [ ! -d "$SRC_ROOT/Peripheral/src" ]; then
    if [ -d "$SRC_ROOT/SRC/Peripheral/src" ]; then
        SRC_ROOT="$SRC_ROOT/SRC"
    else
        echo "SDK not found at '$SRC_ROOT' (need Core / Peripheral / Ld / Startup)" >&2
        exit 1
    fi
fi

CC="${CC:-riscv-none-embed-gcc}"
if ! command -v "$CC" >/dev/null 2>&1; then
    echo "toolchain '$CC' not found in PATH" >&2
    exit 1
fi
OBJCOPY="${OBJCOPY:-${CC%gcc}objcopy}"
SIZE="${SIZE:-${CC%gcc}size}"

ARCH=(-march=rv32i -mabi=ilp32)
INCS=(-I"$ROOT/User" -I"$ROOT/Bsp" -I"$ROOT/App"
      -I"$SRC_ROOT/Core" -I"$SRC_ROOT/Peripheral/inc")
CFLAGS=("${ARCH[@]}" -Os -g -std=gnu99 -fsigned-char
        -ffunction-sections -fdata-sections -fno-common
        -Wall -Wunused -Wuninitialized "${INCS[@]}")
LDFLAGS=("${ARCH[@]}" -nostartfiles --specs=nano.specs --specs=nosys.specs
         -Wl,--gc-sections "-Wl,-Map=$BUILD/$TARGET.map" -Wl,--print-memory-usage
         -T "$SRC_ROOT/Ld/Link.ld")

echo "SDK      : $SRC_ROOT"
echo "Toolchain: $("$CC" --version | head -n 1)"

mkdir -p "$BUILD"

SOURCES=()
while IFS= read -r f; do SOURCES+=("$f"); done < <(find "$SRC_ROOT/Peripheral/src" -name '*.c' | sort)
SOURCES+=("$SRC_ROOT/Core/core_riscv.c" "$SRC_ROOT/Startup/startup_ch32v30x_D8C.S")
while IFS= read -r f; do SOURCES+=("$f"); done < <(find "$ROOT/User" "$ROOT/Bsp" "$ROOT/App" -name '*.c' | sort)

OBJS=()
for src in "${SOURCES[@]}"; do
    obj="$BUILD/$(basename "${src%.*}").o"
    echo "CC   $(basename "$src")"
    "$CC" "${CFLAGS[@]}" -c "$src" -o "$obj"
    OBJS+=("$obj")
done

echo "LD   $TARGET.elf"
"$CC" "${LDFLAGS[@]}" "${OBJS[@]}" -o "$BUILD/$TARGET.elf"

"$OBJCOPY" -O ihex   "$BUILD/$TARGET.elf" "$BUILD/$TARGET.hex"
"$OBJCOPY" -O binary "$BUILD/$TARGET.elf" "$BUILD/$TARGET.bin"
"$SIZE" "$BUILD/$TARGET.elf"
echo "OK -> $BUILD/$TARGET.elf"
