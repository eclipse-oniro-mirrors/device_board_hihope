#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# Build rpmsg_demo (master-side app-developer example) with OH clang + musl sysroot.
# Source: example/rpmsg_demo.c (this repo).
# Deps: device/soc/rockchip/rk3568/mcs -> libmica (headers in libmica/include,
#       libs from its scripts/build_libmica.sh output; run it once) and prebuilt
#       open-amp/libmetal .so (default under $CODE/device/soc/rockchip; SOC_AMP overrides).
# CODE = OpenHarmony full source tree (OH clang + musl sysroot), autodetected by
# walking up from this script (tree root has prebuilts/); override: export CODE=...
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

for _t in clang llvm-readelf; do
  [ -x "$LLVMBIN/$_t" ] || { echo "ERROR: $_t not found at $LLVMBIN"; \
    echo "       repo sync prebuilts/clang in your OH tree (or fix CODE)"; exit 1; }
done
CLANG=$LLVMBIN/clang
# musl sysroot: autodetected from the built products (override: export MUSL=...)
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
for DEMO in rpmsg_demo netproxy_demo; do
  $CLANG $TFLAGS $INC -O2 -Wall -Wl,--dynamic-linker=/data/lib/ld-musl-aarch64.so.1 \
    -o $OUT/artifacts/$DEMO $ROOT/example/$DEMO.c \
    $LIB -lmica -l:libmetal.so.1 -l:libopen_amp.so.1 -lmica_compat -lpthread
done

echo BUILD_OK
for DEMO in rpmsg_demo netproxy_demo; do
  $LLVMBIN/llvm-readelf -h $OUT/artifacts/$DEMO | grep -E "Class:|Machine:"
done
echo "-- deploy set --"
echo "   $OUT/artifacts/{rpmsg_demo,netproxy_demo} -> /data; $SOC/out/artifacts/lib/* -> /data/lib (from build_libmica.sh)"
