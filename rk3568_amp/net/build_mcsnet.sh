#!/usr/bin/env bash
# Build mcs_net.ko (out-of-tree kernel module) against the OH linux-6.6 tree.
# Same flow as rebuild_ohrt.sh (mcs_km); see that script for CODE autodetect.
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
# ROOT = <mcs>/mcs_net when installed; standalone checkout works too.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$ROOT/out}
SRC=$CODE/out/kernel/src_tmp/linux-6.6
OBJ=$CODE/out/kernel/OBJ/linux-6.6
[ -x $CODE/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang ] || { echo "ERROR: clang not found"; exit 1; }
[ -d $SRC ] || { echo "ERROR: built kernel not found at $SRC"; exit 1; }
export PATH=$CODE/prebuilts/clang/ohos/linux-x86_64/llvm/bin:$PATH
mkdir -p $OUT/logs
cd $ROOT/mcs_net
rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order
rm -rf .tmp_versions
make -C $SRC O=$OBJ M=$PWD ARCH=arm64 CC=$CODE/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang LLVM=1 KBUILD_MODPOST_WARN=1 modules > $OUT/logs/ko-build-mcsnet.log 2>&1
RC=$?
echo BUILD_EXIT=$RC
tail -4 $OUT/logs/ko-build-mcsnet.log
[ $RC -ne 0 ] && exit $RC
$OBJ/scripts/sign-file sha512 $OBJ/certs/signing_key.pem $OBJ/certs/signing_key.x509 mcs_net.ko && echo SIGNED
modinfo mcs_net.ko | head -4
