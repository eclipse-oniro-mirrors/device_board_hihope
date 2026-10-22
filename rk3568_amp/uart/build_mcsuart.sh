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
# Build mcs_uart.ko (out-of-tree kernel module) against the OH linux-6.6 tree.
# Same flow as build_mcscan.sh; see that script for CODE autodetect.
set -u
CODE=${CODE:-}
if [ -z "$CODE" ]; then
  _d=$(cd "$(dirname "$0")" && pwd)
  while [ "$_d" != "/" ]; do
    if [ -d "$_d/prebuilts/clang" ]; then CODE=$_d; break; fi
    _d=$(dirname "$_d")
  done
fi
CODE=${CODE:?ERROR: export CODE=<OpenHarmony full source tree root>}
# ROOT = <mcs>/mcs_uart when installed; standalone checkout works too.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$ROOT/out}
SRC=$CODE/out/kernel/src_tmp/linux-6.6
OBJ=$CODE/out/kernel/OBJ/linux-6.6
[ -x $CODE/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang ] || { echo "ERROR: clang not found"; exit 1; }
[ -d $SRC ] || { echo "ERROR: built kernel not found at $SRC"; exit 1; }
export PATH=$CODE/prebuilts/clang/ohos/linux-x86_64/llvm/bin:$PATH
mkdir -p $OUT/logs
cd $ROOT/uart
rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order
rm -rf .tmp_versions
make -C $SRC O=$OBJ M=$PWD ARCH=arm64 CC=$CODE/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang LLVM=1 KBUILD_MODPOST_WARN=1 modules > $OUT/logs/ko-build-mcsuart.log 2>&1
RC=$?
echo BUILD_EXIT=$RC
tail -4 $OUT/logs/ko-build-mcsuart.log
[ $RC -ne 0 ] && exit $RC
$OBJ/scripts/sign-file sha512 $OBJ/certs/signing_key.pem $OBJ/certs/signing_key.x509 mcs_uart.ko && echo SIGNED
modinfo mcs_uart.ko | head -4
