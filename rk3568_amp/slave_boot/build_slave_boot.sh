#!/usr/bin/env bash
# Build slave_boot (master-side minimal slave-core launcher, no rpmsg) with OH
# clang + musl sysroot. Source: slave_boot/slave_boot.c (this repo).
# Deps: device/soc/rockchip/rk3568/mcs -> libmica (headers in libmica/include,
#       libs from its scripts/build_libmica.sh output; run it once) and prebuilt
#       open-amp/libmetal .so (default under $CODE/device/soc/rockchip; SOC_AMP overrides).
set -eu
CODE=${CODE:-}
if [ -z "$CODE" ]; then
  _d=$(cd "$(dirname "$0")" && pwd)
  while [ "$_d" != "/" ]; do
    if [ -d "$_d/prebuilts/clang" ]; then CODE=$_d; break; fi
    _d=$(dirname "$_d")
  done
fi
CODE=${CODE:?ERROR: export CODE=<OpenHarmony full source tree root>}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SOC=${SOC_AMP:-}
[ -n "$SOC" ] || SOC=$CODE/device/soc/rockchip/rk3568/mcs
[ -d "$SOC/libmica/include" ] && [ -d "$SOC/prebuilt/lib" ] || {
  echo "ERROR: rk3568_amp architecture repo not found at $SOC"; \
  echo "       export SOC_AMP=<path to device/soc/rockchip/rk3568/mcs>"; exit 1; }
[ -f "$SOC/out/staging/lib/libmica.so" ] || {
  echo "ERROR: libmica.so not found at $SOC/out/staging/lib"; \
  echo "       run $SOC/scripts/build_libmica.sh first (builds libmica from source)"; exit 1; }

OUT=${OUT:-$ROOT/out}
LLVMBIN=$CODE/prebuilts/clang/ohos/linux-x86_64/llvm/bin
CLANG=$LLVMBIN/clang
MUSL=${MUSL:-}
for _d in $CODE/out/*/obj/third_party/musl/usr; do
  [ -f "$_d/lib/aarch64-linux-ohos/crt1.o" ] || continue
  MUSL=$_d; break
done
[ -n "$MUSL" ] || { echo "ERROR: no usable musl sysroot under $CODE/out/*/obj/third_party/musl/usr"; \
  echo "       build the OH product once, or export MUSL=<sysroot>"; exit 1; }
echo "musl sysroot: $MUSL"

INC="-I$SOC/libmica/include -I$SOC/prebuilt/include"
LIB="-L$SOC/out/staging/lib -L$SOC/prebuilt/lib"
TFLAGS="--target=aarch64-linux-ohos --sysroot=$MUSL -I$MUSL/include/aarch64-linux-ohos -B$MUSL/lib/aarch64-linux-ohos -L$MUSL/lib/aarch64-linux-ohos"

mkdir -p $OUT/artifacts
$CLANG $TFLAGS $INC -O2 -Wall -Wl,--dynamic-linker=/data/lib/ld-musl-aarch64.so.1 \
  -o $OUT/artifacts/slave_boot $ROOT/slave_boot/slave_boot.c \
  $LIB -lmica -l:libmetal.so.1 -l:libopen_amp.so.1 -lmica_compat -lpthread

echo BUILD_OK
$LLVMBIN/llvm-readelf -h $OUT/artifacts/slave_boot | grep -E "Class:|Machine:"
echo "-- undefined musl-risky symbols --"
$LLVMBIN/llvm-nm --undefined-only $OUT/artifacts/slave_boot | grep -E "strlcpy|strlcat" || echo "none"
